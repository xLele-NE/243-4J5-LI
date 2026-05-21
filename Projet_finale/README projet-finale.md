# Système de Télémétrie — Hydro-Limoilou

## Projet de Surveillance Environnementale et Opérationnelle

### Site Spécifique : Poste de vanne (Barrage au fil de l'eau)

\---

## 1\. Description du site et de la mise en situation

### Context général (Hydro-Limoilou)

Ce projet s'inscrit dans le cadre du déploiement de l'infrastructure de télémétrie d'un distributeur d'énergie fictif nommé **Hydro-Limoilou**. Le réseau complet simule la supervision de 8 sites de radiocommunication critiques (abritant chacun un répéteur RF configuré pour assurer les liaisons SCADA et la résilience du réseau).

La convergence des données vers une machine virtuelle (VM) centrale simule un **Centre de conduite du réseau (CCR)**, inspiré des systèmes de supervision industrielle en temps réel (ex. Hydro-Québec). Ce CCR offre une vue agrégée et centralisée des 8 sites distants pour permettre aux répartiteurs d'agir rapidement en cas d'anomalie.

### Spécificités de ma tâche : Poste de commande de vanne

Le site sous ma responsabilité est un **poste de commande de vanne en crête de barrage (barrage au fil de l'eau)**. Ce site stratégique abrite un répéteur RF essentiel pour la liaison SCADA côtière.

L'environnement de ce site présente des risques opérationnels précis qui nécessitent une surveillance stricte :

1. **Risque de crue / Inondation :** Une augmentation critique du niveau d'eau en amont menace directement la structure et la disponibilité des équipements de communication.
2. **Sécurité physique / Intrusion :** Le poste étant isolé, toute entrée non autorisée dans le local technique met en péril l'intégrité du répéteur.

### Matériel et Shield de Capteurs Utilisés

Pour répondre à cette mise en situation, l'architecture locale s'appuie sur un microcontrôleur **T-Beam SUPREME** équipé d'un shield de capteurs configuré comme suit :

* **BH1750 (Capteur de luminosité) :** Mesure de la lumière intérieure permettant de détecter l'ouverture de la porte du local ou une panne d'éclairage.
* **EKMC (Capteur PIR de mouvement) :** Détection d'une intrusion physique à l'intérieur du local technique.
* **Potentiomètre :** Simulation analogique du niveau d'eau en amont du barrage (0 à 100 %).
* **Bouton-poussoir :** Bouton physique d'acquittement de l'alarme locale (*ACK*).
* **DEL (Indicateur visuel) :** Signal d'alarme locale (activation lors d'une crue ou d'une intrusion).

\---

## 2\. Liste des topics utilisés (Conformité contrat VM)

Afin de permettre l'agrégation transparente par le serveur central (VM de l'enseignant) qui gère 8 connexions MQTT parallèles, la nomenclature des topics respecte strictement le contrat d'interface et la hiérarchie standardisée d'Hydro-Limoilou.

Pour mon site (identifié ici par le jeton de routage `site-1`), la structure des topics est configurée sous la racine `hydro-limoilou/site-1/telemetry` :

|Nom du Topic MQTT|Type de charge utile (Payload)|Description|
|-|-|-|
|`hydro-limoilou/site-1/telemetry/lumiere`|JSON : `{"lux": float}`|Intensité lumineuse lue par le capteur BH1750 à l'intérieur du local.|
|`hydro-limoilou/site-1/telemetry/intrusion`|JSON : `{"status": int}`|`0` = Aucun mouvement, `1` = Intrusion détectée par le capteur EKMC.|
|`hydro-limoilou/site-1/telemetry/niveau\_eau`|JSON : `{"pourcentage": int}`|Niveau d'eau simulé par le potentiomètre (0 à 100 %).|
|`hydro-limoilou/site-1/telemetry/alarme`|JSON : `{"active": bool, "cause": string}`|`true` si une alarme est levée (causes : `"intrusion"`, `"crue"`, `"multiple"`), `false` sinon. Gère l'état de la DEL.|
|`hydro-limoilou/site-1/telemetry/ack`|JSON : `{"timestamp": long, "type": string}`|Notification d'acquittement de l'alarme via le bouton physique (`"local"`) ou la GUI (`"remote"`).|

