// LilyGO T-SIM A7670G - Version LTE/Cellulaire avec MQTT via WebSocket SSL
// Utilise PubSubClient avec wrapper WebSocket pour simplifier le code MQTT

#define TINY_GSM_MODEM_SIM7600
#define TINY_GSM_RX_BUFFER 1024

#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_MPU6050.h>

// ESP_SSLClient configuration
#define ENABLE_DEBUG
#define ENABLE_ERROR_STRING
#define DEBUG_PORT Serial
#define SSLCLIENT_INSECURE_ONLY

#include <ESP_SSLClient.h>
#include <mbedtls/base64.h>

#include "auth.h"

// ====== CONFIG MODEM A7670G ======
#define MODEM_TX 26
#define MODEM_RX 27
#define MODEM_PWRKEY 4
#define MODEM_DTR 32 // Déplacé de 21 à 32 pour libérer SDA
#define MODEM_RI 34
#define MODEM_FLIGHT 25
#define MODEM_STATUS 35

#define SD_MISO 2
#define SD_MOSI 15
#define SD_SCLK 4
#define SD_CS 5

// ====== CONFIG MQTT/WSS ======
const char* MQTT_HOST = MQTT_BROKER;
const int   MQTT_WSS_PORT = 443;
const char* MQTT_PATH = "/";

char BUTTON1_STATE_TOPIC[50];
char BUTTON2_STATE_TOPIC[50];
char LED1_SET_TOPIC[50];
char LED2_SET_TOPIC[50];
char LED3_SET_TOPIC[50];
char LED1_STATE_TOPIC[50];
char LED2_STATE_TOPIC[50];
char LED3_STATE_TOPIC[50];
char POT_STATE_TOPIC[50];

// --- Variables d'état et Debounce ---
bool led1State = false;
bool led2State = false;
bool led3State = false;
unsigned long lastButton1Press = 0;
unsigned long lastButton2Press = 0;
unsigned long lastButton3Press = 0;
int lastBtn1State = HIGH;
int lastBtn2State = HIGH;
int lastBtn3State = HIGH;
const unsigned long DEBOUNCE_DELAY = 200; // 200ms debounce

int lastPotPercent = -1;
unsigned long lastPotReadTime = 0;

// --- Configuration des broches (Pins) ---
// LED1 (Rouge) -> Pin 25
// LED2 (Verte) -> Pin 33
// LED3 (Bleue) -> Pin 12
const int LED1_PIN = 25;
const int LED2_PIN = 33;
const int LED3_PIN = 12;
const int BUTTON1_PIN = 36;
const int BUTTON2_PIN = 39;
const int BUTTON3_PIN = 32;
const int POT_PIN = 34;
const int I2C_SDA = 21;
const int I2C_SCL = 22;

char ACCEL_STATE_TOPIC[50];
char SIMON_STATUS_TOPIC[50];
char SIMON_SCORE_TOPIC[50];
char SIMON_START_TOPIC[50];
char SIMON_LEVEL_TOPIC[50];

// --- Logique Simon Game (avec niveaux) ---
enum GameState { IDLE, PLAYING_SEQUENCE, WAIT_USER, NEXT_LEVEL, BRAVO };
GameState currentGameState = IDLE;
int simonOrder[20];  // Max 20 LEDs dans la séquence
int simonLength = 0;  // Nombre actuel de LEDs dans la séquence
int simonLevel = 1;   // Niveau actuel
int stepIndex = 0;
unsigned long lastGameAction = 0;

String lastOrientation = "none";
unsigned long lastAccelReadTime = 0;

Adafruit_MPU6050 mpu;

// Serial pour le modem
HardwareSerial SerialAT(1);

// ============================================================================
// CLASSE WRAPPER WEBSOCKET POUR PUBSUBCLIENT
// ============================================================================

class WebSocketClient : public Client {
private:
  ESP_SSLClient* _sslClient;
  bool _wsConnected;

  // Buffer pour les données reçues
  uint8_t _rxBuffer[512];
  size_t _rxBufferLen;
  size_t _rxBufferPos;

