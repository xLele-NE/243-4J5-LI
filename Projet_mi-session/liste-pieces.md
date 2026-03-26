# Labo 2 — Liste préliminaire de pièces électroniques

## Matériel principal
- Module **LilyGO A7670G (ESP32 + modem LTE + GPS)** pour la communication WiFi, LTE et le traitement embarqué.
- **Carte SIM activée** insérée dans le module pour l'accès LTE (PIN désactivé ou connu).
- **Antennes LTE et GPS** à visser sur les connecteurs dédiés pour la connectivité cellulaire et la géolocalisation.

## Infrastructure et programmation
- **Raspberry Pi 5** hébergeant le broker **Mosquitto** pour les échanges MQTT et l'accès SSH distant.
- **Câble USB** pour flasher et alimenter le LilyGO via Arduino CLI.

## Prototype sur breadboard (devoir de préparation)
- Plaquette de prototypage (**breadboard 830 points**).
- **4 LEDs 5 mm** (rouge, verte, bleue, jaune).
- **4 résistances** de **220 Ω ou 330 Ω** pour les LEDs.
- **3 boutons poussoirs** (tactile switch) avec **3 résistances 10 kΩ** (pull-up/pull-down).
- **Accéléromètre** MPU6050 ou ADXL345 (module breakout).
- **Buzzer actif** 3.3 V ou 5 V.
- **Module amplificateur audio** I2S (MAX98357A ou PAM8403) et **petit haut-parleur 3 W** (4 Ω ou 8 Ω).
- **Microphone MEMS** INMP441 ou MAX4466 (optionnel).
- **Fils jumper** mâle-mâle pour les connexions.
- **Condensateurs de découplage 0.1 µF** (optionnel, recommandé sur l'alim breadboard).
