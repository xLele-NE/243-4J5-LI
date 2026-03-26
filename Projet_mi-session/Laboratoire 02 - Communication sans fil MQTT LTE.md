<div style="background: linear-gradient(90deg, #0ea5e9, #6366f1); padding: 18px 20px; color: #f8fafc; border-radius: 14px; box-shadow: 0 4px 12px rgba(0,0,0,0.15);">
  <h1 style="margin: 0; font-size: 28px;">Labo 2 — Communication sans fil et télémétrie IoT</h1>
  <p style="margin: 6px 0 0; font-size: 15px;">Du câble série au réseau cellulaire : communication MQTT via WiFi et LTE avec contrôle de LEDs.</p>
</div>

---

## 📐 Architecture du système

```mermaid
graph TD
    %% ==== STYLES ====
    classDef zoneClient fill:#f0fdf4,stroke:#16a34a,stroke-width:2px,color:#052e16;
    classDef zoneAccess fill:#eff6ff,stroke:#2563eb,stroke-width:2px,color:#0f172a;
    classDef zoneLab fill:#f9fafb,stroke:#4b5563,stroke-width:2px,color:#020617;
    classDef zoneCloud fill:#fefce8,stroke:#d97706,stroke-width:2px,color:#451a03;
    classDef componentCore fill:#e5e7eb,stroke:#4b5563,stroke-width:1.5px;
    classDef componentService fill:#eef2ff,stroke:#6366f1,stroke-width:1.5px;
    classDef componentDevice fill:#ecfeff,stroke:#06b6d4,stroke-width:1.5px;
    classDef wireless fill:#fae8ff,stroke:#a855f7,stroke-width:1.5px;

    %% ==== ZONE CLIENT ====
    subgraph Zone_Client ["💻 Poste de développement"]
        Dev_PC["Terminal SSH + Navigateur"]:::zoneClient
    end

    %% ==== ZONE LAB / ON-PREM ====
    subgraph Zone_Lab ["🏠 Lab On-Prem"]
        subgraph RPi5_Core ["🍓 Raspberry Pi 5"]
            SSHD["SSH Server"]:::componentCore
            MQTT_Local["Mosquitto Broker"]:::componentService
            Python_UI["Interface tactile Python"]:::componentService
        end

        subgraph Lab_Devices ["📱 Périphériques"]
            Touchscreen["Écran tactile"]:::componentDevice
            LilyGO_A7670G["LilyGO A7670G<br/>(ESP32 + LTE + GPS)"]:::wireless
        end
    end

    %% ==== ZONE CLOUD / SAAS ====
    subgraph Zone_Cloud ["☁️ Services Cloud"]
        Cloudflare["Cloudflare Tunnel<br/>(WSS Proxy)"]:::zoneCloud
        Cellular_Network["Réseau Cellulaire<br/>(LTE Cat-1)"]:::zoneCloud
    end

    %% ==== FLUX PRINCIPAUX ====

    %% 1. ACCÈS DISTANT
    Dev_PC -->|"SSH"| SSHD

    %% 2. COMMUNICATION MQTT via Cloudflare
    Python_UI -->|"MQTT local"| MQTT_Local
    MQTT_Local -->|"WSS via Tunnel"| Cloudflare
    LilyGO_A7670G -->|"MQTT via WiFi WSS"| Cloudflare
    LilyGO_A7670G -->|"MQTT via LTE WSS"| Cellular_Network
    Cellular_Network -->|"Internet"| Cloudflare
    Cloudflare -->|"Tunnel"| MQTT_Local

    %% 3. INTERACTIONS TACTILES
    Python_UI -->|"/dev/input"| Touchscreen
```

Ce diagramme illustre l'architecture avec communication sans fil:
- **Zone Client (vert):** Votre poste de développement
- **Zone Lab (gris):** Raspberry Pi 5 avec broker Mosquitto et interface tactile
- **Zone Cloud (jaune):** Cloudflare Tunnel pour exposer le broker en WSS
- **Communication sans fil (violet):** LilyGO communique via WiFi ou LTE

---

