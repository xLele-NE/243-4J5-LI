# Projet de Mi-Session - IoT Simon Game

## LilyGo T-SIM A7670G + Raspberry Pi Touchscreen

Projet IoT complet intégrant un jeu Simon interactif, un contrôle de LEDs, un accéléromètre, un potentiomètre et une interface tactile, le tout communiquant via MQTT over LTE (Cellulaire).

---

## Table des matières

- [Schéma du circuit](#schéma-du-circuit)
- [Liste des composants (BOM)](#liste-des-composants-bom)
- [Configuration matérielle (Pins)](#configuration-matérielle-pins)
- [Topics MQTT documentés](#topics-mqtt-documentés)
- [Compilation et téléversement (ESP32)](#compilation-et-téléversement-esp32)
- [Lancement de l'interface (Raspberry Pi)](#lancement-de-linterface-raspberry-pi)
- [Utilisation](#utilisation)
- [Fonctionnalités](#fonctionnalités)

---

## Schéma du circuit

### ESP32 (LilyGo T-SIM A7670G)

```
                         ┌─────────────────────────┐
                         │   LilyGo T-SIM A7670G   │
                         │       ESP32             │
                         │                         │
    LED Rouge ◄─────────┤ GPIO 25                 │
    LED Verte ◄─────────┤ GPIO 33                 │
    LED Bleue ◄─────────┤ GPIO 12                 │
                         │                         │
    Bouton 1  ─────────►┤ GPIO 36                 │
    Bouton 2  ─────────►┤ GPIO 39                 │
    Bouton 3  ─────────►┤ GPIO 32                 │
                         │                         │
    Potentiomètre ──────►┤ GPIO 34 (Analogique)    │
                         │                         │
    MPU6050 SDA ────────►┤ GPIO 21                 │
    MPU6050 SCL ────────►┤ GPIO 22                 │
                         │                         │
                         │    [SIM Card Slot]      │
                         └─────────────────────────┘
```

### Breadboard - LEDs avec résistances

```
    ESP32 GPIO 25 ──┤── Résistance 220Ω ──┤── LED Rouge ──┤── GND
    ESP32 GPIO 33 ──┤── Résistance 220Ω ──┤── LED Verte ──┤── GND
    ESP32 GPIO 12 ──┤── Résistance 220Ω ──┤── LED Bleue ──┤── GND
```

### Breadboard - Boutons avec Pull-up externe

```
                    ┌──── 3.3V
                    │
              Résistance 10kΩ
                    │
    ESP32 GPIO 36 ──┤──── Bouton 1 ──┤── GND
    ESP32 GPIO 39 ──┤──── Bouton 2 ──┤── GND
    ESP32 GPIO 32 ──┤──── Bouton 3 ──┤── GND
```

### Potentiomètre

```
    3.3V ─────────┤── Patte 1 (GND pot)
                  │
    GPIO 34 ──────┤── Patte 2 (Curseur / Signal)
                  │
    GND ──────────┤── Patte 3 (VCC pot)
```

### Accéléromètre MPU6050 (STEMMA QT)

```
    ESP32 GPIO 21 ──┤── SDA (MPU6050)
    ESP32 GPIO 22 ──┤── SCL (MPU6050)
    3.3V ───────────┤── VCC (MPU6050)
    GND ────────────┤── GND (MPU6050)
```

### Raspberry Pi (Interface tactile)

```
                    ┌─────────────────────────┐
                    │   Raspberry Pi 4        │
                    │   + Écran tactile       │
                    │                         │
                    │   Python UI (curses)    │
                    │   Connexion MQTT via    │
                    │   WebSocket SSL (443)   │
                    └─────────────────────────┘
```

---

## Liste des composants (BOM)

| Composant | Quantité | Description |
|-----------|:--------:|-------------|
| LilyGo T-SIM A7670G | 1 | Microcontrôleur ESP32 avec modem LTE |
| Carte SIM avec données | 1 | Pour la connexion cellulaire |
| Raspberry Pi 4 | 1 | Ordinateur pour l'interface tactile |
| Écran tactile (7") | 1 | Affichage et interaction |
| LED Rouge | 1 | Indicateur LED 1 |
| LED Verte | 1 | Indicateur LED 2 |
| LED Bleue | 1 | Indicateur LED 3 |
| Résistance 220Ω | 3 | Limitation de courant pour les LEDs |
| Résistance 10kΩ | 3 | Pull-up externe pour les boutons |
| Bouton poussoir | 3 | Entrée utilisateur |
| Potentiomètre 10kΩ | 1 | Contrôle de la luminosité |
| MPU6050 (STEMMA QT) | 1 | Accéléromètre + Gyroscope |
| Breadboard | 1 | Montage sans soudure |
| Câbles Dupont | ~20 | Connexions |

---

## Configuration matérielle (Pins)

### LEDs (Sorties)

| LED | Couleur | GPIO ESP32 |
|-----|---------|:----------:|
| LED 1 | 🔴 Rouge | **25** |
| LED 2 | 🟢 Verte | **33** |
| LED 3 | 🔵 Bleue | **12** |

### Boutons (Entrées avec Pull-up externe)

| Bouton | GPIO ESP32 | Mode |
|--------|:----------:|------|
| Bouton 1 | **36** | INPUT (Pull-up externe) |
| Bouton 2 | **39** | INPUT (Pull-up externe) |
| Bouton 3 | **32** | INPUT (Pull-up externe) |

### Capteurs

| Composant | GPIO ESP32 | Type |
|-----------|:----------:|------|
| Potentiomètre | **34** | Analogique (ADC) |
| MPU6050 SDA | **21** | I2C (Data) |
| MPU6050 SCL | **22** | I2C (Clock) |

---

## Topics MQTT documentés

Tous les topics sont préfixés par le Device ID (`lte-xlele`).

### Topics de contrôle (Publication depuis l'interface)

| Topic | Valeurs | Description |
|-------|---------|-------------|
| `lte-xlele/led/1/set` | `ON`, `OFF` | Allumer/éteindre la LED Rouge |
| `lte-xlele/led/2/set` | `ON`, `OFF` | Allumer/éteindre la LED Verte |
| `lte-xlele/led/3/set` | `ON`, `OFF` | Allumer/éteindre la LED Bleue |
| `lte-xlele/simon/start` | `START`, `STOP` | Démarrer/arrêter le jeu Simon |

### Topics d'état (Publication depuis l'ESP32)

| Topic | Valeurs | Description |
|-------|---------|-------------|
| `lte-xlele/led/1/state` | `ON`, `OFF` | État actuel de la LED Rouge |
| `lte-xlele/led/2/state` | `ON`, `OFF` | État actuel de la LED Verte |
| `lte-xlele/led/3/state` | `ON`, `OFF` | État actuel de la LED Bleue |
| `lte-xlele/potentiometer/state` | `0` à `100` | Valeur du potentiomètre (%) |
| `lte-xlele/accelerometer/orientation` | `flat`, `vertical_left`, `vertical_right`, `horizontal_up`, `horizontal_down` | Orientation de la carte |
| `lte-xlele/simon/status` | `ECOUTEZ`, `YOUR TURN`, `BRAVO`, `PERDU`, `IDLE`, `LVL X` | État du jeu Simon |
| `lte-xlele/simon/level` | `1`, `2`, `3`... | Niveau actuel du Simon |
| `lte-xlele/simon/score` | `0`, `1`, `2`... | Score du Simon |
| `lte-xlele/button/1/state` | `ON`, `OFF` | État du Bouton 1 |
| `lte-xlele/button/2/state` | `ON`, `OFF` | État du Bouton 2 |

### Broker MQTT

| Paramètre | Valeur |
|-----------|--------|
| Broker | `mqtt.xlele.ca` |
| Port | `443` (WebSocket SSL) |
| Transport | `websockets` |
| Utilisateur | `esp_user` |
| Mot de passe | `Teladmin1$` |
| Device ID | `lte-xlele` |

---

## Compilation et téléversement (ESP32)

### Prérequis

- [Arduino CLI](https://arduino.github.io/arduino-cli/) installé
- Cœur ESP32 installé (`esp32:esp32`)
- Bibliothèques requises :
  - `TinyGsm`
  - `PubSubClient`
  - `ESP_SSLClient`
  - `Adafruit MPU6050`
  - `Adafruit BusIO`
  - `Adafruit Unified Sensor`

### Installation des bibliothèques

```bash
arduino-cli lib install "TinyGsm"
arduino-cli lib install "PubSubClient"
arduino-cli lib install "ESP_SSLClient"
arduino-cli lib install "Adafruit MPU6050"
```

### Compilation

```bash
cd labo/Labo-02/code/lilygo_lte_mqtt/
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries ~/Arduino/libraries lilygo_lte_mqtt.ino
```

### Téléversement

```bash
arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32 lilygo_lte_mqtt.ino
```

### Monitorie série (débogage)

```bash
arduino-cli monitor -p /dev/ttyACM0 -c baudrate=115200
```

---

## Lancement de l'interface (Raspberry Pi)

### Prérequis

- Python 3 installé
- Bibliothèques Python :
  - `paho-mqtt`
  - `evdev`

### Installation des dépendances

```bash
pip3 install paho-mqtt evdev
```

### Lancement

```bash
cd labo/Labo-02/led-control/
sudo python3 touch_ui_mqtt.py < /dev/tty1 > /dev/tty1 2>&1
```

### Ou via le script de lancement

```bash
sudo ./launch_on_screen.sh
```

---

## Utilisation

### Démarrage rapide

1. **Connecter le matériel** : Branchez les LEDs, boutons, potentiomètre et accéléromètre selon le schéma.
2. **Insérer la carte SIM** : Placez la carte SIM avec un forfait de données dans le LilyGo.
3. **Allumer l'ESP32** : Attendez que la LED rouge clignote, puis que le modem se connecte au réseau cellulaire.
4. **Lancer l'interface** : Démarrez l'interface Python sur le Raspberry Pi.
5. **Attendre la connexion** : L'indicateur MQTT passe au vert sur l'écran.

### Contrôler les LEDs

- **Via l'écran tactile** : Touchez les boutons LED Rouge/Verte/Bleue pour les allumer ou éteindre.
- **Via les boutons physiques** : Appuyez sur les boutons sur la breadboard pour toggle les LEDs.

### Jouer au Simon

1. Touchez **▶ LANCER SIMON** sur l'écran.
2. Les LEDs s'allument une par une dans un ordre aléatoire.
3. L'écran affiche **YOUR TURN**.
4. Répétez l'ordre avec les boutons physiques.
5. Si réussi, le niveau monte (une LED de plus).
6. Si échoué, les LEDs clignotent et le jeu recommence.
7. Touchez **✕ QUITTER SIMON** pour arrêter.

### Potentiomètre

- Tournez le potentiomètre pour ajuster la luminosité de l'écran tactile.
- La barre de progression sur l'écran montre la valeur actuelle.

### Accéléromètre

- Inclinez la carte à 90° : les boutons de l'interface se réorganisent (vertical ↔ horizontal).
- Remettez la carte à plat pour revenir au layout par défaut.

---

## Fonctionnalités

| Fonctionnalité | Description |
|----------------|-------------|
| 🔴🟢🔵 Contrôle LEDs | Toggle par boutons physiques ou interface tactile |
| 🎮 Jeu Simon | 3 LEDs, niveaux croissants, record de niveau |
| 🔊 Potentiomètre | Contrôle de la luminosité de l'écran |
| 📱 Accéléromètre | Détection d'orientation, layout adaptatif |
| 📡 Communication LTE | Modem A7670G via carte SIM |
| 🔒 MQTT SSL | Connexion sécurisée via WebSocket SSL |
| 🖥️ Interface tactile | Curses sur Raspberry Pi, fond rose |

---

## Architecture du projet

```
├── labo/Labo-02/
│   ├── code/
│   │   ├── lilygo_lte_mqtt/
│   │   │   ├── lilygo_lte_mqtt.ino    # Code ESP32 (Arduino)
│   │   │   └── auth.h                 # Identifiants MQTT
│   │   └── diagnostic_modem/          # Outils de diagnostic
│   └── led-control/
│       ├── touch_ui_mqtt.py           # Interface tactile (Python)
│       ├── mqtt_config.py             # Configuration MQTT
│       └── launch_on_screen.sh        # Script de lancement
```

---

## Auteur

**xLele-NE** - Cégep de Limoilou - Session H26
Cours 243-4J5-LI - Objets connectés
