# Guide de Configuration : Passerelle MQTT Sécurisée (ESP32 ↔ Cloudflare ↔ Raspberry Pi)

Ce document explique comment configurer un broker MQTT (Mosquitto) sur un Raspberry Pi, le sécuriser avec un mot de passe, l'exposer via un Tunnel Cloudflare, et connecter un ESP32 en utilisant des WebSockets sécurisés (WSS).

---

## 🏗️ Architecture

1.  **ESP32** : Client MQTT. Se connecte via SSL (Port 443) et construit les paquets MQTT manuellement.
2.  **Cloudflare Tunnel** : Reçoit le trafic HTTPS/WSS public et le redirige vers le Raspberry Pi.
3.  **Raspberry Pi (Mosquitto)** : Broker MQTT. Écoute en local sur le port 9001 (WebSockets).

---

## Étape 1 : Configuration du Raspberry Pi (Serveur)

### 1. Installation et Sécurisation de Mosquitto

Installez le broker :
```bash
sudo apt update
sudo apt install mosquitto mosquitto-clients
Créez un utilisateur et un mot de passe (remplacez esp_user par votre nom) :

Bash

sudo mosquitto_passwd -c /etc/mosquitto/passwd esp_user
# Entrez le mot de passe deux fois
Corrigez les permissions (CRUCIAL pour éviter les erreurs de lecture) :

Bash

sudo chown mosquitto:mosquitto /etc/mosquitto/passwd
sudo chmod 640 /etc/mosquitto/passwd
2. Fichier de Configuration Mosquitto
Éditez le fichier de configuration :

Bash

sudo nano /etc/mosquitto/mosquitto.conf
Ajoutez ou modifiez le contenu pour avoir ceci :

Plaintext

# Stockage des données
persistence true
persistence_location /var/lib/mosquitto/
log_dest file /var/log/mosquitto/mosquitto.log

# Sécurité
allow_anonymous false
password_file /etc/mosquitto/passwd

# Listener 1 : MQTT Classique (Local uniquement)
listener 1883
protocol mqtt

# Listener 2 : WebSockets (Pour Cloudflare)
listener 9001 0.0.0.0
protocol websockets
Redémarrez le service :

Bash

sudo systemctl restart mosquitto
Étape 2 : Configuration de Cloudflare (Tunnel)
1. Fichier config.yml
Ouvrez la configuration de votre tunnel (/etc/cloudflared/config.yml ou ~/.cloudflared/config.yml). Configurez l'ingress pour pointer vers l'IP locale (force l'IPv4) :

YAML

tunnel: <VOTRE_TUNNEL_UUID>
credentials-file: /chemin/vers/credentials.json

ingress:
  - hostname: mqtt.votre-domaine.com
    service: [http://127.0.0.1:9001](http://127.0.0.1:9001)   # ⚠️ Important : http (pas ws) et 127.0.0.1 (pas localhost)
  
  - service: http_status:404
Redémarrez le tunnel :

Bash

sudo systemctl restart cloudflared
2. Configuration Dashboard Cloudflare
Allez sur le dashboard Cloudflare > Security > Bots.

DÉSACTIVEZ le "Bot Fight Mode" (sinon l'ESP32 sera bloqué).

Étape 3 : Code ESP32 (Client C++)
L'ESP32 doit :

Se connecter en SSL (Port 443).

Spécifier le sous-protocole "mqtt".

Envoyer un paquet CONNECT contenant le Username et le Password.

Snippet : Configuration WebSocket
C++

// Dans setup()
const char* MQTT_HOST = "mqtt.votre-domaine.com";
const int MQTT_PORT = 443;

// Le 5ème argument "mqtt" est obligatoire pour Mosquitto
webSocket.beginSSL(MQTT_HOST, MQTT_PORT, "/", "", "mqtt");
Snippet : Construction du Paquet CONNECT (Authentifié)
La fonction doit mettre le flag 0xC2 (User + Pass + CleanSession) et injecter les identifiants.

C++

// Extrait de la fonction mqtt_build_connect_packet
// ...
// Flags: User(1) + Pass(1) + CleanSession(1) = 11000010 = 0xC2
uint8_t connectFlags = 0xC2; 
vh.push_back(connectFlags);
// ...
// (Ajouter ensuite ClientID, Username et Password au payload)
Étape 4 : Tests et Vérification
1. Vérifier le Pi
Voir si Mosquitto écoute bien :

Bash

sudo ss -tulpn | grep 9001
# Doit afficher "LISTEN ... 0.0.0.0:9001"
2. Tester depuis un PC distant (via Internet)
Utilisez mosquitto_pub pour vérifier que le tunnel et le mot de passe fonctionnent :

Bash

mosquitto_pub -h mqtt.votre-domaine.com -p 443 --capath /etc/ssl/certs \
  -u "esp_user" -P "votre_mot_de_passe" \
  -t "test/topic" -m "Hello Cloudflare"
Si la commande passe sans erreur, l'infrastructure est fonctionnelle ! 🚀