  String generateWebSocketKey() {
    uint8_t key[16];
    for(int i = 0; i < 16; i++) {
      key[i] = random(0, 256);
    }
    size_t olen;
    unsigned char output[64];
    mbedtls_base64_encode(output, sizeof(output), &olen, key, 16);
    return String((char*)output);
  }

  bool readWebSocketFrame() {
    if (!_sslClient->available()) return false;

    uint8_t byte1 = _sslClient->read();
    if (!_sslClient->available()) return false;
    uint8_t byte2 = _sslClient->read();

    uint8_t opcode = byte1 & 0x0F;
    bool masked = (byte2 & 0x80) != 0;
    size_t payloadLen = byte2 & 0x7F;

    if (payloadLen == 126) {
      if (_sslClient->available() < 2) return false;
      payloadLen = (_sslClient->read() << 8) | _sslClient->read();
    } else if (payloadLen == 127) {
      if (_sslClient->available() < 8) return false;
      payloadLen = 0;
      for(int i = 0; i < 8; i++) {
        payloadLen = (payloadLen << 8) | _sslClient->read();
      }
    }

    uint8_t mask[4] = {0};
    if (masked) {
      if (_sslClient->available() < 4) return false;
      for(int i = 0; i < 4; i++) {
        mask[i] = _sslClient->read();
      }
    }

    if (opcode == 0x01 || opcode == 0x02) { // Text ou Binary
      if (_sslClient->available() < payloadLen) return false;

      _rxBufferLen = payloadLen < sizeof(_rxBuffer) ? payloadLen : sizeof(_rxBuffer);
      for(size_t i = 0; i < _rxBufferLen; i++) {
        _rxBuffer[i] = _sslClient->read();
        if (masked) _rxBuffer[i] ^= mask[i % 4];
      }
      _rxBufferPos = 0;
      return true;
    }
    else if (opcode == 0x08) { // Close
      Serial.println("[WSS] Serveur a ferme la connexion");
      _wsConnected = false;
      return false;
    }
    else if (opcode == 0x09) { // Ping
      uint8_t pong[2] = {0x8A, 0x00};
      _sslClient->write(pong, 2);
      return false;
    }

    return false;
  }

public:
  WebSocketClient(ESP_SSLClient* sslClient) {
    _sslClient = sslClient;
    _wsConnected = false;
    _rxBufferLen = 0;
    _rxBufferPos = 0;
  }

  int connect(IPAddress ip, uint16_t port) { return 0; }
  int connect(const char *host, uint16_t port) {
    Serial.println("[WSS] Connexion SSL...");

    if (!_sslClient->connect(host, port)) {
      Serial.println("[WSS] Echec connexion SSL");
      return 0;
    }

    Serial.println("[WSS] SSL connecte, envoi handshake WebSocket...");
    String wsKey = generateWebSocketKey();

    _sslClient->print("GET ");
    _sslClient->print(MQTT_PATH);
    _sslClient->print(" HTTP/1.1\r\nHost: ");
    _sslClient->print(host);
    _sslClient->print("\r\nUpgrade: websocket\r\n");
    _sslClient->print("Connection: Upgrade\r\n");
    _sslClient->print("Sec-WebSocket-Key: ");
    _sslClient->print(wsKey);
    _sslClient->print("\r\nSec-WebSocket-Protocol: mqtt\r\n");
    _sslClient->print("Sec-WebSocket-Version: 13\r\n\r\n");

    unsigned long timeout = millis();
    while (!_sslClient->available() && millis() - timeout < 10000) { // Augmenter à 10s pour LTE
      delay(10);
    }

    if (!_sslClient->available()) {
      Serial.println("[WSS] Timeout handshake");
      return 0;
    }

    String response = "";
    while (_sslClient->available()) {
      char c = _sslClient->read();
      response += c;
      if (response.endsWith("\r\n\r\n")) break;
    }

    if (response.indexOf("101") > 0 && response.indexOf("Switching Protocols") > 0) {
      Serial.println("[WSS] Handshake WebSocket reussi!");
      _wsConnected = true;
      return 1;
    } else {
      Serial.println("[WSS] Handshake WebSocket echoue");
      return 0;
    }
  }

