/*
 * Activite - LoRa + WiFi + LLM (RECEPTEUR COMPLET AVEC RETOUR EMETTEUR)
 * Cegep de Limoilou - Objets connectes
 */

#include <Wire.h>
#include <XPowersLib.h>
#include <SPI.h>
#include <RadioLib.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_wpa2.h>
#include <HTTPClient.h>

// =============================================
// CONFIGURATION (WiFi et API)
// =============================================
#include "config.h"

// =============================================
// PINS T-Beam Supreme
// =============================================

#define PIN_DEL         2     // DEL clignotante de reception
#define PIN_BTN         0     // Bouton "LORA" pour appeler l'IA

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

// =============================================
// VARIABLES
// =============================================

int  dernierPotRecu = 0;      
bool btnPrecedent   = HIGH;   
bool enAttenteLLM   = false;  

// =============================================
// PROTOTYPES
// =============================================

void initPMU();
void initLoRa();
void connecterWiFi();
String appelLLM(int valeurPot);

// =============================================
// SETUP
// =============================================

void setup() {
  Serial.begin(115200);
  delay(1000); // Laisse le temps au terminal de s'ouvrir

  Serial.println("\n\n====================================");
  Serial.println("  DEMARRAGE DU SYSTEME (SANS ECRAN) ");
  Serial.println("====================================\n");

  pinMode(PIN_DEL, OUTPUT);
  pinMode(PIN_BTN, INPUT_PULLUP);
  digitalWrite(PIN_DEL, LOW);

  Wire1.begin(PMU_SDA, PMU_SCL);
  initPMU();
  
  connecterWiFi();
  initLoRa();
  
  Serial.println("\n>>> SYSTEME PRET ! En attente du signal LoRa... <<<");
  Serial.println(">>> Appuie sur le bouton LORA ou tape 'a' puis Entree <<<\n");
}

// =============================================
// LOOP
// =============================================

void loop() {
  
  bool declenchement = false; 

  // 1A. Détection du bouton physique
  bool btnActuel = digitalRead(PIN_BTN);
  if (btnPrecedent == HIGH && btnActuel == LOW && !enAttenteLLM) {
    declenchement = true;
  }
  btnPrecedent = btnActuel;

  // 1B. Détection du clavier (Moniteur Série)
  if (Serial.available() > 0) {
    char touche = Serial.read();
    if (touche == 'a' || touche == 'A') {
      declenchement = true;
    }
  }

  // 2. APPEL DE L'IA SI DECLENCHÉ
  if (declenchement && !enAttenteLLM) {
    if (WiFi.status() == WL_CONNECTED) {
      enAttenteLLM = true;
      Serial.println("\n**************************************************");
      Serial.println("* SIGNAL DE DECLENCHEMENT RECU !                 *");
      Serial.println("* Appel de l'IA avec la valeur : " + String(dernierPotRecu) + "            *");
      Serial.println("**************************************************");
      
      String reponse = appelLLM(dernierPotRecu);
      
      Serial.println("\n----------------- REPONSE IA -----------------");
      Serial.println(reponse);
      Serial.println("----------------------------------------------\n");

      // --- ON RENVOIE LA DÉCISION À L'ÉMETTEUR ---
      Serial.println("[LORA] Transmission de la decision a l'emetteur...");
      JsonDocument docRetour;
      docRetour["ia"] = reponse; 
      String payloadRetour;
      serializeJson(docRetour, payloadRetour);
      radio.transmit(payloadRetour); 
      // -------------------------------------------
      
      delay(3000); 
      enAttenteLLM = false;
      Serial.println("Retour au mode ecoute LoRa...");
    } else {
      Serial.println("\n[ERREUR] Impossible d'appeler l'IA, le WiFi est deconnecte !");
    }
  }

  // 3. GESTION DE LA RECEPTION LORA
  if (!enAttenteLLM) { 
    String strRecue;
    int etat = radio.receive(strRecue); 

    if (etat == RADIOLIB_ERR_NONE) {
      digitalWrite(PIN_DEL, HIGH); 
      
      JsonDocument doc;
      DeserializationError erreur = deserializeJson(doc, strRecue);

      if (!erreur) {
        
        // --- LE FAMEUX FILTRE ---
        // On s'assure que le message contient la clé "appareil" ET que c'est le tien
        if (doc.containsKey("appareil") && doc["appareil"].as<String>() == "T-Beam-1") {
          
          dernierPotRecu = doc["pot"]; 
          String nomAppareil = doc["appareil"].as<String>();
          float forceSignal = radio.getRSSI();
          
          Serial.println("[LORA] Signal recu de " + nomAppareil + " | Valeur Pot: " + String(dernierPotRecu) + " | RSSI: " + String(forceSignal) + " dBm");
        }
        // Si ça vient d'un autre appareil, on l'ignore en silence !
      }
      
      delay(30);
      digitalWrite(PIN_DEL, LOW);
      
    } else if (etat != RADIOLIB_ERR_RX_TIMEOUT) {
      // Ignorer les erreurs de timeout normales pour garder le terminal propre
    }
  }
}

