# Projet IoT : Communication LoRa Bidirectionnelle avec Intelligence Artificielle (LLM)

## 📖 Description du Projet

Ce projet implémente un système d'objets connectés interactif en utilisant deux cartes **LilyGO T-Beam Supreme** (équipées de la puce radio SX1262). Le système démontre une communication radio LoRa bidirectionnelle couplée à une prise de décision par une Intelligence Artificielle (API Groq / Llama) via Wi-Fi.

Le cycle de fonctionnement est le suivant :

1. **L'Émetteur (TX)** lit la valeur d'un potentiomètre et l'envoie en continu via LoRa.
2. **Le Récepteur (RX)** capte ces valeurs. Sur commande (pression d'un bouton ou via le terminal), il se connecte au Wi-Fi et envoie la dernière valeur reçue à un modèle d'Intelligence Artificielle (LLM).
3. L'IA analyse la valeur (ex: *si < 1000, dire "Lumière faible"*) et retourne sa décision.
4. Le Récepteur renvoie instantanément la décision de l'IA à l'Émetteur par LoRa.
5. L'Émetteur reçoit l'ordre et agit physiquement en allumant, éteignant ou faisant clignoter une DEL.

\---

## 🛠️ Matériel Requis

* **2x** Cartes LilyGO T-Beam Supreme (ESP32-S3 + Puce LoRa SX1262)
* **1x** Potentiomètre analogique
* **1x** DEL (LED) avec résistance appropriée
* Fils de connexion (Jumper wires)

\---

## 📂 Structure des Fichiers

### 1\. `config.h` (Configuration Globale)

Ce fichier est le cœur de la configuration du système. Il est partagé (ou inclus) dans le récepteur.

* **WIFI\_SSID / WIFI\_PASSWORD** : Identifiants de votre réseau Wi-Fi.
* **EAP\_IDENTITY / EAP\_PASSWORD** : Identifiants pour le réseau WPA2 Entreprise (ex: réseau du Cégep).
* **OPENWEBUI\_URL \& API\_KEY** : Paramètres de connexion à l'API LLM (Groq dans ce cas).
* **SYSTEM\_PROMPT** : La directive donnée à l'IA pour analyser le potentiomètre.

### 2\. `llm\_t\_beam\_supreme\_TX\_finale.ino` (L'Émetteur)

Ce code doit être téléversé sur la première carte T-Beam.

* **Broches configurées** :

  * `PIN\_POT = 2` : Broche de lecture du potentiomètre.
  * `PIN\_DEL = 3` : Broche de contrôle de la DEL externe.
* **Fonctionnement** : Envoie un paquet JSON contenant la clé `"appareil": "T-Beam-1"` et la valeur du potentiomètre toutes les secondes. Il écoute ensuite pendant 1 seconde (`timeout`) pour recevoir d'éventuels ordres de l'IA pour contrôler la DEL.

### 3\. `llm\_t\_beam\_supreme\_testRx-finale.ino` (Le Récepteur)

Ce code doit être téléversé sur la deuxième carte T-Beam.

* **Broches configurées** :

  * `PIN\_BTN = 0` : Bouton physique pour déclencher l'appel à l'IA.
  * `PIN\_DEL = 2` : DEL clignotante de réception.
* **Fonctionnement** : Écoute les signaux LoRa et filtre uniquement les messages provenant de `"T-Beam-1"`. Lors d'un déclenchement, il extrait la réponse du LLM (avec une méthode robuste d'extraction manuelle du JSON pour éviter les erreurs de mémoire) et la retransmet en format JSON (`{"ia": "réponse"}`).

\---

## 🚀 Instructions de Déploiement et de Test

### Étape 1 : Préparation de la configuration

1. Ouvrez le fichier `config.h`.
2. Assurez-vous que les informations Wi-Fi correspondent à votre réseau (modifiez `USE\_WPA2\_ENTERPRISE` selon vos besoins).
3. Vérifiez la clé API (`API\_KEY`) pour l'accès au modèle Llama.

### Étape 2 : Déploiement

1. Connectez la première carte (Émetteur). Dans l'IDE Arduino, téléversez le fichier `llm\_t\_beam\_supreme\_TX\_finale.ino`.
2. Connectez la deuxième carte (Récepteur). Dans l'IDE Arduino, assurez-vous que `config.h` est dans le même dossier de projet et téléversez le fichier `llm\_t\_beam\_supreme\_testRx-finale.ino`.

### Étape 3 : Le Test Final

1. Laissez le **Récepteur** branché à l'ordinateur et ouvrez le **Moniteur Série** (baud rate : 115200).
2. Alimentez l'**Émetteur** (batterie ou autre port USB).
3. Tournez le potentiomètre de l'Émetteur à une extrémité.
4. Dans le terminal du Récepteur, vous devriez voir les valeurs LoRa arriver (`\[LORA] Signal recu de T-Beam-1...`).
5. **Déclenchez l'IA** en appuyant sur le bouton physique du Récepteur (PIN 0) OU en tapant la lettre `a` suivie de *Entrée* dans la barre de saisie du Moniteur Série.
6. Observez la magie :

   * Le terminal affiche la réponse de l'IA (ex: "Lumière faible.").
   * Le Récepteur envoie la commande par LoRa.
   * La DEL de l'Émetteur s'allume, s'éteint ou clignote en fonction de la consigne !