  size_t write(uint8_t b) {
    return write(&b, 1);
  }

  size_t write(const uint8_t *buf, size_t size) {
    if (!_wsConnected) return 0;

    // Frame WebSocket binaire avec masque
    uint8_t header[14];
    int headerLen = 2;

    header[0] = 0x82; // FIN + Binary frame

    if (size < 126) {
      header[1] = 0x80 | size;
    } else if (size < 65536) {
      header[1] = 0x80 | 126;
      header[2] = (size >> 8) & 0xFF;
      header[3] = size & 0xFF;
      headerLen = 4;
    } else {
      header[1] = 0x80 | 127;
      for(int i = 0; i < 8; i++) header[2 + i] = 0;
      header[6] = (size >> 24) & 0xFF;
      header[7] = (size >> 16) & 0xFF;
      header[8] = (size >> 8) & 0xFF;
      header[9] = size & 0xFF;
      headerLen = 10;
    }

    // Masque aléatoire
    uint8_t mask[4];
    for(int i = 0; i < 4; i++) {
      mask[i] = random(0, 256);
      header[headerLen + i] = mask[i];
    }
    headerLen += 4;

    _sslClient->write(header, headerLen);

    for(size_t i = 0; i < size; i++) {
      uint8_t maskedByte = buf[i] ^ mask[i % 4];
      _sslClient->write(&maskedByte, 1);
    }

    return size;
  }

  int available() {
    // D'abord vérifier le buffer
    if (_rxBufferPos < _rxBufferLen) {
      return _rxBufferLen - _rxBufferPos;
    }

    // Sinon essayer de lire une nouvelle frame
    if (_sslClient->available()) {
      if (readWebSocketFrame()) {
        return _rxBufferLen - _rxBufferPos;
      }
    }

    return 0;
  }

  int read() {
    if (_rxBufferPos < _rxBufferLen) {
      return _rxBuffer[_rxBufferPos++];
    }

    if (_sslClient->available()) {
      if (readWebSocketFrame() && _rxBufferPos < _rxBufferLen) {
        return _rxBuffer[_rxBufferPos++];
      }
    }

    return -1;
  }

  int read(uint8_t *buf, size_t size) {
    size_t count = 0;
    while (count < size) {
      int c = read();
      if (c < 0) break;
      buf[count++] = (uint8_t)c;
    }
    return count;
  }

  int peek() {
    if (_rxBufferPos < _rxBufferLen) {
      return _rxBuffer[_rxBufferPos];
    }
    return -1;
  }

  void flush() {
    _sslClient->flush();
  }

  void stop() {
    _wsConnected = false;
    _sslClient->stop();
  }

  uint8_t connected() {
    return _wsConnected && _sslClient->connected();
  }

  operator bool() {
    return _wsConnected;
  }
};

// ============================================================================
// CLIENTS ET MQTT
// ============================================================================

TinyGsm modem(SerialAT);
TinyGsmClient gsmClient(modem, 0);
ESP_SSLClient sslClient;
WebSocketClient wsClient(&sslClient);
PubSubClient mqttClient(wsClient);

// État
long lastButtonCheck = 0;
int lastButton1State = HIGH;
int lastButton2State = HIGH;
unsigned long lastGprsCheck = 0;
const unsigned long GPRS_CHECK_INTERVAL = 30000;

// ============================================================================
// CALLBACK MQTT
// ============================================================================

