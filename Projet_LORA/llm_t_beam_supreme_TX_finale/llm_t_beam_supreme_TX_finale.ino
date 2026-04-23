/*
 * Activite - ÉMETTEUR LoRa (Envoi Potentiometre + Action DEL)
 * Cegep de Limoilou - Objets connectes
 */

#include <Wire.h>
#include <XPowersLib.h>
#include <SPI.h>
#include <RadioLib.h>
#include <ArduinoJson.h>

// =============================================
// PINS T-Beam Supreme
// =============================================

#define PIN_DEL         3     // La DEL physique sur la carte (ou externe)
#define PIN_POT         2     // <--- METS TA PIN DE POTENTIOMETRE ICI

// I2C bus 1 : PMU
#define PMU_SDA         42
#define PMU_SCL         41
#define PMU_IRQ_PIN     40

// SPI & LoRa (Puce SX1262)
#define LORA_SCK        12
#define LORA_MISO       13
#define LORA_MOSI       11
#define LORA_CS         10
#define LORA_DIO1       1
#define LORA_RST        5
#define LORA_BUSY       4

// =============================================
// OBJETS MATERIELS
// =============================================

XPowersAXP2101 pmu;
SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);

unsigned long tempsPrecedent = 0;
const long intervalleEnvoi = 1000; // Envoi toutes les 2 secondes
bool premierEnvoi = true;          // NOUVEAU : Pour forcer le tir au demarrage

// =============================================
// SETUP
// =============================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n====================================");
  Serial.println("       DEMARRAGE EMETTEUR           ");
  Serial.println("====================================\n");

  pinMode(PIN_DEL, OUTPUT);
  digitalWrite(PIN_DEL, LOW);

  Wire1.begin(PMU_SDA, PMU_SCL);
  
  // Initialisation du PMU (Obligatoire pour alimenter la puce LoRa)
  if (!pmu.init(Wire1, AXP2101_SLAVE_ADDRESS, PMU_SDA, PMU_SCL)) {
    Serial.println("[PMU] PMU non detecte.");
  } else {
    pmu.setALDO1Voltage(3300); pmu.enableALDO1();  
    pmu.setALDO2Voltage(3300); pmu.enableALDO2();  
    pmu.setALDO3Voltage(3300); pmu.enableALDO3(); // LoRa
    pmu.setALDO4Voltage(3300); pmu.enableALDO4();  
    pmu.setBLDO1Voltage(3300); pmu.enableBLDO1();  
    pmu.setBLDO2Voltage(3300); pmu.enableBLDO2();
    pmu.setDC3Voltage(3300);   pmu.enableDC3();     
    pmu.setDC5Voltage(3300);   pmu.enableDC5();
  }

  // Initialisation LoRa
  Serial.print("[LORA] Demarrage radio... ");
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  int etat = radio.begin(915.0, 125.0, 9, 7, 18, 10, 8, 1.6, false);
  if (etat == RADIOLIB_ERR_NONE) {
    Serial.println("OK !");
  } else {
    Serial.println("ECHEC (Code: " + String(etat) + ")");
    while(true);
  }
}

// =============================================
// LOOP
// =============================================

void loop() {
  
  // --------------------------------------------------------
  // 1. ENVOI DU POTENTIOMETRE (Toutes les 2 secondes)
  // --------------------------------------------------------
  if (premierEnvoi || millis() - tempsPrecedent >= intervalleEnvoi) {
    tempsPrecedent = millis();
    premierEnvoi = false;
    
    int valPot = analogRead(PIN_POT);

    JsonDocument doc;
    doc["appareil"] = "T-Beam-1";
    doc["pot"] = valPot;
    
    String payload;
    serializeJson(doc, payload);

    Serial.println("[LORA] Envoi -> " + payload);
    radio.transmit(payload);
  }

  // --------------------------------------------------------
  // 2. ECOUTE DES ORDRES DE L'IA (Le reste du temps)
  // --------------------------------------------------------
  String strRecue;
  
  // CORRECTION CRUCIALE : On ajoute un "timeout" de 1000 millisecondes (1 seconde)
  // L'ESP32 n'attendra plus à l'infini. S'il n'y a rien, il continue sa boucle !
  int etat = radio.receive(strRecue, 1000); 

  if (etat == RADIOLIB_ERR_NONE) {
    JsonDocument doc;
    DeserializationError erreur = deserializeJson(doc, strRecue);

    if (!erreur && doc.containsKey("ia")) {
      String ordreIA = doc["ia"].as<String>();
      Serial.println("\n>>> [ORDRE IA RECU] : " + ordreIA + " <<<");

      // ACTION SUR LA DEL SELON LE TEXTE
      ordreIA.toLowerCase(); 

      if (ordreIA.indexOf("faible") >= 0) {
        Serial.println("-> Action : J'ALLUME la lumiere.");
        digitalWrite(PIN_DEL, HIGH);
      } 
      else if (ordreIA.indexOf("forte") >= 0) {
        Serial.println("-> Action : J'ETEINS la lumiere.");
        digitalWrite(PIN_DEL, LOW);
      } 
      else if (ordreIA.indexOf("parfait") >= 0) {
        Serial.println("-> Action : Clin d'oeil (Parfait).");
        digitalWrite(PIN_DEL, HIGH);
        delay(200);
        digitalWrite(PIN_DEL, LOW);
      }
      Serial.println();
    }
  }
}