## 🧭 Plan du guide
- [Matériel et branchements](#-matériel-et-branchements)
- [Introduction au protocole MQTT](#1-introduction-au-protocole-mqtt)
- [Configuration du broker MQTT avec WSS](#2-configuration-du-broker-mqtt-avec-wss)
- [Communication MQTT via WiFi](#3-communication-mqtt-via-wifi)
- [Communication MQTT via LTE](#4-communication-mqtt-via-lte)
- [Interface tactile Python](#5-interface-tactile-python)
- [Exercice : Boutons physiques](#6-exercice-boutons-physiques)
- [Exercice : Montage complet](#7-exercice-montage-complet)

<div style="height: 6px; background: linear-gradient(90deg, #22d3ee, #22c55e); border-radius: 999px; margin: 18px 0;"></div>

## 🎒 Matériel et branchements

### Matériel requis

<div style="background:#ecfeff; border:1px solid #06b6d4; padding:12px 14px; border-radius:10px;">
<ul style="margin:0;">
  <li>LilyGO T-SIM A7670G avec antennes GPS et LTE</li>
  <li>Carte SIM avec forfait de données actif</li>
  <li>Raspberry Pi 5 avec écran tactile</li>
  <li>2 LEDs (rouge et verte)</li>
  <li>2 boutons poussoirs</li>
  <li>Résistances (220Ω-330Ω pour LEDs)</li>
  <li>Plaquette de prototypage et fils de connexion</li>
  <li>Câble USB-A vers USB-C</li>
</ul>
</div>

### Configuration des GPIO

Tous les codes de ce laboratoire utilisent les mêmes pins GPIO :

| Composant | GPIO | Description |
|-----------|------|-------------|
| **LED 1 (Rouge)** | GPIO 32 | Sortie - Connecter avec résistance 220Ω |
| **LED 2 (Verte)** | GPIO 33 | Sortie - Connecter avec résistance 220Ω |
| **Bouton 1** | GPIO 34 | Entrée avec pull-up interne |
| **Bouton 2** | GPIO 35 | Entrée avec pull-up interne |

### Schéma de branchement

```
LilyGO A7670G
┌─────────────────────────────────┐
│                                 │
│  GPIO 32 ──[220Ω]──[LED ROUGE]──┤─── GND
│  GPIO 33 ──[220Ω]──[LED VERTE]──┤─── GND
│                                 │
│  GPIO 34 ──[BTN1]───────────────┤─── GND
│  GPIO 35 ──[BTN2]───────────────┤─── GND
│                                 │
│  [ANT LTE]     [ANT GPS]        │
│  [Slot SIM]    [USB-C]          │
└─────────────────────────────────┘
```

<div style="background:#fee2e2; border:1px solid #ef4444; padding:10px 12px; border-radius:10px;">
<strong>⚠️ Important avant de commencer</strong>
<ul>
  <li>Carte SIM avec forfait de données actif et PIN désactivé</li>
  <li>Antenne LTE vissée sur le connecteur LTE (pas GPS!)</li>
  <li>Antenne GPS vissée sur le connecteur GPS</li>
  <li>Les boutons sont connectés entre GPIO et GND (pull-up interne activé)</li>
</ul>
</div>

<div style="height: 5px; background: linear-gradient(90deg, #f59e0b, #fb7185); border-radius: 999px; margin: 22px 0;"></div>

## 1. Introduction au protocole MQTT

> 🎯 **Objectif :** comprendre MQTT et ses avantages pour l'IoT.

### 💡 Concepts clés

**Qu'est-ce que MQTT?**

MQTT (Message Queuing Telemetry Transport) est un protocole de messagerie léger conçu pour l'IoT. Il utilise une architecture **publish/subscribe** où les appareils communiquent via un **broker** central.

**Architecture Publish/Subscribe:**

```
[Publisher] --publish--> [Broker] --deliver--> [Subscriber(s)]
                           ↕
                    [Topics/Routes]
```

**Topics (sujets):**

Les topics sont des chaînes hiérarchiques qui organisent les messages :

```
esp32-123456/
├── led/
│   ├── 1/set        # Commande LED rouge (ON/OFF)
│   └── 2/set        # Commande LED verte (ON/OFF)
└── button/
    ├── 1/state      # État bouton 1 (PRESSED/RELEASED)
    └── 2/state      # État bouton 2 (PRESSED/RELEASED)
```

**Device ID (identifiant de l'appareil):**

Le préfixe des topics (`esp32-123456` dans l'exemple ci-dessus) est le **Device ID**. Cet identifiant unique permet de distinguer plusieurs appareils sur le même broker MQTT.

<div style="background:#dbeafe; border:1px solid #3b82f6; padding:10px 12px; border-radius:10px;">
<strong>📝 Configuration du Device ID</strong>
<p>Le Device ID est configuré dans le fichier <code>auth.h</code> via la variable <code>MQTT_CLIENT_ID</code> :</p>
<ul>
  <li><strong>WiFi :</strong> <code>labo2/code/lilygo_wifi_mschapv2/auth.h</code></li>
  <li><strong>LTE :</strong> <code>labo2/code/lilygo_lte_mqtt/auth.h</code></li>
</ul>
<p>Choisissez un identifiant unique et significatif (ex: <code>esp32-cuisine</code>, <code>lte-garage</code>).</p>
</div>

**Avantages de MQTT pour l'IoT:**
- ✅ **Léger:** Headers minimaux (~2 bytes)
- ✅ **Bidirectionnel:** Publish et subscribe sur le même canal
- ✅ **Découplé:** L'interface ne dépend pas directement du LilyGO
- ✅ **Extensible:** Plusieurs clients peuvent contrôler le même appareil

<div style="height: 5px; background: linear-gradient(90deg, #22c55e, #84cc16); border-radius: 999px; margin: 22px 0;"></div>

## 2. Configuration du broker MQTT avec WSS

> 🔒 **Objectif :** configurer Mosquitto sur le Raspberry Pi et l'exposer via Cloudflare Tunnel en WebSocket Secure (WSS).

### 💡 Pourquoi WSS via Cloudflare?

Les appareils IoT (LilyGO via WiFi ou LTE) doivent pouvoir se connecter au broker MQTT depuis n'importe où sur Internet. Cependant :
- Le Raspberry Pi est derrière un NAT (pas d'IP publique directe)
- MQTT standard (port 1883) n'est pas chiffré
- Les connexions LTE passent par Internet public

**Solution :** Utiliser **Cloudflare Tunnel** pour exposer le broker Mosquitto en **WSS (WebSocket Secure)** sur le port 443 avec chiffrement TLS.

```
[LilyGO] → [Internet/LTE] → [Cloudflare WSS :443] → [Tunnel] → [RPi Mosquitto :1883]
```

### 2.1 Installation de Mosquitto

```bash
sudo apt update
sudo apt install -y mosquitto mosquitto-clients
```

### 2.2 Configuration de Mosquitto

**Créer le fichier de configuration :**
```bash
sudo nano /etc/mosquitto/conf.d/default.conf
```

**Contenu :**
```conf
# Listener MQTT standard (local uniquement)
listener 1883 localhost

# Listener WebSocket (pour Cloudflare Tunnel)
listener 9001
protocol websockets

# Authentification
allow_anonymous false
password_file /etc/mosquitto/passwd

# Logging
log_dest file /var/log/mosquitto/mosquitto.log
log_type all
```

### 2.3 Création des utilisateurs MQTT

**Créer un utilisateur pour les ESP32 :**
```bash
sudo mosquitto_passwd -c /etc/mosquitto/passwd esp_user
```

Entrez un mot de passe sécurisé. **Notez-le**, vous en aurez besoin pour la configuration des appareils.

**Ajouter d'autres utilisateurs si nécessaire :**
```bash
sudo mosquitto_passwd /etc/mosquitto/passwd autre_utilisateur
```

### 2.4 Redémarrer Mosquitto

```bash
sudo systemctl restart mosquitto
sudo systemctl status mosquitto
```

**Vérifier que les ports sont ouverts :**
```bash
sudo ss -tlnp | grep mosquitto
```

Vous devriez voir :
```
LISTEN  0  100  127.0.0.1:1883  *:*  users:(("mosquitto",pid=...))
LISTEN  0  100        *:9001    *:*  users:(("mosquitto",pid=...))
```

### 2.5 Test local

**Terminal 1 - Subscriber :**
```bash
mosquitto_sub -h localhost -p 1883 -u esp_user -P VOTRE_MOT_DE_PASSE -t "test/#" -v
```

**Terminal 2 - Publisher :**
```bash
mosquitto_pub -h localhost -p 1883 -u esp_user -P VOTRE_MOT_DE_PASSE -t "test/hello" -m "Hello MQTT!"
```

### 2.6 Configuration Cloudflare Tunnel

Le tunnel Cloudflare expose le broker Mosquitto WebSocket (port 9001) sur Internet en HTTPS/WSS.

**Prérequis :** Avoir déjà configuré `cloudflared` (voir Labo 1).

**Modifier la configuration du tunnel :**
```bash
nano ~/.cloudflared/config.yml
```

**Ajouter le service MQTT :**
```yaml
tunnel: <VOTRE-UUID-TUNNEL>
credentials-file: /home/<USER>/.cloudflared/<UUID>.json

ingress:
  # SSH existant
  - hostname: rpi.votredomaine.ca
    service: ssh://localhost:22

  # MQTT WebSocket
  - hostname: mqtt.votredomaine.ca
    service: http://localhost:9001

  # Règle par défaut
  - service: http_status:404
```

**Redémarrer le tunnel :**
```bash
sudo systemctl restart cloudflared
```

### 2.7 Configuration DNS Cloudflare

1. Connectez-vous au dashboard Cloudflare
2. Allez dans **DNS** pour votre domaine
3. Le tunnel devrait avoir créé automatiquement l'entrée `mqtt.votredomaine.ca`
4. Sinon, exécutez :
```bash
cloudflared tunnel route dns <NOM-TUNNEL> mqtt.votredomaine.ca
```

### 2.8 Test de connexion WSS

**Depuis n'importe quel appareil avec accès Internet :**

```bash
# Installer un client MQTT avec support WebSocket
pip3 install paho-mqtt

# Tester avec Python
python3 << 'EOF'
import paho.mqtt.client as mqtt
import ssl

client = mqtt.Client(transport="websockets")
client.tls_set(cert_reqs=ssl.CERT_REQUIRED, tls_version=ssl.PROTOCOL_TLS)
client.username_pw_set("esp_user", "VOTRE_MOT_DE_PASSE")

def on_connect(client, userdata, flags, rc):
    print(f"Connecté avec code: {rc}")
    client.subscribe("test/#")

def on_message(client, userdata, msg):
    print(f"{msg.topic}: {msg.payload.decode()}")

client.on_connect = on_connect
client.on_message = on_message

client.connect("mqtt.votredomaine.ca", 443)
client.loop_forever()
EOF
```

<div style="background:#f0fdf4; border:1px solid #22c55e; padding:10px 12px; border-radius:10px;">
<strong>✅ Résultat attendu</strong>
<ul>
  <li>Mosquitto écoute sur les ports 1883 (local) et 9001 (WebSocket)</li>
  <li>Cloudflare Tunnel expose le port 9001 en WSS sur <code>mqtt.votredomaine.ca:443</code></li>
  <li>Les appareils peuvent se connecter depuis n'importe où via WSS</li>
</ul>
</div>

### 2.9 Test avec le client web HiveMQ

HiveMQ fournit un client MQTT WebSocket en ligne qui permet de tester votre broker directement depuis un navigateur, sans installer de logiciel supplémentaire.

**Accéder au client :**
1. Ouvrez votre navigateur et allez sur : https://www.hivemq.com/demos/websocket-client/

**Configurer la connexion :**

| Paramètre | Valeur |
|-----------|--------|
| **Host** | `mqtt.votredomaine.ca` |
| **Port** | `443` |
| **ClientID** | `hivemq-test-client` (ou un nom unique) |
| **Username** | `esp_user` |
| **Password** | Votre mot de passe Mosquitto |
| **Keep Alive** | `60` |
| **SSL** | ✅ Coché (obligatoire pour WSS) |

**Étapes de test :**

1. **Connexion :**
   - Remplissez les paramètres ci-dessus
   - Cliquez sur **Connect**
   - Le statut devrait passer à "Connected" (vert)

2. **S'abonner aux topics :**
   - Dans la section **Subscriptions**, cliquez sur **Add New Topic Subscription**
   - Entrez `#` pour recevoir tous les messages (ou un topic spécifique comme `esp32-123456/#`)
   - Cliquez sur **Subscribe**

3. **Publier un message de test :**
   - Dans la section **Publish**, entrez :
     - **Topic** : `test/hello`
     - **Message** : `Hello from HiveMQ!`
   - Cliquez sur **Publish**
   - Le message devrait apparaître dans la section **Messages**

4. **Tester le contrôle des LEDs :**
   - **Topic** : `esp32-XXXXXX/led/1/set` (remplacez par votre Device ID)
   - **Message** : `ON` ou `OFF`
   - Cliquez sur **Publish**
   - Si votre ESP32 est connecté, la LED devrait réagir

<div style="background:#dbeafe; border:1px solid #3b82f6; padding:10px 12px; border-radius:10px;">
<strong>💡 Astuce de débogage</strong>
<p>Le client HiveMQ est très utile pour :</p>
<ul>
  <li>Vérifier que votre broker est accessible depuis Internet</li>
  <li>Tester les topics avant de configurer l'ESP32</li>
  <li>Déboguer les problèmes de connexion MQTT</li>
  <li>Envoyer des commandes manuelles aux appareils</li>
</ul>
</div>

<div style="background:#fef3c7; border:1px solid #f59e0b; padding:10px 12px; border-radius:10px;">
<strong>⚠️ Erreurs courantes</strong>
<ul>
  <li><strong>"Connection failed" :</strong> Vérifiez que SSL est coché et que le port est 443</li>
  <li><strong>"Not authorized" :</strong> Vérifiez le nom d'utilisateur et mot de passe</li>
  <li><strong>"Connection refused" :</strong> Vérifiez que Cloudflare Tunnel est actif et que Mosquitto fonctionne</li>
</ul>
</div>

<div style="height: 5px; background: linear-gradient(90deg, #f59e0b, #f97316); border-radius: 999px; margin: 22px 0;"></div>

## 3. Communication MQTT via WiFi

> 📡 **Objectif :** contrôler les LEDs via MQTT en utilisant le WiFi.

### 3.1 Configuration (auth.h)

Le code WiFi se trouve dans `labo2/code/lilygo_wifi_mschapv2/`.

**Créer le fichier de configuration :**
```bash
cd ~/243-4J5-LI/labo2/code/lilygo_wifi_mschapv2
cp auth.h.example auth.h
nano auth.h
```

**Configuration pour WiFi WPA2-Personal (réseau domestique) :**
```cpp
// Définir le type de sécurité WiFi
#define WIFI_SECURITY_WPA2_PERSONAL

// Configuration WiFi
const char* WIFI_SSID = "VotreReseauWiFi";
const char* WIFI_PASSWORD = "VotreMotDePasse";

// Configuration MQTT (votre broker via Cloudflare)
const char* MQTT_BROKER = "mqtt.votredomaine.ca";
const char* MQTT_USER = "esp_user";
const char* MQTT_PASS = "VOTRE_MOT_DE_PASSE";

// Identifiant unique de l'appareil - À PERSONNALISER
// Format recommandé: "esp32-XXXXXX" ou un nom significatif
// Cet identifiant sera utilisé comme préfixe pour tous les topics MQTT
const char* MQTT_CLIENT_ID = "esp32-XXXXXX";
```

**Configuration pour WiFi WPA2-Enterprise (réseau du Cégep) :**
```cpp
#define WIFI_SECURITY_WPA2_ENTERPRISE

const char* WIFI_SSID = "NomReseauCegep";
const char* EAP_IDENTITY = "votre_identifiant";
const char* EAP_USERNAME = "votre_identifiant";
const char* EAP_PASSWORD = "votre_mot_de_passe";

// ... reste de la config MQTT
```

### 3.2 Compilation et téléversement

```bash
cd ~/243-4J5-LI/labo2/code/lilygo_wifi_mschapv2
arduino-cli compile --fqbn esp32:esp32:esp32 lilygo_wifi_mschapv2.ino
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 lilygo_wifi_mschapv2.ino
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```

### 3.3 Fonctionnement

Au démarrage, le moniteur série affiche :
```
=== LilyGo WiFi - MQTT via WebSocket SSL ===

Connexion WiFi a VotreReseauWiFi
Using WPA2-Personal connection.
.....
WiFi connecte!
Adresse IP: 192.168.1.xxx

[MQTT] Device ID: esp32-XXXXXX
[SSL] Configuration du client SSL...
[WSS] Connexion SSL...
[WSS] SSL connecte, envoi handshake WebSocket...
[WSS] Handshake WebSocket reussi!
[MQTT] Connexion au broker...
[MQTT] Connecte!
[MQTT] Souscriptions envoyees

=== Systeme pret ===
```

**Notez le Device ID** (ex: `esp32-123456`) - vous en aurez besoin pour l'interface Python.

### 3.4 Topics MQTT

Le code s'abonne automatiquement aux topics de commande :
- `{device_id}/led/1/set` → Recevoir "ON" ou "OFF" pour LED rouge
- `{device_id}/led/2/set` → Recevoir "ON" ou "OFF" pour LED verte

Et publie l'état des boutons :
- `{device_id}/button/1/state` → Envoie "PRESSED" ou "RELEASED"
- `{device_id}/button/2/state` → Envoie "PRESSED" ou "RELEASED"

<div style="height: 5px; background: linear-gradient(90deg, #22d3ee, #3b82f6); border-radius: 999px; margin: 22px 0;"></div>

## 4. Communication MQTT via LTE

> 🌍 **Objectif :** contrôler les LEDs via MQTT en utilisant le réseau cellulaire.

### 4.1 Configuration (auth.h)

Le code LTE se trouve dans `labo2/code/lilygo_lte_mqtt/`.

**Créer le fichier de configuration :**
```bash
cd ~/243-4J5-LI/labo2/code/lilygo_lte_mqtt
cp auth.h.example auth.h
nano auth.h
```

**Configuration APN selon votre opérateur :**
```cpp
// Configuration APN (Access Point Name)
const char APN[] = "internet.com";  // Voir tableau ci-dessous
const char APN_USER[] = "";         // Généralement vide au Canada
const char APN_PASS[] = "";         // Généralement vide au Canada

// Configuration MQTT (votre broker via Cloudflare)
const char MQTT_BROKER[] = "mqtt.votredomaine.ca";
const char MQTT_USER[] = "esp_user";
const char MQTT_PASS[] = "VOTRE_MOT_DE_PASSE";

// Identifiant unique de l'appareil - À PERSONNALISER
// Format recommandé: "lte-XXXXXX" ou un nom significatif
// Cet identifiant sera utilisé comme préfixe pour tous les topics MQTT
const char MQTT_CLIENT_ID[] = "lte-XXXXXX";
```

**APNs par opérateur au Canada :**

| Opérateur | APN |
|-----------|-----|
| Rogers | `internet.com` ou `ltemobile.apn` |
| Bell | `inet.bell.ca` ou `pda.bell.ca` |
| Telus | `sp.telus.com` ou `isp.telus.com` |
| Fido | `internet.fido.ca` |
| Koodo | `sp.koodo.com` |
| Virgin | `media.bell.ca` |
| Videotron | `media.videotron` |

### 4.2 Bibliothèques requises

Installez les bibliothèques nécessaires :
```bash
arduino-cli lib install "TinyGSM"
arduino-cli lib install "PubSubClient"
```

### 4.3 Compilation et téléversement

```bash
cd ~/243-4J5-LI/labo2/code/lilygo_lte_mqtt
arduino-cli compile --fqbn esp32:esp32:esp32 lilygo_lte_mqtt.ino
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 lilygo_lte_mqtt.ino
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```

### 4.4 Séquence de démarrage

```
=== LilyGo T-SIM A7670G - MQTT via LTE + WebSocket SSL ===

[MODEM] Allumage du modem...
[MODEM] Modem allume
[MODEM] Initialisation...
[MODEM] Info: SIMCOM_A7670G
[MODEM] IMEI: 123456789012345
[MQTT] Device ID: lte-012345

[NETWORK] Configuration de l'APN...
[NETWORK] APN configure
[NETWORK] Connexion au reseau cellulaire...
[NETWORK] Operateur: Rogers
[NETWORK] Signal: -67 dBm

[GPRS] Connexion GPRS...
[GPRS] IP: 10.123.45.67
[GPRS] Connecte

[SSL] Configuration du client SSL...
[WSS] Connexion SSL...
[WSS] Handshake WebSocket reussi!
[MQTT] Connexion au broker...
[MQTT] Connecte!

=== Systeme pret ===
```

<div style="background:#fef9c3; border:1px solid #facc15; padding:10px 12px; border-radius:10px;">
<strong>⏱️ Temps de démarrage</strong>
<p>La connexion LTE prend plus de temps que le WiFi (~30-60 secondes) car le modem doit:</p>
<ul>
  <li>S'initialiser (~3 secondes)</li>
  <li>Rechercher le réseau cellulaire (jusqu'à 60 secondes)</li>
  <li>Établir la connexion GPRS/LTE</li>
  <li>Se connecter au broker MQTT</li>
</ul>
</div>

### 4.5 Différences WiFi vs LTE

| Caractéristique | WiFi | LTE |
|----------------|------|-----|
| **Device ID** | `esp32-` + MAC | `lte-` + IMEI |
| **Temps démarrage** | ~5 secondes | ~30-60 secondes |
| **Mobilité** | Limitée au réseau WiFi | Couverture cellulaire |
| **Consommation** | Faible | Moyenne à élevée |
| **Coût** | Gratuit (WiFi existant) | Forfait de données |

<div style="height: 5px; background: linear-gradient(90deg, #c084fc, #22d3ee); border-radius: 999px; margin: 22px 0;"></div>

## 5. Interface tactile Python

> 📱 **Objectif :** contrôler les LEDs depuis l'écran tactile du Raspberry Pi via MQTT.

### 5.1 Installation des dépendances

```bash
sudo apt update
sudo apt install -y python3-paho-mqtt python3-evdev
```

### 5.2 Configuration

Le code Python se trouve dans `labo2/led-control/`.

**Créer le fichier de configuration :**
```bash
cd ~/243-4J5-LI/labo2/led-control
cp mqtt_config.py.example mqtt_config.py
nano mqtt_config.py
```

**Configuration :**
```python
MQTT_CONFIG = {
    # Broker MQTT (votre broker via Cloudflare)
    "broker": "mqtt.votredomaine.ca",
    "port": 443,  # Port WSS (WebSocket Secure)

    # Identifiants Mosquitto
    "username": "esp_user",
    "password": "VOTRE_MOT_DE_PASSE",

    # Device ID de votre ESP32/LTE
    # WiFi: "esp32-XXXXXX" (affiché au démarrage)
    # LTE: "lte-XXXXXX" (affiché au démarrage)
    "device_id": "esp32-123456",
}
```

### 5.3 Lancement de l'interface

**Depuis SSH (pour tests) :**
```bash
cd ~/243-4J5-LI/labo2/led-control
sudo python3 touch_ui_mqtt.py
```

**Sur l'écran tactile local :**
```bash
cd ~/243-4J5-LI/labo2/led-control
./launch_on_screen.sh
```

Ou manuellement :
```bash
sudo chvt 1
sudo setsid sh -c 'exec </dev/tty1 >/dev/tty1 2>&1 python3 /home/$USER/243-4J5-LI/labo2/led-control/touch_ui_mqtt.py'
```

### 5.4 Utilisation de l'interface

L'interface affiche :
- **LED ROUGE** : Toggle ON/OFF pour la LED 1 (GPIO 32)
- **LED VERTE** : Toggle ON/OFF pour la LED 2 (GPIO 33)
- **QUITTER** : Ferme l'application

**Indicateurs :**
- **MQTT CONNECTÉ** (vert) : Connexion établie
- **MQTT DÉCONNECTÉ** (rouge) : Pas de connexion
- **Zone feedback** : Affiche les messages MQTT envoyés/reçus

**Raccourci clavier :** Appuyez sur `q` pour quitter.

<div style="height: 5px; background: linear-gradient(90deg, #10b981, #06b6d4); border-radius: 999px; margin: 22px 0;"></div>

## 6. Exercice : Boutons physiques

> 🔘 **Objectif :** ajouter deux boutons physiques qui toggle les LEDs localement ET envoient l'état par MQTT.

### 6.1 Comportement attendu

Les boutons physiques sur le LilyGO doivent avoir le comportement suivant :

1. **Bouton 1 (GPIO 34)** :
   - Appui → Toggle la LED rouge (GPIO 32)
   - Publie l'état de la LED sur `{device_id}/led/1/state` ("ON" ou "OFF")

2. **Bouton 2 (GPIO 35)** :
   - Appui → Toggle la LED verte (GPIO 33)
   - Publie l'état de la LED sur `{device_id}/led/2/state` ("ON" ou "OFF")

### 6.2 Ce que vous devez faire

<div style="background:#dbeafe; border:1px solid #3b82f6; padding:10px 12px; border-radius:10px;">
<strong>📝 Tâches</strong>
<ol>
  <li><strong>Brancher les boutons</strong> sur GPIO 34 et 35 (entre GPIO et GND)</li>
  <li><strong>Modifier le code Arduino</strong> (WiFi ou LTE) pour :
    <ul>
      <li>Détecter l'appui sur les boutons (avec debounce)</li>
      <li>Toggle l'état de la LED correspondante</li>
      <li>Publier le nouvel état sur MQTT</li>
    </ul>
  </li>
  <li><strong>Tester</strong> que l'appui sur un bouton physique :
    <ul>
      <li>Allume/éteint la LED localement</li>
      <li>Envoie un message MQTT visible dans l'interface Python</li>
    </ul>
  </li>
</ol>
</div>

### 6.3 Indices pour l'implémentation

**Détection du bouton avec debounce :**
```cpp
// Variables globales
bool led1State = false;
bool led2State = false;
unsigned long lastButton1Press = 0;
unsigned long lastButton2Press = 0;
const unsigned long DEBOUNCE_DELAY = 200;  // 200ms

// Dans la fonction checkButtons() ou loop()
void checkButtonsForToggle() {
    unsigned long now = millis();

    // Bouton 1 - Toggle LED 1
    if (digitalRead(BUTTON1_PIN) == LOW) {  // Bouton pressé (pull-up)
        if (now - lastButton1Press > DEBOUNCE_DELAY) {
            lastButton1Press = now;
            led1State = !led1State;  // Toggle
            digitalWrite(LED1_PIN, led1State ? HIGH : LOW);

            // Publier l'état sur MQTT
            const char* state = led1State ? "ON" : "OFF";
            mqttClient.publish(LED1_STATE_TOPIC, state);
            Serial.print("[BTN1] LED1 toggled: ");
            Serial.println(state);
        }
    }

    // Bouton 2 - Toggle LED 2 (même logique)
    // ...
}
```

**Nouveaux topics à définir :**
```cpp
char LED1_STATE_TOPIC[50];  // {device_id}/led/1/state
char LED2_STATE_TOPIC[50];  // {device_id}/led/2/state

// Dans setup() après avoir défini MQTT_CLIENT_ID :
snprintf(LED1_STATE_TOPIC, sizeof(LED1_STATE_TOPIC), "%s/led/1/state", MQTT_CLIENT_ID);
snprintf(LED2_STATE_TOPIC, sizeof(LED2_STATE_TOPIC), "%s/led/2/state", MQTT_CLIENT_ID);
```

### 6.4 Validation

<div style="background:#f0fdf4; border:1px solid #22c55e; padding:10px 12px; border-radius:10px;">
<strong>✅ À vérifier :</strong>
<ul>
  <li>Appuyer sur BTN1 → LED rouge toggle + message MQTT envoyé</li>
  <li>Appuyer sur BTN2 → LED verte toggle + message MQTT envoyé</li>
  <li>L'état est visible dans l'interface Python (zone feedback)</li>
  <li>Pas de "rebond" (un seul toggle par appui)</li>
</ul>
</div>

<div style="height: 5px; background: linear-gradient(90deg, #a855f7, #ec4899); border-radius: 999px; margin: 22px 0;"></div>

## 7. Exercice : Montage complet

> 🎯 **Objectif :** assembler et tester le système complet avec communication bidirectionnelle.

### Étapes

1. **Monter le circuit** sur la plaquette de prototypage selon le schéma de branchement
   - 2 LEDs avec résistances sur GPIO 32 et 33
   - 2 boutons entre GPIO 34/35 et GND

2. **Choisir le mode de communication** :
   - **WiFi** : Si vous avez accès à un réseau WiFi
   - **LTE** : Si vous avez une carte SIM avec données

3. **Configurer et téléverser le code Arduino** avec les modifications de l'exercice 6

4. **Noter le Device ID** affiché dans le moniteur série

5. **Configurer l'interface Python** avec le bon Device ID

6. **Tester la communication bidirectionnelle** :
   - Interface tactile → LED : Appuyer sur les boutons toggle → Les LEDs s'allument/éteignent
   - Boutons physiques → Interface : Appuyer sur BTN1/BTN2 → L'état s'affiche dans l'interface

### Validation finale

<div style="background:#f0fdf4; border:1px solid #22c55e; padding:10px 12px; border-radius:10px;">
<strong>✅ Critères de réussite :</strong>
<ul>
  <li>Les LEDs répondent aux commandes de l'interface tactile</li>
  <li>Les boutons physiques toggle les LEDs ET envoient l'état par MQTT</li>
  <li>L'interface Python affiche les messages reçus des boutons</li>
  <li>La connexion MQTT est stable (indicateur vert)</li>
  <li>Le système fonctionne via WiFi OU LTE</li>
</ul>
</div>

### Dépannage

<div style="background:#fef3c7; border:1px solid #f59e0b; padding:10px 12px; border-radius:10px;">
<strong>⚡ Problèmes courants</strong>
<ul>
  <li><strong>LEDs ne s'allument pas :</strong> Vérifier le sens des LEDs et les résistances</li>
  <li><strong>MQTT déconnecté :</strong> Vérifier le Device ID et les identifiants</li>
  <li><strong>Boutons ne répondent pas :</strong> Vérifier les connexions GPIO 34/35 vers GND</li>
  <li><strong>Rebond des boutons :</strong> Augmenter DEBOUNCE_DELAY (ex: 300ms)</li>
  <li><strong>LTE ne se connecte pas :</strong> Vérifier l'APN et la carte SIM</li>
  <li><strong>WSS ne fonctionne pas :</strong> Vérifier la config Cloudflare et Mosquitto</li>
</ul>
</div>

<div style="height: 5px; background: linear-gradient(90deg, #22d3ee, #a855f7); border-radius: 999px; margin: 22px 0;"></div>

## 📚 Commandes de vérification utiles

```bash
# Vérifier que Mosquitto écoute sur les bons ports
sudo ss -tlnp | grep mosquitto

# Tester la connexion locale MQTT
mosquitto_sub -h localhost -p 1883 -u esp_user -P VOTRE_MOT_DE_PASSE -t "#" -v

# Envoyer une commande manuellement
mosquitto_pub -h localhost -p 1883 -u esp_user -P VOTRE_MOT_DE_PASSE \
  -t "esp32-123456/led/1/set" -m "ON"

# Vérifier les logs Mosquitto
sudo tail -f /var/log/mosquitto/mosquitto.log

# Lister les ports série disponibles
arduino-cli board list

# Moniteur série
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```

<div style="height: 5px; background: linear-gradient(90deg, #a855f7, #ec4899); border-radius: 999px; margin: 22px 0;"></div>

## 📂 Structure des fichiers

```
labo2/
├── Labo2-communication-sans-fil-MQTT-LTE.md  # Cet énoncé
├── code/
│   ├── diagnostic_modem/
│   │   └── diagnostic_avance.ino     # Diagnostic modem (optionnel)
│   ├── lilygo_wifi_mschapv2/
│   │   ├── lilygo_wifi_mschapv2.ino  # Code WiFi MQTT
│   │   └── auth.h.example            # Template configuration
│   └── lilygo_lte_mqtt/
│       ├── lilygo_lte_mqtt.ino       # Code LTE MQTT
│       ├── auth.h.example            # Template configuration
│       └── trust_anchors.h           # Certificats SSL
└── led-control/
    ├── touch_ui_mqtt.py              # Interface tactile Python
    ├── mqtt_config.py.example        # Template configuration
    ├── launch_on_screen.sh           # Script de lancement
    └── requirements.txt              # Dépendances Python
```