void startSimonGame() {
  simonLevel = 1;
  simonLength = 3;
  // Générer les 3 premières LEDs aléatoires
  for (int i = 0; i < 20; i++) {
    simonOrder[i] = random(1, 4); // 1=Rouge, 2=Vert, 3=Bleu
  }
  stepIndex = 0;
  currentGameState = PLAYING_SEQUENCE;
  char lvlStr[10];
  itoa(simonLevel, lvlStr, 10);
  mqttClient.publish(SIMON_LEVEL_TOPIC, lvlStr);
  mqttClient.publish(SIMON_STATUS_TOPIC, "ECOUTEZ");
  lastGameAction = millis();
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  if (strcmp(topic, SIMON_START_TOPIC) == 0) {
    if (msg == "STOP") {
      currentGameState = IDLE;
      digitalWrite(LED1_PIN, LOW);
      digitalWrite(LED2_PIN, LOW);
      digitalWrite(LED3_PIN, LOW);
      mqttClient.publish(SIMON_STATUS_TOPIC, "IDLE");
      Serial.println("[GAME] Simon arrêté");
    } else {
      Serial.println("[GAME] Start Simon!");
      startSimonGame();
    }
    return;
  }

  Serial.print("[MQTT] <- ");
  Serial.print(topic);
  Serial.print(" = ");
  Serial.println(msg);

  if (strcmp(topic, LED1_SET_TOPIC) == 0) {
    if (msg == "ON") {
      digitalWrite(LED1_PIN, HIGH);
      led1State = true;  // Sync state
      Serial.println("[LED1] Allumee (ROUGE)");
      mqttClient.publish(LED1_STATE_TOPIC, "ON"); // Publier état à jour
    } else if (msg == "OFF") {
      digitalWrite(LED1_PIN, LOW);
      led1State = false; // Sync state
      Serial.println("[LED1] Eteinte");
      mqttClient.publish(LED1_STATE_TOPIC, "OFF"); // Publier état à jour
    }
  }
  else if (strcmp(topic, LED2_SET_TOPIC) == 0) {
    if (msg == "ON") {
      digitalWrite(LED2_PIN, HIGH);
      led2State = true;  // Sync state
      Serial.println("[LED2] Allumee (VERTE)");
      mqttClient.publish(LED2_STATE_TOPIC, "ON"); // Publier état à jour
    } else if (msg == "OFF") {
      digitalWrite(LED2_PIN, LOW);
      led2State = false; // Sync state
      Serial.println("[LED2] Eteinte");
      mqttClient.publish(LED2_STATE_TOPIC, "OFF"); // Publier état à jour
    }
  }
  else if (strcmp(topic, LED3_SET_TOPIC) == 0) {
    if (msg == "ON") {
      digitalWrite(LED3_PIN, HIGH);
      led3State = true;  // Sync state
      Serial.println("[LED3] Allumee (BLEUE)");
      mqttClient.publish(LED3_STATE_TOPIC, "ON"); // Publier état à jour
    } else if (msg == "OFF") {
      digitalWrite(LED3_PIN, LOW);
      led3State = false; // Sync state
      Serial.println("[LED3] Eteinte");
      mqttClient.publish(LED3_STATE_TOPIC, "OFF"); // Publier état à jour
    }
  }
}

// ============================================================================
// FONCTIONS MODEM
// ============================================================================

void modemPowerOn() {
  Serial.println("[MODEM] Allumage du modem...");
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(100);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(1000);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(3000);
  Serial.println("[MODEM] Modem allume");
}