// =============================================
// APPEL API OPENWEBUI / GROQ (LLM)
// =============================================

String appelLLM(int valeurPot) {
  HTTPClient http;
  http.begin(OPENWEBUI_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + API_KEY);
  http.setTimeout(30000); 

  JsonDocument doc;
  doc["model"] = MODEL_NAME;
  JsonArray messages = doc["messages"].to<JsonArray>();
  
  JsonObject systemMsg = messages.add<JsonObject>();
  systemMsg["role"]    = "system";
  systemMsg["content"] = SYSTEM_PROMPT;

  JsonObject userMsg = messages.add<JsonObject>();
  userMsg["role"]    = "user";
  userMsg["content"] = "potentiometre: " + String(valeurPot);

  String payload;
  serializeJson(doc, payload);

  int httpCode = http.POST(payload);
  String reponse = "";

  if (httpCode == 200) {
    String body = http.getString();

    JsonDocument rep;
    DeserializationError erreur = deserializeJson(rep, body);
    
    if (erreur) {
      reponse = "Erreur de decodage JSON : " + String(erreur.c_str());
    } else {
      
      // LA CORRECTION DU EST ICI
      String texteExtrait = rep["choices"]["message"]["content"].as<String>();
      
      if (texteExtrait != "null" && texteExtrait.length() > 0) {
        reponse = texteExtrait;
      } else {
        Serial.println("[DEBUG] ArduinoJson a echoue. Extraction manuelle en cours...");
        
        int indexDebut = body.indexOf("\"content\":\"") + 11;
        int indexFin = body.indexOf("\"},\"logprobs\""); 
        
        if (indexFin == -1) {
          indexFin = body.indexOf("\"}", indexDebut);
        }
        
        if (indexDebut > 11 && indexFin > indexDebut) {
          reponse = body.substring(indexDebut, indexFin);
          reponse.replace("\\n", "\n"); 
        } else {
          reponse = "Erreur fatale : Impossible de lire la phrase de l'IA.";
        }
      }
    }
  } else {
    reponse = "Erreur HTTP serveur: " + String(httpCode);
    Serial.println(http.getString());
  }

  http.end();
  return reponse;
}

// =============================================
// FONCTIONS DE BASE
// =============================================

void connecterWiFi() {
  Serial.print("[WIFI] Connexion a " + String(WIFI_SSID) + " ...");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  delay(100);

  if (USE_WPA2_ENTERPRISE) {
    esp_wifi_sta_wpa2_ent_set_identity((uint8_t*)EAP_IDENTITY, strlen(EAP_IDENTITY));
    esp_wifi_sta_wpa2_ent_set_username((uint8_t*)EAP_USERNAME, strlen(EAP_USERNAME));
    esp_wifi_sta_wpa2_ent_set_password((uint8_t*)EAP_PASSWORD, strlen(EAP_PASSWORD));
    esp_wifi_sta_wpa2_ent_enable();
    WiFi.begin(WIFI_SSID);
  } else {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  int tentatives = 0;
  while (WiFi.status() != WL_CONNECTED && tentatives < 30) {
    delay(500);
    Serial.print(".");
    tentatives++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WIFI] Connecte avec succes ! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n[WIFI] Echec de la connexion.");
  }
}

void initLoRa() {
  Serial.print("[LORA] Demarrage de la puce radio... ");
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  int etat = radio.begin(915.0, 125.0, 9, 7, 18, 10, 8, 1.6, false);
  if (etat == RADIOLIB_ERR_NONE) {
    Serial.println("OK !");
  } else {
    Serial.println("ECHEC (Code: " + String(etat) + ")");
    while(true);
  }
}

void initPMU() {
  if (!pmu.init(Wire1, AXP2101_SLAVE_ADDRESS, PMU_SDA, PMU_SCL)) {
    Serial.println("[PMU] Avertissement: PMU non detecte.");
    return;
  }
  pmu.setALDO1Voltage(3300);  pmu.enableALDO1();  
  pmu.setALDO2Voltage(3300);  pmu.enableALDO2();  
  pmu.setALDO3Voltage(3300);  pmu.enableALDO3();  
  pmu.setALDO4Voltage(3300);  pmu.enableALDO4();  
  pmu.setBLDO1Voltage(3300);  pmu.enableBLDO1();  
  pmu.setBLDO2Voltage(3300);  pmu.enableBLDO2();
  pmu.setDC3Voltage(3300);    pmu.enableDC3();     
  pmu.setDC5Voltage(3300);    pmu.enableDC5();
  Serial.println("[PMU] Gestionnaire d'energie initialise.");
}