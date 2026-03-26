// Diagnostic avancé du modem A7670G
// Se concentre sur le problème de connexion réseau

#define TINY_GSM_MODEM_SIM7600
#define TINY_GSM_RX_BUFFER 1024
#define TINY_GSM_DEBUG Serial

#include <TinyGsmClient.h>

// CONFIG MODEM A7670G
#define MODEM_TX 26
#define MODEM_RX 27
#define MODEM_PWRKEY 4
#define MODEM_DTR 32
#define MODEM_RI 33
#define MODEM_FLIGHT 25
#define MODEM_STATUS 34

const char APN[] = "hologram";

HardwareSerial SerialAT(1);
TinyGsm modem(SerialAT);

void modemPowerOn() {
  Serial.println("[MODEM] Allumage du modem...");
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(100);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(1000);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(3000);
  Serial.println("[MODEM] ✓ Modem allumé");
}

void sendAT(const char* cmd, int delayMs = 1000) {
  Serial.print("→ ");
  Serial.println(cmd);
  SerialAT.println(cmd);
  delay(delayMs);

  while (SerialAT.available()) {
    String response = SerialAT.readStringUntil('\n');
    response.trim();
    if (response.length() > 0 && response != cmd) {
      Serial.print("  ");
      Serial.println(response);
    }
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("=== DIAGNOSTIC AVANCÉ MODEM A7670G ===");
  Serial.println("=== Résolution du problème 'no network service' ===");
  Serial.println();

  modemPowerOn();

  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(3000);

  // Test de base
  sendAT("AT");
  sendAT("ATE0"); // Désactiver l'écho

  Serial.println("=== 1. VÉRIFICATION DE LA CARTE SIM ===");
  sendAT("AT+CPIN?");
  sendAT("AT+CIMI");  // IMSI

  Serial.println("=== 2. QUALITÉ DU SIGNAL (CRITIQUE!) ===");
  Serial.println("Note: AT+CSQ doit retourner > 10 pour un signal acceptable");
  sendAT("AT+CSQ");
  delay(2000);
  sendAT("AT+CSQ");
  delay(2000);
  sendAT("AT+CSQ");
  Serial.println("Si CSQ retourne 99,99 ou 0,0 → PAS D'ANTENNE ou SIGNAL TRÈS FAIBLE!");
  Serial.println();

  Serial.println("=== 3. MODE RÉSEAU ACTUEL ===");
  sendAT("AT+CNMP?");  // Mode réseau préféré
  Serial.println("Valeurs possibles:");
  Serial.println("  2 = Automatique (2G/3G/4G)");
  Serial.println("  13 = GSM uniquement (2G)");
  Serial.println("  38 = LTE uniquement (4G)");
  Serial.println("  51 = GSM et LTE");
  Serial.println();

  Serial.println("=== 4. ÉTAT D'ENREGISTREMENT RÉSEAU ===");
  sendAT("AT+CREG?");   // GSM (2G)
  sendAT("AT+CGREG?");  // GPRS (2.5G)
  sendAT("AT+CEREG?");  // LTE (4G)
  Serial.println("Format: +CREG: <n>,<stat>");
  Serial.println("  stat=0 → Non enregistré, pas de recherche");
  Serial.println("  stat=1 → Enregistré (réseau local)");
  Serial.println("  stat=2 → Recherche en cours...");
  Serial.println("  stat=3 → Enregistrement refusé");
  Serial.println("  stat=5 → Enregistré (roaming)");
  Serial.println();

  Serial.println("=== 5. TENTATIVE: FORCER LE MODE AUTOMATIQUE ===");
  Serial.println("Configuration du mode réseau sur AUTOMATIQUE (2G/3G/4G)...");
  sendAT("AT+CNMP=2", 2000);  // Mode automatique
  sendAT("AT+CMNB=3", 2000);  // Toutes les bandes LTE

  Serial.println("=== 6. ACTIVATION DES NOTIFICATIONS D'ÉTAT ===");
  sendAT("AT+CREG=2");  // Activer notifications GSM avec localisation
  sendAT("AT+CGREG=2"); // Activer notifications GPRS avec localisation
  sendAT("AT+CEREG=2"); // Activer notifications LTE avec localisation

  Serial.println("=== 7. RECHERCHE DE RÉSEAU (30 secondes) ===");
  Serial.println("Attendez...");
  sendAT("AT+COPS=0", 30000);  // Sélection automatique de l'opérateur

  Serial.println("=== 8. RÉSULTATS APRÈS CONFIGURATION ===");
  sendAT("AT+COPS?");   // Opérateur sélectionné
  sendAT("AT+CSQ");     // Signal
  sendAT("AT+CREG?");   // État GSM
  sendAT("AT+CGREG?");  // État GPRS
  sendAT("AT+CEREG?");  // État LTE

  Serial.println();
  Serial.println("=== ANALYSE DES RÉSULTATS ===");
  Serial.println();
  Serial.println("PROBLÈMES COURANTS ET SOLUTIONS:");
  Serial.println();
  Serial.println("1. CSQ = 99,99 ou 0,0 → AUCUN SIGNAL");
  Serial.println("   Solutions:");
  Serial.println("   - Vérifiez que l'antenne LTE est bien connectée au connecteur 'MAIN'");
  Serial.println("   - Déplacez l'appareil près d'une fenêtre ou à l'extérieur");
  Serial.println("   - L'antenne fournie peut être de mauvaise qualité");
  Serial.println();
  Serial.println("2. +CME ERROR: no network service");
  Serial.println("   Solutions:");
  Serial.println("   - La carte SIM Hologram doit être ACTIVÉE sur hologram.io");
  Serial.println("   - Vérifiez que la carte a du CRÉDIT (cartes prépayées)");
  Serial.println("   - Essayez une autre carte SIM (Rogers, Bell, Telus)");
  Serial.println();
  Serial.println("3. CREG/CGREG/CEREG retourne stat=2 (recherche)");
  Serial.println("   Solutions:");
  Serial.println("   - Attendez 1-2 minutes supplémentaires");
  Serial.println("   - Le signal peut être trop faible");
  Serial.println();
  Serial.println("4. CREG/CGREG/CEREG retourne stat=3 (refusé)");
  Serial.println("   Solutions:");
  Serial.println("   - La carte SIM n'est PAS activée ou N'A PAS de forfait data");
  Serial.println("   - Connectez-vous au portail Hologram et vérifiez l'état");
  Serial.println();
  Serial.println("=== MODE PASSTHROUGH ACTIVÉ ===");
  Serial.println("Vous pouvez maintenant envoyer des commandes AT manuellement");
  Serial.println("Exemples:");
  Serial.println("  AT+COPS=?  → Lister tous les opérateurs disponibles (lent!)");
  Serial.println("  AT+CPSI?   → Informations système détaillées");
  Serial.println();
}

void loop() {
  // Mode passthrough pour commandes manuelles
  if (SerialAT.available()) {
    Serial.write(SerialAT.read());
  }
  if (Serial.available()) {
    SerialAT.write(Serial.read());
  }
}