bool initModem() {
  Serial.println("[MODEM] Initialisation...");

  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(3000);

  if (!modem.restart()) {
    Serial.println("[MODEM] Echec du redemarrage");
    return false;
  }

  String modemInfo = modem.getModemInfo();
  Serial.print("[MODEM] Info: ");
  Serial.println(modemInfo);

  String imei = modem.getIMEI();
  Serial.print("[MODEM] IMEI: ");
  Serial.println(imei);

  Serial.print("[MQTT] Device ID: ");
  Serial.println(MQTT_CLIENT_ID);

  snprintf(LED1_SET_TOPIC, sizeof(LED1_SET_TOPIC), "%s/led/1/set", MQTT_CLIENT_ID);
  snprintf(LED2_SET_TOPIC, sizeof(LED2_SET_TOPIC), "%s/led/2/set", MQTT_CLIENT_ID);
  snprintf(LED3_SET_TOPIC, sizeof(LED3_SET_TOPIC), "%s/led/3/set", MQTT_CLIENT_ID);
  snprintf(BUTTON1_STATE_TOPIC, sizeof(BUTTON1_STATE_TOPIC), "%s/button/1/state", MQTT_CLIENT_ID);
  snprintf(BUTTON2_STATE_TOPIC, sizeof(BUTTON2_STATE_TOPIC), "%s/button/2/state", MQTT_CLIENT_ID);
  snprintf(LED1_STATE_TOPIC, sizeof(LED1_STATE_TOPIC), "%s/led/1/state", MQTT_CLIENT_ID);
  snprintf(LED2_STATE_TOPIC, sizeof(LED2_STATE_TOPIC), "%s/led/2/state", MQTT_CLIENT_ID);
  snprintf(LED3_STATE_TOPIC, sizeof(LED3_STATE_TOPIC), "%s/led/3/state", MQTT_CLIENT_ID);
  snprintf(POT_STATE_TOPIC, sizeof(POT_STATE_TOPIC), "%s/potentiometer/state", MQTT_CLIENT_ID);
  snprintf(ACCEL_STATE_TOPIC, sizeof(ACCEL_STATE_TOPIC), "%s/accelerometer/orientation", MQTT_CLIENT_ID);
  snprintf(SIMON_STATUS_TOPIC, sizeof(SIMON_STATUS_TOPIC), "%s/simon/status", MQTT_CLIENT_ID);
  snprintf(SIMON_SCORE_TOPIC, sizeof(SIMON_SCORE_TOPIC), "%s/simon/score", MQTT_CLIENT_ID);
  snprintf(SIMON_START_TOPIC, sizeof(SIMON_START_TOPIC), "%s/simon/start", MQTT_CLIENT_ID);
  snprintf(SIMON_LEVEL_TOPIC, sizeof(SIMON_LEVEL_TOPIC), "%s/simon/level", MQTT_CLIENT_ID);

  Serial.println("[MODEM] Initialise");
  return true;
}

bool connectToNetwork() {
  Serial.println("[NETWORK] Configuration de l'APN...");

  modem.sendAT("+CGDCONT=1,\"IP\",\"", APN, "\"");
  if (modem.waitResponse() != 1) {
    Serial.println("[NETWORK] Echec de configuration APN");
  } else {
    Serial.println("[NETWORK] APN configure");
  }

  Serial.println("[NETWORK] Connexion au reseau cellulaire...");

  if (!modem.waitForNetwork(60000L)) {
    Serial.println("[NETWORK] Echec de connexion au reseau");
    return false;
  }

  String operator_name = modem.getOperator();
  Serial.print("[NETWORK] Operateur: ");
  Serial.println(operator_name);

  int signalQuality = modem.getSignalQuality();
  Serial.print("[NETWORK] Signal: ");
  Serial.print(signalQuality);
  Serial.println(" dBm");

  Serial.println("[GPRS] Connexion GPRS...");
  if (!modem.gprsConnect(APN, APN_USER, APN_PASS)) {
    Serial.println("[GPRS] Echec de connexion GPRS");
    return false;
  }

  if (!modem.isGprsConnected()) {
    Serial.println("[GPRS] GPRS non connecte");
    return false;
  }

  IPAddress ip = modem.localIP();
  Serial.print("[GPRS] IP: ");
  Serial.println(ip);
  Serial.println("[GPRS] Connecte");

  return true;
}

