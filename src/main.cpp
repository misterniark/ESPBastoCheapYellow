#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

// Instanciation de l'état global de l'application
AppState state = {
    .currentMode = MODE_NONE,
    .lastSelectedMode = MODE_NONE,
    .isHeating = false,
    .heatingRequested = false,
    .isLocked = false,
    .relayConnected = false,
    .pingFailures = 0,
    .currentTemp = 0.0f,
    .currentHumidity = 0.0f,
    .sensorError = false,
    .sensorErrorStartTime = 0,
    .sensorAlertAckedTime = 0,
    .setpoint = DEFAULT_SETPOINT,
    .hysteresis = DEFAULT_HYSTERESIS,
    .timerMinutes = DEFAULT_TIMER_MIN,
    .timerRemainingSecs = 0,
    .modeStopPending = false,
    .modeStopView = VIEW_MENU,
    .screenAwake = true,
    .lastActivityTime = 0,
    .forceEval = false
};

Preferences preferences;
bool _settingsDirty = false;
uint32_t _lastSettingsChangeTime = 0;

void loadSettings() {
    preferences.begin("espbasto", false);
    
    state.currentMode = MODE_NONE;
    state.lastSelectedMode = (OperatingMode)preferences.getUChar("mode", MODE_NONE);
    
    state.setpoint = preferences.getFloat("setpoint", DEFAULT_SETPOINT);
    state.hysteresis = preferences.getFloat("hysteresis", DEFAULT_HYSTERESIS);
    state.timerMinutes = preferences.getUShort("timer", DEFAULT_TIMER_MIN);
    
    // Validation des limites au cas où des données corrompues seraient lues
    if (state.lastSelectedMode > MODE_C_SETPOINT) state.lastSelectedMode = MODE_NONE;
    if (state.setpoint < SETPOINT_MIN || state.setpoint > SETPOINT_MAX) state.setpoint = DEFAULT_SETPOINT;
    if (state.hysteresis < HYSTERESIS_MIN || state.hysteresis > HYSTERESIS_MAX) state.hysteresis = DEFAULT_HYSTERESIS;
    if (state.timerMinutes < TIMER_MIN || state.timerMinutes > TIMER_MAX) state.timerMinutes = DEFAULT_TIMER_MIN;
    
    preferences.end();
}

void markSettingsDirty() {
    _settingsDirty = true;
    _lastSettingsChangeTime = millis();
}

void saveSettings() {
    _settingsDirty = false;
    preferences.begin("espbasto", false);
    preferences.putUChar("mode", state.lastSelectedMode);
    preferences.putFloat("setpoint", state.setpoint);
    preferences.putFloat("hysteresis", state.hysteresis);
    preferences.putUShort("timer", state.timerMinutes);
    preferences.end();
}

bool request_mode_stop(ScreenView nextView) {
    state.modeStopPending = true;
    state.modeStopView = nextView;

    if (state.currentMode == MODE_NONE && !state.isHeating && !state.heatingRequested) {
        finalize_mode_stop();
        return true;
    }

    if (espnow_send_command(CMD_HEAT_OFF)) {
        state.heatingRequested = false;
        return true;
    }

    state.modeStopPending = false;
    state.modeStopView = current_view;
    return false;
}

void finalize_mode_stop() {
    state.currentMode = MODE_NONE;
    state.timerRemainingSecs = 0;
    state.modeStopPending = false;
    saveSettings();

    if (current_view != state.modeStopView) {
        change_view(state.modeStopView);
    }
}

void cancel_mode_stop() {
    state.modeStopPending = false;
    state.modeStopView = current_view;
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
    if (state.screenAwake) {
        if (!display_ui_sleep_hint_active() && (now - state.lastActivityTime > SCREEN_TIMEOUT_MS)) {
            display_ui_show_sleep_hint();
        } else if (display_ui_sleep_hint_expired()) {
            state.screenAwake = false;
            digitalWrite(TFT_BL, LOW);
            display_ui_sleep();
            Serial.println("Timeout inactivité : Mise en veille écran");
        }
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
            if (display_ui_sleep_hint_active()) {
                display_ui_cancel_sleep_hint();
                state.lastActivityTime = now;
                return;
            }

            // Si une alerte est affichée, l'acquitter
            if (current_view == VIEW_ALERT_CONN) {
                state.pingFailures = 0;
                change_view(VIEW_MENU);
                Serial.println("[BOOT] Alerte connexion acquittee.");
            } else if (current_view == VIEW_ALERT_SENSOR) {
                state.sensorAlertAckedTime = now;
                change_view(VIEW_MENU);
                Serial.println("[BOOT] Alerte capteur acquittee (cooldown 60s).");
            }
            state.lastActivityTime = now;
        }
    }

    /* Traitement cyclique des sous-systèmes */
    touch_ui_loop();
    display_ui_loop();
    sensors_loop();   // Gère le lissage EMA et détecte les erreurs capteurs
    espnow_link_loop(); // Gère ping et réceptions

    /* 2. Logique Thermostat (Évaluation toutes les 60s ou sur demande) */
    static uint32_t lastThermostatEval = 0;
    if (state.forceEval || (now - lastThermostatEval > THERMOSTAT_EVAL_MS)) {
        state.forceEval = false;
        lastThermostatEval = now;

        if (!state.modeStopPending && state.currentMode == MODE_A_THERMOSTAT && !state.sensorError) {
            float minTemp = state.setpoint - state.hysteresis;
            if (state.currentTemp < minTemp && !state.heatingRequested) {
                if (espnow_send_command(CMD_HEAT_ON)) {
                    state.heatingRequested = true;
                }
            } else if (state.currentTemp >= state.setpoint && state.heatingRequested) {
                request_mode_stop(current_view);
            }
        } 
        else if (!state.modeStopPending && state.currentMode == MODE_C_SETPOINT && !state.sensorError) {
            if (state.currentTemp < state.setpoint && !state.heatingRequested) {
                if (espnow_send_command(CMD_HEAT_ON)) {
                    state.heatingRequested = true;
                }
            } else if (state.currentTemp >= state.setpoint && state.heatingRequested) {
                request_mode_stop(current_view);
            }
        }
    }

    /* 3. Logique Minuteur (Évaluation toutes les 1s) */
    static uint32_t lastTimerTick = 0;
    if (now - lastTimerTick >= 1000) {
        lastTimerTick = now;
        if (!state.modeStopPending && state.currentMode == MODE_B_TIMER && state.timerRemainingSecs > 0) {
            state.timerRemainingSecs--;
            if (!state.heatingRequested) {
                if (espnow_send_command(CMD_HEAT_ON)) {
                    state.heatingRequested = true;
                }
            }
            if (state.timerRemainingSecs == 0) {
                request_mode_stop(current_view);
            }
        }
    }

    /* 4. Gestion Mode Dégradé / Sécurité Capteur */
    if (!state.modeStopPending && state.sensorError && (state.isHeating || state.heatingRequested) && 
       (state.currentMode == MODE_A_THERMOSTAT || state.currentMode == MODE_C_SETPOINT)) {
        
        if (now - state.sensorErrorStartTime >= SENSOR_ERROR_TIMEOUT_MS) {
            Serial.println("⚠ ERREUR CRITIQUE : Temps mode dégradé écoulé (5 min). ARRÊT SÉCURITÉ.");
            request_mode_stop(current_view);
        }
    }

    /* 5. Sauvegarde différée des réglages +/- */
    if (_settingsDirty && (now - _lastSettingsChangeTime >= SETTINGS_SAVE_DELAY_MS)) {
        saveSettings();
    }

    delay(10);
}