\---

## 3\. Procédure de démo et résultats des 3 scénarios de test

L'architecture de communication valide le transit des trames :
`T-Beam Distant` $
ightarrow$ (LoRa P2P) $
ightarrow$ `T-Beam Gateway` $
ightarrow$ (MQTT via Cloudflare Tunnel) $
ightarrow$ `Raspberry Pi 5` $
ightarrow$ `VM Centrale / Dashboard`.

### Scénario de test 1 : Intrusion physique dans le local technique (Sécurité)

* **Objectif :** Valider la détection d'une présence non autorisée et l'envoi immédiat de l'alerte au CCR.
* **Procédure de démonstration :**

  1. Le local est initialement stable (capteur EKMC au repos, topic `intrusion` à `0`).
  2. Simuler une intrusion en passant la main devant le capteur EKMC et en masquant/démasquant le capteur BH1750 (simulation d'ouverture de porte).
* **Résultats attendus \& observés :**

  * Le capteur EKMC bascule à `1`.
  * La DEL d'alarme s'allume instantanément sur le T-Beam SUPREME distant.
  * Une trame MQTT est publiée sur `hydro-limoilou/site-1/telemetry/intrusion` avec la valeur `{"status": 1}`.
  * Le topic `alarme` publie `{"active": true, "cause": "intrusion"}`. L'alerte apparaît en rouge sur la GUI tactile du Raspberry Pi 5 et sur le Dashboard de la VM centrale.

### Scénario de test 2 : Élévation critique du niveau d'eau (Alerte de Crue)

* **Objectif :** Valider la surveillance du niveau d'eau en amont et le déclenchement du protocole de sécurité de crue.
* **Procédure de démonstration :**

  1. Le potentiomètre est initialement positionné à mi-course (niveau nominal $
  2. pprox 50%$).
  3. Tourner progressivement le potentiomètre vers la droite pour dépasser le seuil critique de sécurité fixé à **85%**.
* **Résultats attendus \& observés :**

  * Les valeurs publiées sur `hydro-limoilou/site-1/telemetry/niveau\_eau` augmentent en temps réel (ex: `{"pourcentage": 92}`).
  * Dès le franchissement du seuil de 85%, la DEL d'alarme locale s'active (mode clignotant ou fixe selon configuration).
  * Le topic `alarme` publie instantanément la charge utile : `{"active": true, "cause": "crue"}`.
  * Le tableau de bord central affiche visuellement l'état d'urgence pour le poste de vanne, alertant le répartiteur d'un risque imminent pour le répéteur RF.

### Scénario de test 3 : Processus d'acquittement de l'alarme (ACK)

* **Objectif :** Valider la prise en charge d'une alarme par l'opérateur ou le technicien terrain et l'extinction des indicateurs de crise.
* **Procédure de démonstration :**

  1. Placer le système en état d'alarme active (Scénario 1 ou 2 complété, DEL allumée).
  2. Ramener le capteur à l'état normal (éloigner la main de l'EKMC ou redescendre le potentiomètre sous 85%). L'alarme reste mémorisée.
  3. Appuyer sur le bouton-poussoir physique d'acquittement du shield (ou envoyer la commande via l'interface).
* **Résultats attendus \& observés :**

  * L'appui sur le bouton déclenche l'extinction immédiate de la DEL d'alarme locale.
  * Un message est publié sur le topic `hydro-limoilou/site-1/telemetry/ack` avec la charge utile correspondante.
  * Le topic `alarme` repasse à `{"active": false, "cause": "none"}`.
  * Sur le Dashboard central de la VM, l'indicateur d'alarme vire au vert/normal, confirmant la résolution ou la prise en charge de l'incident par l'équipe d'Hydro-Limoilou.