void checkButtons() {
  unsigned long now = millis();

  // --- BOUTON 1 (GPIO 36) ---
  int currentBtn1 = digitalRead(BUTTON1_PIN);
  
  // Détection front descendant (HIGH -> LOW)
  if (currentBtn1 == LOW && lastBtn1State == HIGH) {
    if (now - lastButton1Press > DEBOUNCE_DELAY) {
      lastButton1Press = now;
      led1State = !led1State; // Toggle

      // Action locale
      digitalWrite(LED1_PIN, led1State ? HIGH : LOW);
      Serial.print("[BTN1] Toggle -> ");
      Serial.println(led1State ? "ON" : "OFF");

      // Action MQTT (si connecté)
      if (mqttClient.connected()) {
        const char* state = led1State ? "ON" : "OFF";
        mqttClient.publish(LED1_STATE_TOPIC, state);
      }
    }
  }
  lastBtn1State = currentBtn1; // Mémoriser l'état

  // --- BOUTON 2 (GPIO 35) ---
  int currentBtn2 = digitalRead(BUTTON2_PIN);

  // Détection front descendant (HIGH -> LOW)
  if (currentBtn2 == LOW && lastBtn2State == HIGH) {
    if (now - lastButton2Press > DEBOUNCE_DELAY) {
      lastButton2Press = now;
      led2State = !led2State; // Toggle

      // Action locale
      digitalWrite(LED2_PIN, led2State ? HIGH : LOW);
      Serial.print("[BTN2] Toggle -> ");
      Serial.println(led2State ? "ON" : "OFF");

      // Action MQTT (si connecté)
      if (mqttClient.connected()) {
        const char* state = led2State ? "ON" : "OFF";
        mqttClient.publish(LED2_STATE_TOPIC, state);
      }
    }
  }
  lastBtn2State = currentBtn2; // Mémoriser l'état

  // --- BOUTON 3 (GPIO 39) ---
  int currentBtn3 = digitalRead(BUTTON3_PIN);

  // Détection front descendant (HIGH -> LOW)
  if (currentBtn3 == LOW && lastBtn3State == HIGH) {
    if (now - lastButton3Press > DEBOUNCE_DELAY) {
      lastButton3Press = now;
      led3State = !led3State; // Toggle

      // Action locale
      digitalWrite(LED3_PIN, led3State ? HIGH : LOW);
      Serial.print("[BTN3] Toggle -> ");
      Serial.println(led3State ? "ON" : "OFF");

      // Action MQTT (si connecté)
      if (mqttClient.connected()) {
        const char* state = led3State ? "ON" : "OFF";
        mqttClient.publish(LED3_STATE_TOPIC, state);
      }
    }
  }
  lastBtn3State = currentBtn3; // Mémoriser l'état
}

void checkPotentiometer() {
  unsigned long now = millis();
  if (now - lastPotReadTime > 50) { // Read every 50ms for near real-time reaction
    lastPotReadTime = now;
    int potValue = analogRead(POT_PIN);
    // ESP32 ADC is 12-bit (0-4095)
    int potPercent = map(potValue, 0, 4095, 0, 100);
    // Reduced hysteresis to 1% to make it very responsive
    if (abs(potPercent - lastPotPercent) >= 1 || lastPotPercent == -1) {
      lastPotPercent = potPercent;
      char potStr[10];
      itoa(potPercent, potStr, 10);
      
      // Action MQTT (si connecté)
      if (mqttClient.connected()) {
        mqttClient.publish(POT_STATE_TOPIC, potStr);
      }
    }
  }
}

void checkAccelerometer() {
  unsigned long now = millis();
  if (now - lastAccelReadTime > 800) { // Every 800ms
    lastAccelReadTime = now;
    
    sensors_event_t a, g, temp;
    if (mpu.getEvent(&a, &g, &temp)) {
      String currentOrientation = "flat";
      float absX = abs(a.acceleration.x);
      float absY = abs(a.acceleration.y);

      // Seuil élevé (8.0) pour ne déclencher qu'à environ 90 degrés
      if (absX > absY && absX > 8.0) {
        currentOrientation = (a.acceleration.x > 0) ? "vertical_left" : "vertical_right";
      } else if (absY > 8.0) {
        currentOrientation = (a.acceleration.y > 0) ? "horizontal_up" : "horizontal_down";
      } else {
        currentOrientation = "flat";
      }

      if (currentOrientation != lastOrientation) {
        lastOrientation = currentOrientation;
        Serial.print(">>> ORIENTATION: ");
        Serial.println(currentOrientation);

        if (mqttClient.connected()) {
          mqttClient.publish(ACCEL_STATE_TOPIC, currentOrientation.c_str());
        }
      }
    }
  }
}

