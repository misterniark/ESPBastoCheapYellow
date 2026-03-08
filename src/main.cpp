#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

extern void espnow_send_command(ESPNowCommand cmd);

// Déclarations externes pour la gestion du bouton BOOT et des alertes
enum ScreenView { VIEW_MENU, VIEW_MODE_A, VIEW_MODE_B, VIEW_MODE_C, VIEW_ALERT_CONN, VIEW_ALERT_SENSOR };
extern void change_view(ScreenView view);
extern int current_view;

// Instanciation de l'état global de l'application
AppState state = {
    .currentMode = MODE_NONE,
    .isHeating = false,
    .isLocked = false,
    .relayConnected = false,
    .pingFailures = 0,
    .currentTemp = 0.0f,
    .currentHumidity = 0.0f,
    .sensorError = false,
    .sensorErrorStartTime = 0,
    .setpoint = DEFAULT_SETPOINT,
    .hysteresis = DEFAULT_HYSTERESIS,
    .timerMinutes = DEFAULT_TIMER_MIN,
    .timerRemainingSecs = 0,
    .screenAwake = true,
    .lastActivityTime = 0
};

Preferences preferences;

void loadSettings() {
    preferences.begin("espbasto", false);
    
    // On lit l'ancien mode pour memoire, mais on DOIT demarrer a OFF
    preferences.getUChar("mode", MODE_NONE); // Juste pour avancer le curseur interne ou ignorer
    state.currentMode = MODE_NONE;
    
    state.setpoint = preferences.getFloat("setpoint", DEFAULT_SETPOINT);
    state.hysteresis = preferences.getFloat("hysteresis", DEFAULT_HYSTERESIS);
    state.timerMinutes = preferences.getUShort("timer", DEFAULT_TIMER_MIN);
    
    // Validation des limites au cas où des données corrompues seraient lues
    if (state.setpoint < SETPOINT_MIN || state.setpoint > SETPOINT_MAX) state.setpoint = DEFAULT_SETPOINT;
    if (state.hysteresis < HYSTERESIS_MIN || state.hysteresis > HYSTERESIS_MAX) state.hysteresis = DEFAULT_HYSTERESIS;
    if (state.timerMinutes < TIMER_MIN || state.timerMinutes > TIMER_MAX) state.timerMinutes = DEFAULT_TIMER_MIN;
    
    preferences.end();
}

void saveSettings() {
    preferences.begin("espbasto", false);
    preferences.putUChar("mode", state.currentMode);
    preferences.putFloat("setpoint", state.setpoint);
    preferences.putFloat("hysteresis", state.hysteresis);
    preferences.putUShort("timer", state.timerMinutes);
    preferences.end();
}

void setup() {
    Serial.begin(115200);
    delay(10);
    Serial.println("\n--- ESPBasto Thermostat Controller ---");

    // Initialisation des broches matérielles
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH); // Rétroéclairage ON par défaut
    
    // LED RGB configurées et désactivées
    pinMode(LED_R, OUTPUT);
    pinMode(LED_G, OUTPUT);
    pinMode(LED_B, OUTPUT);
    digitalWrite(LED_R, LED_OFF);
    digitalWrite(LED_G, LED_OFF);
    digitalWrite(LED_B, LED_OFF);

    // Bouton BOOT (GPIO0) : secours / réveil / acquittement alertes
    pinMode(BOOT_BTN, INPUT_PULLUP);

    // Initialisation des préférences et chargement de la sauvegarde
    loadSettings();
    state.lastActivityTime = millis(); // Reset timeout inactivité

    /* 
     * Initialisation des sous-systèmes
     */
    display_ui_init();
    touch_ui_init();
    sensors_init();
    espnow_link_init();
    
    Serial.println("Setup complet. Démarrage boucle principale.");
}

