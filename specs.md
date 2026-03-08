# ESPBasto - Spécifications Techniques

## Vue d'ensemble

Contrôle d'un chauffage Webasto via ESP-NOW avec 3 modes de fonctionnement.

**Priorité n°1** : Faible consommation électrique ⚡

> Cette version des specs est recalée sur le matériel réellement présent dans `Matériel.rtf` : **module ESP32-2432S028R** (ESP32-WROOM-32 + écran 2,8" ILI9341 + tactile résistif XPT2046).

---

## 1. Économie d'énergie

### Mise en veille écran

| Paramètre | Valeur |
|-----------|--------|
| Timeout inactivité | **60 secondes** |
| Actions surveillées | Touch écran, bouton BOOT (secours) |

**Logique :**
```
SI aucune action pendant 60s :
  → Rétroéclairage OFF (TFT_BL = GPIO21)
  → Contrôleur ILI9341 en SLPIN

SI action détectée (écran éteint) :
  → ILI9341 SLPOUT + délai ~120 ms
  → Rétroéclairage ON
  → Première action consommée (réveil uniquement)

SI action détectée (écran allumé) :
  → Action normale sur l'interface
```

> ⚠️ Le premier toucher qui réveille l'écran est **consommé** : il ne déclenche pas l'action de l'interface.

> ℹ️ Le chauffage continue de fonctionner même si l'écran est éteint.

### Autres optimisations énergétiques

| Optimisation | Remarque |
|-------------|----------|
| CPU à 80 MHz | Suffisant pour l'UI + ESP-NOW, à valider selon la fluidité écran |
| Backlight OFF | Meilleur levier d'économie sur cette carte |
| ILI9341 en veille (`SLPIN`) | Réduit la consommation du contrôleur TFT |
| WiFi modem sleep (`WIFI_PS_MIN_MODEM`) | Recommandé hors trafic intense |
| Désactivation LED RGB par défaut | LED actives à l'état bas, à garder éteintes hors debug |

---

## 2. Architecture

```
┌──────────────────────────┐        ESP-NOW         ┌─────────────────────┐
│ ESP32-2432S028R #1       │ ◄────────────────────► │      Module #2      │
│     (Contrôleur HMI)     │       (~200 m)         │   (Relais Webasto)  │
├──────────────────────────┤                        ├─────────────────────┤
│ • ESP32-WROOM-32         │                        │ • Relais 24V        │
│ • TFT ILI9341 240x320    │                        │ • Webasto           │
│ • Tactile XPT2046        │                        │                     │
│ • RGB LED                │                        │                     │
│ • LDR                    │                        │                     │
│ • µSD                    │                        │                     │
│ • HP (GPIO26)            │                        │                     │
│ • CN1/P3 pour extensions │                        │                     │
└──────────────────────────┘                        └─────────────────────┘
```

### Extensions retenues pour le projet thermostat

Le matériel du fichier `Matériel.rtf` **ne mentionne pas** d'encodeur EC11, de bouton K0, d'AHT21 ni de MAX17048.

Pour conserver les fonctions du projet d'origine, on retient les extensions suivantes :

| Fonction projet | Matériel retenu | Interface | Statut |
|-----------------|-----------------|-----------|--------|
| Température / humidité | **AHT21 externe** | I2C sur CN1 | Recommandé |
| Navigation UI | **Écran tactile résistif** | XPT2046 | Remplace encodeur + K0 |

> ℹ️ Le connecteur « DHT11 » est annoncé par la fiche produit, mais son pin data n'est pas documenté de façon assez fiable dans les sources vérifiées. **On ne fige donc pas ce connecteur dans le code**. Pour les capteurs externes, utiliser **CN1** avec un brochage explicite défini dans cette spec.

---

## 3. Modes de fonctionnement

### Valeurs par défaut

| Paramètre | Valeur par défaut |
|-----------|-------------------|
| Setpoint (consigne) | **20°C** |
| Hystérésis | **3°C** |
| Durée minuteur | **30 min** |

---

### Démarrage et sauvegarde

**Sauvegarde des paramètres :** Oui (`Preferences` / NVS)

| Paramètre sauvegardé |
|----------------------|
| Setpoint |
| Hystérésis |
| Durée minuteur |
| Dernier mode utilisé |

**État au démarrage :**
```
Affichage : Menu principal
Chauffage : OFF (ne démarre qu'après action utilisateur)
Paramètres : Chargés depuis NVS (ou défauts si premier démarrage)
```

---

### Mode A - Thermostat avec Hystérésis

| Paramètre | Plage | Pas | Défaut |
|-----------|-------|-----|--------|
| Consigne (setpoint) | 10°C à 35°C | 0.5°C | 20°C |
| Hystérésis | 1°C à 5°C | 1°C | 3°C |

**Logique :**
```
Lecture capteur : toutes les 2 secondes (lissage EMA)
Décision thermostat : toutes les 60 secondes (valeur lissée)

SI température < (setpoint - hysteresis) → Chauffage ON
SI température >= setpoint → Chauffage OFF
SINON → Garder l'état actuel (zone morte)
```

**Exemple** (setpoint=21°C, hystérésis=3°C) :
- T < 18°C → ON
- T >= 21°C → OFF
- 18°C ≤ T < 21°C → Zone morte

---

### Mode B - Thermostat avec Minuteur

| Paramètre | Plage | Pas | Défaut |
|-----------|-------|-----|--------|
| Durée | 1 min à 120 min | 1 min | 30 min |

**Logique :**
```
Démarrage → Chauffage ON + Timer démarre
Timer > 0  → Chauffage ON
Timer = 0  → Chauffage OFF
```

> ⚠️ Pas de contrôle de température dans ce mode.

---

### Mode C - Thermostat avec Consigne

| Paramètre | Plage | Pas | Défaut |
|-----------|-------|-----|--------|
| Consigne (setpoint) | 10°C à 35°C | 0.5°C | 20°C |

**Logique :**
```
Lecture capteur : toutes les 2 secondes (lissage EMA)
Décision : toutes les 60 secondes (valeur lissée)

SI température < setpoint → Chauffage ON
SI température ≥ setpoint → Chauffage OFF (définitif)
```

> ℹ️ Une fois la consigne atteinte, le chauffage s'arrête et ne redémarre pas automatiquement.

---

## 4. Matériel - Contrôleur (#1)

### Matériel présent sur la carte ESP32-2432S028R

| Élément | Modèle / fonction | Interface |
|--------|--------------------|-----------|
| Microcontrôleur | **ESP32-WROOM-32** (double cœur) | - |
| Écran | **ILI9341** 2,8" 240x320 | SPI |
| Tactile | **XPT2046** résistif | SPI dédié |
| Rétroéclairage | TFT backlight | GPIO |
| LED RGB | Rouge / Vert / Bleu | GPIO |
| Capteur de lumière | **LDR** | ADC |
| Stockage | Lecteur **microSD / TF** | SPI |
| Audio | Sortie haut-parleur amplifiée | GPIO / PWM / DAC |
| Boutons | **BOOT** + **RST** | GPIO / reset |
| Extensions | Connecteurs **P3**, **CN1**, **P1** | GPIO / alimentation |

### Périphériques externes recommandés pour ce projet

| Périphérique | Librairie recommandée | Interface retenue | Pins retenus |
|-------------|------------------------|-------------------|--------------|
| AHT21 (temp/humidité) | `Adafruit AHTX0` | I2C | SDA=**GPIO27**, SCL=**GPIO22** |


> ℹ️ Le bus I2C externe est volontairement fixé à **CN1** avec `Wire.begin(27, 22)` pour éviter tout conflit avec le rétroéclairage sur GPIO21.

---

## 5. Bibliothèques recommandées

### Stack principale retenue

| Fonction | Librairie | Statut | Notes |
|----------|-----------|--------|-------|
| WiFi / ESP-NOW | `WiFi.h` + `ESP_NOW.h` | **Recommandé** | API officielle Arduino-ESP32 |
| Écran TFT | `TFT_eSPI` | **Recommandé** | Performant pour ILI9341 |
| Écran tactile | `XPT2046_Touchscreen` | **Recommandé** | À gérer séparément du TFT |
| NVS / paramètres | `Preferences.h` | **Recommandé** | Sauvegarde paramètres |
| I2C externe | `Wire.h` | **Recommandé** | Bus personnalisé sur CN1 |
| AHT21 externe | `Adafruit AHTX0` | **Recommandé** | Meilleur choix qu'un DHT11 pour un thermostat |
| µSD | `SD.h` | Optionnel | Logs / assets / thèmes |

### Règles d'implémentation importantes

1. **Ne pas utiliser le tactile via TFT_eSPI** sur cette carte : le XPT2046 est câblé sur un **bus SPI différent** du TFT.
2. Le TFT peut être piloté par `TFT_eSPI`.
3. Le tactile doit être piloté par `XPT2046_Touchscreen` avec son propre `SPIClass` si nécessaire.
4. Le rétroéclairage est sur **GPIO21** : ne pas réutiliser cette pin pour autre chose.
5. Les GPIO **34, 35, 36, 39** sont des entrées uniquement.

---

## 6. Câblage / GPIO - Contrôleur (#1)

### Écran TFT ILI9341 (SPI TFT)

| Signal | GPIO | Fonction |
|--------|------|----------|
| TFT_MISO | **GPIO12** | Lecture SPI TFT |
| TFT_MOSI | **GPIO13** | Données SPI TFT |
| TFT_SCLK | **GPIO14** | Horloge SPI TFT |
| TFT_CS | **GPIO15** | Chip Select TFT |
| TFT_DC | **GPIO2** | Data / Command |
| TFT_RST | **-1** | Reset non câblé séparément |
| TFT_BL | **GPIO21** | Rétroéclairage |

### Écran tactile XPT2046 (SPI tactile dédié)

| Signal | GPIO | Fonction |
|--------|------|----------|
| XPT2046_IRQ | **GPIO36** | IRQ tactile |
| XPT2046_MOSI | **GPIO32** | Données SPI tactile |
| XPT2046_MISO | **GPIO39** | Lecture SPI tactile |
| XPT2046_CLK | **GPIO25** | Horloge SPI tactile |
| XPT2046_CS | **GPIO33** | Chip Select tactile |

### Lecteur microSD / TF

| Signal | GPIO | Fonction |
|--------|------|----------|
| SD_MISO | **GPIO19** | Lecture SPI SD |
| SD_MOSI | **GPIO23** | Données SPI SD |
| SD_SCK | **GPIO18** | Horloge SPI SD |
| SD_CS | **GPIO5** | Chip Select SD |

### LED RGB (actives à l'état bas)

| Couleur | GPIO | Note |
|---------|------|------|
| Rouge | **GPIO4** | `LOW = ON` |
| Vert | **GPIO16** | `LOW = ON` |
| Bleu | **GPIO17** | `LOW = ON` |

### Capteur de lumière / LDR

| Signal | GPIO | Note |
|--------|------|------|
| LDR | **GPIO34** | ADC, entrée uniquement |

### Haut-parleur

| Signal | GPIO | Note |
|--------|------|------|
| Speaker | **GPIO26** | Connecté à l'ampli, à réserver à l'audio |

### Boutons intégrés

| Bouton | GPIO | Note |
|--------|------|------|
| BOOT | **GPIO0** | Strapping pin, à ne pas utiliser comme contrôle principal |
| RESET | - | Reset matériel |

### Connecteurs d'extension

#### Connecteur P3

| Broche | GPIO | Note |
|--------|------|------|
| GND | - | Masse |
| IO35 | **GPIO35** | Entrée uniquement, pas de pull-up interne |
| IO22 | **GPIO22** | Disponible |
| IO21 | **GPIO21** | Déjà utilisé par le backlight |

#### Connecteur CN1

| Broche | GPIO | Note |
|--------|------|------|
| GND | - | Masse |
| IO22 | **GPIO22** | Recommandé comme SCL I2C |
| IO27 | **GPIO27** | Recommandé comme SDA I2C |
| 3V3 | - | Alimentation capteurs |

#### Connecteur P1 (série)

| Broche | GPIO | Note |
|--------|------|------|
| TX | **GPIO1** | Série |
| RX | **GPIO3** | Série |

### Bus I2C externe retenu pour le projet

| Signal I2C | GPIO | Usage |
|------------|------|-------|
| SDA | **GPIO27** | AHT21 / MAX17048 |
| SCL | **GPIO22** | AHT21 / MAX17048 |

---

## 7. Résumé des pins - Contrôleur (#1)

| GPIO | Utilisé par | Notes |
|------|-------------|-------|
| GPIO0 | BOOT | Strapping pin |
| GPIO1 | TX série | Connecteur P1 |
| GPIO2 | TFT_DC | Écran TFT |
| GPIO3 | RX série | Connecteur P1 |
| GPIO4 | LED rouge | Actif à bas |
| GPIO5 | SD_CS | Carte microSD |
| GPIO12 | TFT_MISO | SPI TFT |
| GPIO13 | TFT_MOSI | SPI TFT |
| GPIO14 | TFT_SCLK | SPI TFT |
| GPIO15 | TFT_CS | SPI TFT |
| GPIO16 | LED verte | Actif à bas |
| GPIO17 | LED bleue | Actif à bas |
| GPIO18 | SD_SCK | SPI SD |
| GPIO19 | SD_MISO | SPI SD |
| GPIO21 | TFT_BL | Backlight, ne pas réutiliser |
| GPIO22 | I2C externe SCL | CN1 / P3 |
| GPIO23 | SD_MOSI | SPI SD |
| GPIO25 | XPT2046_CLK | SPI tactile |
| GPIO26 | Speaker | Audio uniquement |
| GPIO27 | I2C externe SDA | CN1 |
| GPIO32 | XPT2046_MOSI | SPI tactile |
| GPIO33 | XPT2046_CS | SPI tactile |
| GPIO34 | LDR | ADC, entrée uniquement |
| GPIO35 | Extension P3 | Entrée uniquement |
| GPIO36 | XPT2046_IRQ | Entrée uniquement |
| GPIO39 | XPT2046_MISO | Entrée uniquement |

---

## 8. Communication ESP-NOW

| Paramètre | Valeur |
|-----------|--------|
| Protocole | ESP-NOW |
| Portée | ~200 m (extérieur) |
| Latence | ~2-3 ms |
| Timeout ACK | **1 seconde** |
| Retry ACK | **3 tentatives** espacées de 500 ms |
| WiFi power save | Modem sleep (`WIFI_PS_MIN_MODEM`) |
| Adresse MAC relais | **En dur dans le code** |

**Adresse MAC actuelle (broadcast) :**
```
FF:FF:FF:FF:FF:FF
```

> ℹ️ Remplacer par l'adresse MAC réelle du module relais une fois connue.

### Messages envoyés (Contrôleur → Relais)

| Commande | Description |
|----------|-------------|
| `HEAT_ON` | Allumer chauffage |
| `HEAT_OFF` | Éteindre chauffage |
| `PING` | Vérifier connexion |

### Messages reçus (Relais → Contrôleur)

| Réponse | Code | Description |
|---------|------|-------------|
| `ACK_ON` | 11 | Chauffage allumé confirmé |
| `ACK_OFF` | 12 | Chauffage éteint confirmé |
| `ACK_PONG` | 13 | Relais connecté |
| `ACK_LOCKED` | 14 | Interface verrouillée |
| `ACK_UNLOCKED` | 15 | Interface déverrouillée |

---

## 9. Interface Menu

### Objectif ergonomique

L'interface doit être utilisable **au doigt** sur l'écran résistif 2,8", **sans stylet**, avec une identité visuelle **rétro-futuriste** inspirée des interfaces embarquées, dashboards néon et écrans industriels sci-fi.

Le style visuel doit évoquer un univers **technique, lisible et tactile**, sans sacrifier l'ergonomie.

Conséquences directes pour le design :

- **Aucun petit bouton** ni icône isolée difficile à viser
- **Grandes zones tactiles** avec espacement visible entre actions
- **Une seule action principale par ligne**
- **Actions critiques en bas d'écran** avec gros boutons
- **Pas d'interaction fine** de type mini `+` / `-` trop rapprochés
- **Style rétro-futuriste** avec contours marqués, contrastes forts, labels larges et ambiance néon maîtrisée

### Direction artistique rétro-futuriste

L'interface doit adopter les codes suivants :

| Élément | Direction retenue |
|---------|-------------------|
| Ambiance | **Cockpit / terminal rétro-futuriste** |
| Fond principal | Sombre, uni ou très légèrement texturé |
| Couleurs accent | Cyan, ambre, vert phosphore ou orange technique |
| Contraste | Élevé pour lecture immédiate |
| Boutons | Grands blocs avec contour lumineux ou fort liseré |
| Typographie | Simple, lisible, légèrement “technique” |
| États | Couleur dédiée par statut (`ON`, `OFF`, `ALERTE`, `LOCK`) |
| Icônes | Peu nombreuses, grosses, secondaires au texte |

**Principes de style :**
- Le rendu doit faire penser à une **interface de contrôle embarqué**
- Les couleurs doivent rester **fonctionnelles avant d'être décoratives**
- Éviter l'effet “jeu vidéo gadget” : le style rétro-futuriste doit rester **sobre, utile et robuste**

### Palette visuelle recommandée

| Usage | Couleur suggérée | Rôle |
|------|------------------|------|
| Fond | Noir / anthracite très sombre | Base visuelle |
| Texte principal | Cyan clair ou blanc légèrement bleuté | Lisibilité |
| Accent action | Orange / ambre | Boutons d'action |
| État actif chauffage | Orange vif / rouge chaud | `🔥 ON` |
| État liaison OK | Vert / cyan | `📶` |
| Alerte / erreur | Rouge soutenu | Erreurs |
| Verrouillage | Jaune / ambre | `🔒` |

> ℹ️ Le style rétro-futuriste doit venir surtout des **contrastes, cadres, libellés et états**, pas d'effets graphiques lourds qui pénaliseraient les performances ou la lisibilité.

### Règles d'ergonomie tactile

| Règle | Valeur / principe |
|-------|-------------------|
| Taille mini d'une cible tactile | **≥ 70 x 40 px** |
| Taille recommandée boutons principaux | **~200 x 50 px** |
| Espacement entre boutons | **8 à 12 px minimum** |
| Zone active | Toute la surface visuelle du bouton |
| Navigation principale | **Grosses tuiles / gros boutons** |
| Réglage de valeur | **Grand bouton - / valeur / grand bouton +** |
| Action retour | Bouton large fixe en bas |
| Action principale | Bouton large coloré / très contrasté |

> ℹ️ Sur écran résistif, l'utilisateur peut appuyer avec le doigt, mais la précision est inférieure à un smartphone moderne. L'UI doit donc privilégier des **cibles larges, simples et bien espacées**.

### Principes visuels retenus

1. **Header compact** en haut avec uniquement les informations utiles.
2. **Zone centrale** composée de gros boutons tactiles.
3. **Barre d'action basse** réservée aux actions principales :
   - `RETOUR`
   - `DÉMARRER`
   - `ARRÊTER`
   - `OK`
4. Les valeurs réglables doivent être présentées dans des **cartes larges**.
5. Les états (`ON`, `OFF`, `CONNECTÉ`, `VERROUILLÉ`) doivent être visibles de loin.
6. Le texte doit rester **gros et lisible**, sans surcharge.
7. Chaque écran doit avoir une esthétique **tableau de bord rétro-futuriste** cohérente.

---

### Contrôles

| Contrôle | Action |
|----------|--------|
| 👆 Appui sur une grande tuile | Entrer dans un mode |
| 👆 Appui sur un gros bouton `-` ou `+` | Modifier une valeur |
| 👆 Appui sur bouton large d'action | Lancer / arrêter / valider |
| 👆 Appui sur bannière d'alerte | Acquitter l'erreur |
| 🔘 BOOT | Secours / réveil uniquement |

> ⚠️ Le bouton BOOT ne doit pas être l'élément principal d'ergonomie : c'est un **strapping pin**.

---

### Indicateurs du header

Affichés sur toutes les pages :

| Élément | Description |
|---------|-------------|
| Titre écran | Nom de la page courante |
| 🌡️ | Température actuelle (si AHT21 détecté) |
| 💧 | Humidité (si AHT21 détecté) |
| 📶 / ❌ | État de la liaison relais |
| 🔒 | Relais verrouillé / UI bloquée |
| 🔥 ON / OFF | État chauffage |

**Règle de sobriété :**
- Pas plus de **4 indicateurs simultanés** en plus du titre
- Priorité visuelle : `Erreur` > `Verrouillé` > `Connexion` > `Température`

**Traitement visuel recommandé :**
- Header sombre
- Titre en capitales ou semi-capitales
- Séparateurs fins lumineux
- États représentés par texte + icône, pas par icône seule

**Logique ping :**
```
Intervalle ping : 60 secondes

SI PONG reçu → compteur échecs = 0, picto = 📶
SI pas de PONG → compteur échecs + 1

SI compteur échecs ≥ 3 → alerte connexion perdue
```

**Logique verrouillage :**
```
SI ACK_LOCKED reçu :
  → Retour menu principal
  → Afficher cadenas 🔒
  → Bloquer les actions tactiles
  → Arrêter le minuteur si actif

SI ACK_UNLOCKED reçu :
  → Masquer cadenas
  → Débloquer l'interface
```

---

### Alerte connexion perdue

- Réveille l'écran si nécessaire
- Affiche un écran d'erreur plein écran
- Bouton `OK` très large et centré
- Acquittable aussi par BOOT en secours
- Style visuel : **alarme embarquée rétro-futuriste**

```
┌──────────────────────────────────────┐
│ ❌ CONNEXION RELAIS PERDUE           │
├──────────────────────────────────────┤
│ Vérifier le module relais            │
│ et la portée radio.                  │
│                                      │
│        [      OK      ]              │
└──────────────────────────────────────┘
```

**Règles UI erreur :**
- Fond rouge ou très contrasté
- Aucun autre bouton affiché
- Le bouton `OK` doit être suffisamment grand pour être appuyé au doigt
- Titre très lisible, ton “système d'alerte”

---

### Erreur capteur AHT21 externe

**Logique :**
```
Lecture AHT21 : toutes les 2 secondes (lissage EMA)

SI lecture OK → Afficher température/humidité, reset timer erreur
SI échec lecture → Timer erreur + 1 min, afficher alerte

SI timer erreur < 5 min → Chauffage CONTINUE (mode dégradé)
SI timer erreur ≥ 5 min → Chauffage OFF (sécurité)
```

**Écran d'alerte :**
- Réveille l'écran si nécessaire
- Affiche un écran simple, lisible, avec un seul bouton large

```
┌──────────────────────────────────────┐
│ ❌ ERREUR CAPTEUR                    │
├──────────────────────────────────────┤
│ Vérifier le câblage AHT21 sur CN1    │
│ SDA = GPIO27   SCL = GPIO22          │
│                                      │
│        [      OK      ]              │
└──────────────────────────────────────┘
```

> ⚠️ Le chauffage continue pendant 5 min max, puis s'arrête par sécurité.

---

### Écran Menu Principal

Le menu principal doit être composé de **3 grosses tuiles pleine largeur**, faciles à toucher, avec un rendu type **console de pilotage rétro-futuriste**.

**Règles :**
- Une tuile = un mode
- Hauteur généreuse
- Toute la tuile est cliquable
- Pas de petits numéros ni de petites icônes comme cible principale
- Contour ou liseré contrasté autour de chaque tuile

```
┌──────────────────────────────────────┐
│ WEBASTO CTRL        21.5°C   📶      │
├──────────────────────────────────────┤
│                                      │
│ [     THERMOSTAT HYSTÉRÉSIS       ]  │
│                                      │
│ [         MODE MINUTEUR           ]  │
│                                      │
│ [       MODE CONSIGNE SIMPLE      ]  │
│                                      │
└──────────────────────────────────────┘
```

**Contenu recommandé de chaque tuile :**
- Nom du mode en gros
- Sous-texte optionnel plus petit
- Éventuel état actuel sur la droite (`ACTIF`, `OFF`, `25 min`, etc.)
- Effet visuel léger au toucher : inversion, surbrillance ou bord renforcé

---

### Écran Mode A - Thermostat (Hystérésis)

L'écran doit éviter les petits boutons `[-] [+]` collés à une petite valeur.

**Disposition retenue :**
- Une carte large par paramètre
- Ligne de réglage avec :
  - gros bouton `-`
  - valeur centrale très lisible
  - gros bouton `+`
- Un bouton `RETOUR`
- Un bouton principal `ACTIVER` ou `ARRÊTER`
- Habillage visuel type panneau de contrôle

```
┌──────────────────────────────────────┐
│ THERMOSTAT                🔥 ON  📶   │
├──────────────────────────────────────┤
│ CONSIGNE                             │
│ [  -  ]   [      20.0°C      ] [ + ] │
│                                      │
│ HYSTÉRÉSIS                           │
│ [  -  ]   [       3.0°C      ] [ + ] │
│                                      │
│ TEMPÉRATURE ACTUELLE : 21.5°C        │
├──────────────────────────────────────┤
│ [   RETOUR   ]   [   ARRÊTER   ]     │
└──────────────────────────────────────┘
```

**Règles d'interaction :**
1. Appui direct sur `-` ou `+`
2. Pas besoin de “sélectionner un champ” avant réglage
3. Appui long optionnel pour auto-répétition après 500 ms
4. Retour au menu via gros bouton bas

---

### Écran Mode B - Minuteur

Le minuteur doit être encore plus simple :
- réglage en gros boutons
- affichage du temps restant en très gros
- gros bouton principal `DÉMARRER` / `ARRÊTER`
- rendu visuel évoquant un compte à rebours de tableau de bord

```
┌──────────────────────────────────────┐
│ MINUTEUR                   🔥 OFF 📶  │
├──────────────────────────────────────┤
│ DURÉE                                │
│ [  -  ]   [      30 min       ] [ + ]│
│                                      │
│ TEMPS RESTANT                        │
│            25:42                     │
│                                      │
├──────────────────────────────────────┤
│ [   RETOUR   ]   [   DÉMARRER   ]    │
└──────────────────────────────────────┘
```

**Comportement recommandé :**
- Si le minuteur tourne, le bouton principal devient `ARRÊTER`
- Le retour menu arrête le minuteur
- Le temps restant doit être lisible d'un coup d'œil

---

### Écran Mode C - Consigne

Même logique ergonomique que les autres écrans :
- un seul réglage principal
- une grande lecture de la température
- un gros bouton de lancement
- esthétique cohérente avec le reste du cockpit UI

```
┌──────────────────────────────────────┐
│ CONSIGNE SIMPLE            🔥 OFF 📶  │
├──────────────────────────────────────┤
│ TEMPÉRATURE CIBLE                    │
│ [  -  ]   [      25.0°C      ] [ + ] │
│                                      │
│ TEMPÉRATURE ACTUELLE : 21.5°C        │
│                                      │
├──────────────────────────────────────┤
│ [   RETOUR   ]   [   DÉMARRER   ]    │
└──────────────────────────────────────┘
```

**Règles d'usage :**
1. Boutons `-` / `+` larges et espacés
2. `DÉMARRER` lance le mode
3. Une fois la consigne atteinte, chauffage coupé selon la logique du mode C
4. `RETOUR` coupe le chauffage et revient au menu

---

### Barre d'action basse

Toutes les pages fonctionnelles doivent utiliser une **barre basse homogène**.

| Emplacement | Usage |
|-------------|-------|
| Bas gauche | `RETOUR` |
| Bas droite | Action principale (`DÉMARRER`, `ARRÊTER`, `OK`) |

**Règles :**
- Boutons sur toute la hauteur de la barre
- Largeur suffisante pour le doigt
- Libellés explicites, pas uniquement une icône
- Style visuel constant sur tous les écrans

---

### États visuels des boutons

| État | Rendu attendu |
|------|----------------|
| Normal | Bouton contrasté avec contour net |
| Appuyé | Inversion légère / enfoncement visuel |
| Désactivé | Contraste réduit mais texte lisible |
| Action active | Accent fort (`DÉMARRER`, `ARRÊTER`) |
| Danger / erreur | Rouge / très contrasté |

**Style recommandé :**
- Contour lumineux léger ou bord épais
- Remplissage sombre avec accent coloré
- Texte centré en capitales courtes

---

### Comportement tactile recommandé

| Cas | Comportement |
|-----|--------------|
| Appui valide | Feedback visuel immédiat |
| Premier appui après réveil écran | Consommé, aucune action fonctionnelle |
| Appui hors zone | Ignoré |
| Double appui involontaire | Anti-rebond logiciel ~150 ms |
| Appui long sur `+` / `-` | Défilement accéléré optionnel |

---

### Éléments à éviter

Pour garantir une bonne ergonomie au doigt, éviter :

- Petits boutons de moins de 40 px de haut
- Icônes seules comme unique cible tactile
- Paramètres sur plusieurs colonnes serrées
- Menus trop denses
- Scroll vertical nécessaire pour l'usage normal
- Réglage nécessitant une précision type “stylet”
- Effets rétro-futuristes trop décoratifs nuisant à la lisibilité

---

### Diagramme de navigation

```
                    ┌─────────────┐
                    │    MENU     │
                    │  PRINCIPAL  │
                    └──────┬──────┘
                           │
           ┌───────────────┼───────────────┐
           │               │               │
           ▼               ▼               ▼
    ┌─────────────┐ ┌─────────────┐ ┌─────────────┐
    │ THERMOSTAT  │ │  MINUTEUR   │ │  CONSIGNE   │
    │    (A)      │ │    (B)      │ │    (C)      │
    └─────────────┘ └─────────────┘ └─────────────┘
           │               │               │
           └───────────────┴───────────────┘
                           │
                        [RETOUR]
```

### Résumé UX retenu

L'interface finale doit respecter les principes suivants :

- **Pilotable au doigt**
- **Simple à comprendre en quelques secondes**
- **Sans petites cibles tactiles**
- **Avec gros boutons homogènes**
- **Avec une action principale évidente par écran**
- **Avec une identité rétro-futuriste lisible et sobre**
- **Compatible avec un usage debout, mobile, ou en environnement froid**


## 10. Notes ESP32-2432S028R / PlatformIO

### Configuration PlatformIO recommandée

```ini
[env:cyd]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200

lib_deps =
    bodmer/TFT_eSPI@^2.5.43
    https://github.com/PaulStoffregen/XPT2046_Touchscreen.git
    adafruit/Adafruit AHTX0
    adafruit/Adafruit MAX1704X

build_flags =
    -DUSER_SETUP_LOADED
    -DUSE_HSPI_PORT
    -DILI9341_DRIVER
    -DTFT_MISO=12
    -DTFT_MOSI=13
    -DTFT_SCLK=14
    -DTFT_CS=15
    -DTFT_DC=2
    -DTFT_RST=-1
    -DTFT_BL=21
    -DTFT_BACKLIGHT_ON=HIGH
    -DSPI_FREQUENCY=40000000
    -DSPI_READ_FREQUENCY=20000000
    -DSPI_TOUCH_FREQUENCY=2500000
```

### Particularités importantes

| Élément | Note |
|---------|------|
| **Board PlatformIO** | Utiliser `esp32dev` par défaut pour compatibilité |
| **ESP32** | ESP32-WROOM-32 classique, pas ESP32-C3 |
| **GPIO21** | Réservé au rétroéclairage TFT |
| **Tactile** | Sur bus SPI séparé du TFT |
| **GPIO34/35/36/39** | Entrées uniquement |
| **GPIO0** | Bouton BOOT, pin de strapping |
| **I2C externe** | Utiliser `Wire.begin(27, 22)` |
| **LED RGB** | Actives à l'état bas |
| **TFT_RST** | Non câblé séparément (`-1`) |

---



## 11. Structure du Code

```
📁 src/
├── config.h          → Constantes, pins, couleurs, timings
├── display_ui.cpp    → Rendu TFT ILI9341
├── touch_ui.cpp      → Gestion XPT2046 + navigation
├── sensors.cpp       → AHT21 / MAX17048 externes
├── espnow_link.cpp   → Messages ESP-NOW
└── main.cpp          → Orchestration générale
```