bool reconnectMQTT() {
  Serial.println("[MQTT] Connexion au broker...");

  if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)) {
    Serial.println("[MQTT] Connecte!");

    mqttClient.subscribe(LED1_SET_TOPIC);
    mqttClient.subscribe(LED2_SET_TOPIC);
    mqttClient.subscribe(LED3_SET_TOPIC);
    mqttClient.subscribe(SIMON_START_TOPIC); // Simon Start subscription
    Serial.println("[MQTT] Souscriptions envoyees");

    return true;
  }

  Serial.print("[MQTT] Echec, code: ");
  Serial.println(mqttClient.state());
  return false;
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("=== LilyGo T-SIM A7670G - MQTT via LTE + WebSocket SSL ===");
  Serial.println();

  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);
  pinMode(BUTTON1_PIN, INPUT); // Utilisation d'un pull-up externe
  pinMode(BUTTON2_PIN, INPUT); // Utilisation d'un pull-up externe
  pinMode(BUTTON3_PIN, INPUT); // Utilisation d'un pull-up externe

  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  digitalWrite(LED3_PIN, LOW);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000); 
  
  Serial.println("[I2C] Scan en cours...");
  byte error, address;
  int nDevices = 0;
  for(address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error == 0) {
      Serial.print("[I2C] Appareil trouve a l'adresse 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
      nDevices++;
    }
  }
  if (nDevices == 0) Serial.println("[I2C] Aucun appareil trouve!");

  if (!mpu.begin()) {
    Serial.println("[ERREUR] MPU6050 non detecte");
  } else {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    Serial.println("[OK] MPU6050 detecte et initialise");
  }

  modemPowerOn();

  if (!initModem()) {
    Serial.println("[ERREUR] Impossible d'initialiser le modem");
    while (true) {
      digitalWrite(LED1_PIN, !digitalRead(LED1_PIN));
      delay(200);
    }
  }

  if (!connectToNetwork()) {
    Serial.println("[ERREUR] Impossible de se connecter au reseau");
    while (true) {
      digitalWrite(LED1_PIN, !digitalRead(LED1_PIN));
      delay(500);
    }
  }

  // Configurer ESP_SSLClient
  Serial.println("[SSL] Configuration du client SSL...");
  sslClient.setClient(&gsmClient);
  sslClient.setInsecure();
  sslClient.setBufferSizes(2048, 1024);
  sslClient.setDebugLevel(1);

  // Configurer MQTT
  mqttClient.setServer(MQTT_HOST, MQTT_WSS_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setKeepAlive(60);

  // Connexion WebSocket et MQTT
  if (!wsClient.connect(MQTT_HOST, MQTT_WSS_PORT)) {
    Serial.println("[ERREUR] Impossible de se connecter via WebSocket");
    while (true) {
      digitalWrite(LED1_PIN, !digitalRead(LED1_PIN));
      delay(1000);
    }
  }

  if (!reconnectMQTT()) {
    Serial.println("[ERREUR] Impossible de se connecter au broker MQTT");
    while (true) {
      digitalWrite(LED1_PIN, !digitalRead(LED1_PIN));
      delay(1000);
    }
  }

  Serial.println();
  Serial.println("=== Systeme pret ===");
  Serial.println();

  for (int i = 0; i < 3; i++) {
    digitalWrite(LED1_PIN, HIGH);
    digitalWrite(LED2_PIN, HIGH);
    delay(200);
    digitalWrite(LED1_PIN, LOW);
    digitalWrite(LED2_PIN, LOW);
    delay(200);
  }
}

// ============================================================================
// LOOP
// ============================================================================

int lastSimonBtn1 = HIGH;
int lastSimonBtn2 = HIGH;
int lastSimonBtn3 = HIGH;