void loop() {
    uint32_t now = millis();

    /* 1. Gestion de la veille de l'écran (Timeout) */
    if (state.screenAwake && (now - state.lastActivityTime > SCREEN_TIMEOUT_MS)) {
        state.screenAwake = false;
        digitalWrite(TFT_BL, LOW); // Eteindre rétroéclairage
        display_ui_sleep();     // ILI9341 SLPIN command
        Serial.println("Timeout inactivité : Mise en veille écran");
    }

    /* 1b. Gestion du bouton BOOT (secours / réveil / acquittement) */
    static uint32_t lastBootPress = 0;
    if (digitalRead(BOOT_BTN) == LOW && (now - lastBootPress > 300)) {
        lastBootPress = now;
        Serial.println("[BOOT] Appui bouton BOOT detecte.");
        
        if (!state.screenAwake) {
            // Réveiller l'écran (action consommée, pas d'action fonctionnelle)
            display_ui_wake();
            Serial.println("[BOOT] Reveil ecran via BOOT.");
        } else {
            // Si une alerte est affichée, l'acquitter
            if (current_view == VIEW_ALERT_CONN) {
                state.pingFailures = 0;
                change_view(VIEW_MENU);
                Serial.println("[BOOT] Alerte connexion acquittee.");
            } else if (current_view == VIEW_ALERT_SENSOR) {
                state.sensorError = false;
                change_view(VIEW_MENU);
                Serial.println("[BOOT] Alerte capteur acquittee.");
            }
            state.lastActivityTime = now;
        }
    }

    /* Traitement cyclique des sous-systèmes */
    touch_ui_loop();
    display_ui_loop();
    sensors_loop();   // Gère le lissage EMA et détecte les erreurs capteurs
    espnow_link_loop(); // Gère ping et réceptions

    /* 2. Logique Thermostat (Évaluation toutes les 60s) */
    static uint32_t lastThermostatEval = 0;
    if (now - lastThermostatEval > THERMOSTAT_EVAL_MS) {
        lastThermostatEval = now;

        if (state.currentMode == MODE_A_THERMOSTAT && !state.sensorError) {
            float minTemp = state.setpoint - state.hysteresis;
            if (state.currentTemp < minTemp && !state.isHeating) {
                espnow_send_command(CMD_HEAT_ON);
                state.isHeating = true;
            } else if (state.currentTemp >= state.setpoint && state.isHeating) {
                espnow_send_command(CMD_HEAT_OFF);
                state.isHeating = false;
            }
        } 
        else if (state.currentMode == MODE_C_SETPOINT && !state.sensorError) {
            if (state.currentTemp < state.setpoint && !state.isHeating) {
                espnow_send_command(CMD_HEAT_ON);
                state.isHeating = true;
            } else if (state.currentTemp >= state.setpoint && state.isHeating) {
                espnow_send_command(CMD_HEAT_OFF);
                state.isHeating = false;
                // Le mode C s'arrête définitivement une fois atteint
                state.currentMode = MODE_NONE;
                saveSettings();
            }
        }
    }

    /* 3. Logique Minuteur (Évaluation toutes les 1s) */
    static uint32_t lastTimerTick = 0;
    if (now - lastTimerTick >= 1000) {
        lastTimerTick = now;
        if (state.currentMode == MODE_B_TIMER && state.timerRemainingSecs > 0) {
            state.timerRemainingSecs--;
            if (!state.isHeating) {
                espnow_send_command(CMD_HEAT_ON);
                state.isHeating = true;
            }
            if (state.timerRemainingSecs == 0) {
                // Temps écoulé, arrêt du chauffage
                espnow_send_command(CMD_HEAT_OFF);
                state.isHeating = false;
                state.currentMode = MODE_NONE;
                saveSettings();
            }
        }
    }

    /* 4. Gestion Mode Dégradé / Sécurité Capteur */
    if (state.sensorError && state.isHeating && 
       (state.currentMode == MODE_A_THERMOSTAT || state.currentMode == MODE_C_SETPOINT)) {
        
        if (now - state.sensorErrorStartTime >= SENSOR_ERROR_TIMEOUT_MS) {
            Serial.println("⚠ ERREUR CRITIQUE : Temps mode dégradé écoulé (5 min). ARRÊT SÉCURITÉ.");
            espnow_send_command(CMD_HEAT_OFF);
            state.isHeating = false;
            state.currentMode = MODE_NONE;
            saveSettings();
        }
    }

    delay(10); // Léger retard pour ne pas bloquer Watchdog (Tâche Idle FreeRTOS)
}
