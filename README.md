# 🔥 ESPBasto — Contrôleur Thermostat Webasto

Contrôleur tactile ESP32 pour chauffage Webasto via ESP-NOW, conçu pour le module **ESP32-2432S028R** (CheapYellowDisplay).

> **Projet associé :** [ESPBastoRelay](https://github.com/misterniark/ESPBastoRelay) — Module relais distant qui reçoit les commandes et pilote physiquement le Webasto.

---

## 📐 Architecture

```
┌──────────────────────────┐        ESP-NOW         ┌─────────────────────┐
│ ESP32-2432S028R           │ ◄────────────────────► │  ESPBastoRelay      │
│     (Ce projet)           │       (~200 m)         │  (Module relais)    │
├──────────────────────────┤                        ├─────────────────────┤
│ • Écran ILI9341 2,8"      │                        │ • Relais 24V        │
│ • Tactile XPT2046         │                        │ • Commande Webasto  │
│ • Capteur AHT21 (I2C)     │                        │                     │
│ • Interface rétro-futur.  │                        │                     │
└──────────────────────────┘                        └─────────────────────┘
```

Le contrôleur gère l'interface utilisateur, la lecture de la température et les décisions thermostat. Il envoie des commandes `HEAT_ON` / `HEAT_OFF` / `PING` au module relais via ESP-NOW chiffré.

---

## 🎛️ Modes de fonctionnement

| Mode | Description |
|------|-------------|
| **A — Thermostat Hystérésis** | Régulation automatique avec consigne (10–35°C) et hystérésis (1–5°C) |
| **B — Minuteur** | Chauffage ON pendant une durée configurable (1–120 min) |
| **C — Consigne Simple** | Chauffage jusqu'à atteindre la température cible, puis arrêt définitif |

---

## ✨ Fonctionnalités

- **Interface tactile rétro-futuriste** — Grosses cibles tactiles (≥70×40 px), ambiance cockpit embarqué
- **Affichage temps réel** — Température, humidité, état chauffage, liaison relais, verrou
- **3 modes de chauffage** avec sauvegarde des paramètres en NVS
- **Communication ESP-NOW** chiffrée (PMK/LMK) avec retry + timeout
- **Gestion des alertes** — Perte de connexion relais, erreur capteur AHT21
- **Économie d'énergie** — CPU 80 MHz, veille écran 60s, WiFi modem sleep
- **Verrouillage distant** — Le module relais peut verrouiller l'interface
- **Bouton BOOT** comme secours pour réveiller l'écran et acquitter les alertes
- **Sécurité capteur** — Arrêt automatique du chauffage après 5 min sans données capteur

---

## 🔌 Matériel requis

| Composant | Rôle |
|-----------|------|
| **ESP32-2432S028R** (CYD) | Carte contrôleur avec écran + tactile |
| **AHT21** | Capteur température / humidité (I2C sur CN1) |

### Brochage capteur AHT21

| Signal | GPIO | Connecteur |
|--------|------|------------|
| SDA | **GPIO27** | CN1 |
| SCL | **GPIO22** | CN1 |
| VCC | 3V3 | CN1 |
| GND | GND | CN1 |

---

## 🚀 Installation

### Prérequis

- [PlatformIO](https://platformio.org/) (CLI ou extension VS Code)
- Câble USB-C pour le module CYD

### Build & Flash

```bash
# Cloner le projet
git clone https://github.com/misterniark/ESPBastoCheapYellow.git
cd ESPBastoCheapYellow

# Compiler
pio run

# Flasher
pio run -t upload

# Moniteur série
pio device monitor -b 115200
```

### Configuration

Avant le déploiement final, modifier dans `src/config.h` :

```c
// Remplacer par la MAC réelle du module relais
const uint8_t RELAY_MAC[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

// Personnaliser les clés de chiffrement (16 caractères)
const char ESPNOW_PMK[17] = "VotrePMKPerso000";
const char ESPNOW_LMK[17] = "VotreLMKPerso000";
```

> ⚠️ Le chiffrement ESP-NOW ne fonctionne qu'avec une adresse MAC unicast. Tant que `RELAY_MAC` est en broadcast (`FF:FF:FF:FF:FF:FF`), le chiffrement est automatiquement désactivé.

---

## 📁 Structure du projet

```
src/
├── config.h          # Pins, constantes, enums, état global
├── main.cpp          # Setup, loop, logique thermostat/minuteur
├── display_ui.cpp    # Rendu graphique TFT ILI9341
├── touch_ui.cpp      # Gestion tactile XPT2046
├── espnow_link.cpp   # Communication ESP-NOW + retry/timeout
└── sensors.cpp       # Lecture AHT21 avec lissage EMA
```

---

## 📡 Protocole ESP-NOW

### Commandes (Contrôleur → Relais)

| Commande | Code | Description |
|----------|------|-------------|
| `CMD_HEAT_ON` | 1 | Allumer le chauffage |
| `CMD_HEAT_OFF` | 2 | Éteindre le chauffage |
| `CMD_PING` | 3 | Vérifier la connexion |

### Réponses (Relais → Contrôleur)

| Réponse | Code | Description |
|---------|------|-------------|
| `ACK_ON` | 11 | Chauffage allumé confirmé |
| `ACK_OFF` | 12 | Chauffage éteint confirmé |
| `ACK_PONG` | 13 | Relais connecté |
| `ACK_LOCKED` | 14 | Interface verrouillée |
| `ACK_UNLOCKED` | 15 | Interface déverrouillée |

### Mécanisme de fiabilité

- **Timeout ACK** : 1 seconde après l'envoi initial
- **Retries** : 3 tentatives espacées de 500 ms
- **Alerte connexion** : Après 3 pings consécutifs sans réponse

---

## 🔋 Économie d'énergie

| Optimisation | Détail |
|-------------|--------|
| CPU 80 MHz | Divisé par 3 vs défaut (240 MHz) |
| Veille écran | Rétroéclairage OFF + ILI9341 SLPIN après 60s |
| WiFi modem sleep | `WIFI_PS_MIN_MODEM` activé |
| LED RGB éteintes | Actives LOW, maintenues HIGH par défaut |

---

## 📜 Licence

Ce projet est fourni tel quel, sans garantie. Utilisation à vos propres risques.

---

## 🔗 Liens

- **Module relais** : [ESPBastoRelay](https://github.com/misterniark/ESPBastoRelay)
- **Spécifications complètes** : [`specs.md`](specs.md)