void updateSimonGame() {
  if (currentGameState == IDLE) return;

  // --- Afficher le niveau pendant 1.5 secondes avant de montrer la séquence ---
  if (currentGameState == NEXT_LEVEL) {
    char lvlStr[10];
    itoa(simonLevel, lvlStr, 10);
    mqttClient.publish(SIMON_LEVEL_TOPIC, lvlStr);
    char statusMsg[30];
    snprintf(statusMsg, sizeof(statusMsg), "LVL %d", simonLevel);
    mqttClient.publish(SIMON_STATUS_TOPIC, statusMsg);
    delay(1500);
    currentGameState = PLAYING_SEQUENCE;
  }

  // --- Phase 1 : Montrer la séquence ---
  if (currentGameState == PLAYING_SEQUENCE) {
    mqttClient.publish(SIMON_STATUS_TOPIC, "ECOUTEZ");
    delay(500);
    for (int i = 0; i < simonLength; i++) {
      int pin = (simonOrder[i] == 1) ? LED1_PIN : (simonOrder[i] == 2) ? LED2_PIN : LED3_PIN;
      digitalWrite(pin, HIGH);
      delay(600);
      digitalWrite(pin, LOW);
      delay(350);
    }
    stepIndex = 0;
    currentGameState = WAIT_USER;
    mqttClient.publish(SIMON_STATUS_TOPIC, "YOUR TURN");
    lastGameAction = millis();
    lastSimonBtn1 = digitalRead(BUTTON1_PIN);
    lastSimonBtn2 = digitalRead(BUTTON2_PIN);
    lastSimonBtn3 = digitalRead(BUTTON3_PIN);
  }

  // --- Phase 2 : Attendre les appuis ---
  if (currentGameState == WAIT_USER) {
    int currentB1 = digitalRead(BUTTON1_PIN);
    int currentB2 = digitalRead(BUTTON2_PIN);
    int currentB3 = digitalRead(BUTTON3_PIN);

    int pressed = 0;
    if (currentB1 == LOW && lastSimonBtn1 == HIGH) pressed = 1;
    else if (currentB2 == LOW && lastSimonBtn2 == HIGH) pressed = 2;
    else if (currentB3 == LOW && lastSimonBtn3 == HIGH) pressed = 3;

    lastSimonBtn1 = currentB1;
    lastSimonBtn2 = currentB2;
    lastSimonBtn3 = currentB3;

    if (pressed > 0) {
      int pin = (pressed == 1) ? LED1_PIN : (pressed == 2) ? LED2_PIN : LED3_PIN;
      digitalWrite(pin, HIGH);
      delay(300);
      digitalWrite(pin, LOW);

      if (pressed == simonOrder[stepIndex]) {
        stepIndex++;
        if (stepIndex >= simonLength) {
          // Niveau réussi !
          simonLevel++;
          simonLength++; // Ajouter une LED de plus
          currentGameState = NEXT_LEVEL;
          Serial.print("[SIMON] Niveau ");
          Serial.print(simonLevel - 1);
          Serial.println(" réussi !");
        }
      } else {
        // PERDU
        for (int i = 0; i < 4; i++) {
          digitalWrite(LED1_PIN, HIGH); digitalWrite(LED2_PIN, HIGH); digitalWrite(LED3_PIN, HIGH);
          delay(200);
          digitalWrite(LED1_PIN, LOW); digitalWrite(LED2_PIN, LOW); digitalWrite(LED3_PIN, LOW);
          delay(200);
        }
        currentGameState = IDLE;
        mqttClient.publish(SIMON_STATUS_TOPIC, "PERDU");
        Serial.println("[SIMON] PERDU");
      }
    }
  }

  if (currentGameState == BRAVO) {
    // Animation de victoire
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED1_PIN, HIGH); delay(150); digitalWrite(LED1_PIN, LOW);
      digitalWrite(LED2_PIN, HIGH); delay(150); digitalWrite(LED2_PIN, LOW);
      digitalWrite(LED3_PIN, HIGH); delay(150); digitalWrite(LED3_PIN, LOW);
    }
    currentGameState = IDLE;
  }
}

void loop() {
  unsigned long now = millis();

  // Vérifier la connexion GPRS
  if (now - lastGprsCheck > GPRS_CHECK_INTERVAL) {
    lastGprsCheck = now;

    if (!modem.isGprsConnected()) {
      Serial.println("[GPRS] Connexion perdue");

      if (connectToNetwork()) {
        if (wsClient.connect(MQTT_HOST, MQTT_WSS_PORT)) {
          reconnectMQTT();
        }
      }
    }
  }

  // Maintenir la connexion MQTT
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }

  mqttClient.loop();

  // Mettre à jour le jeu Simon
  updateSimonGame();

  // Ne lire les boutons/pot/accel que si le jeu n'est pas en cours
  if (currentGameState == IDLE) {
    // Vérifier les boutons
    checkButtons();

    // Vérifier le potentiomètre
    checkPotentiometer();

    // Vérifier l'accéléromètre
    checkAccelerometer();
  }

  delay(10);
}
