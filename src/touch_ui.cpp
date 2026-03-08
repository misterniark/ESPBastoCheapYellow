#include <Arduino.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include "config.h"

// Instance Touch and SPI
SPIClass touchSpi(VSPI); // Using VSPI for the touch driver bus
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

// Enum pour eviter header cyclique ou l'inclure si dispo
enum ScreenView { 
    VIEW_MENU, 
    VIEW_MODE_A, 
    VIEW_MODE_B, 
    VIEW_MODE_C, 
    VIEW_ALERT_CONN, 
    VIEW_ALERT_SENSOR 
};

extern void change_view(ScreenView view); // Definie dans display_ui.cpp
extern void saveSettings();       // main.cpp

// Dimensions calibration ecran touch "Brut" XPT2046
// Ces valeurs dependent de l'ecran exact mais standards generalement:
#define TOUCH_MIN_X 300
#define TOUCH_MAX_X 3800
#define TOUCH_MIN_Y 300
#define TOUCH_MAX_Y 3800

uint32_t last_touch_time = 0;
bool is_touch_down = false;

// Helpers
bool is_in_rect(int tx, int ty, int x, int y, int w, int h) {
    return (tx >= x && tx <= x + w && ty >= y && ty <= y + h);
}

void touch_ui_init() {
    Serial.println("[Touch] Init bus SPI + XPT2046");
    touchSpi.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
    ts.begin(touchSpi);
    ts.setRotation(1); // Mode paysage 320x240 map to TFT rotation 1
}

void touch_ui_loop() {
    // Anti-rebond et frequence de test (toutes les 50ms min)
    uint32_t now = millis();

    // Verifier ISR pin si activee (efficace pour eviter requetes SPI bloquantes)
    if (ts.tirqTouched() || ts.touched()) {
        if (!is_touch_down && (now - last_touch_time > 150)) {
            // Un appui detecte !
            is_touch_down = true;
            last_touch_time = now;
            
            // 1. Reveiller l'ecran s'il dort !
            if (!state.screenAwake) {
                extern void display_ui_wake();
                display_ui_wake();
                return; // Action consommee just pour reveiller.
            }
            
            state.lastActivityTime = now; // MAJ de l'inactivite

            // Si verouille, on ignore tout (le clic reaffiche juste l'ecran et reset lastActivityTime)
            if (state.isLocked) {
                Serial.println("[Touch] Clic ignore (Verrouille)");
                return;
            }

            // Lire la position (brute)
            TS_Point p = ts.getPoint();
            // Transformer du raw tactile au pixel ecran
            // Attention: depend du setup de calibration, pour la cible rot=1:
            int tx = map(p.x, TOUCH_MIN_X, TOUCH_MAX_X, 0, 320);
            int ty = map(p.y, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, 240);
            
            // On borne
            tx = constrain(tx, 0, 320);
            ty = constrain(ty, 0, 240);
            
            extern int current_view; // Hack access pour reduire fichiers partages
            
            // ALERTS
            if (current_view == VIEW_ALERT_CONN || current_view == VIEW_ALERT_SENSOR) { // alerts CONN ou SENSOR
                if (is_in_rect(tx, ty, 90, 140, 140, 40)) { // Bouton OK
                    // Acquitter
                    if (current_view == VIEW_ALERT_CONN) state.pingFailures = 0; // force reset
                    if (current_view == VIEW_ALERT_SENSOR) state.sensorError = false; 
                    change_view(VIEW_MENU); // View Menu
                }
                return;
            }

            // MENU PRINCIPAL (view 0)
            if (current_view == VIEW_MENU) {
                if (is_in_rect(tx, ty, 10, 45,  300, 45)) change_view(VIEW_MODE_A); // Mode A
                if (is_in_rect(tx, ty, 10, 100, 300, 45)) change_view(VIEW_MODE_B); // Mode B
                if (is_in_rect(tx, ty, 10, 155, 300, 45)) change_view(VIEW_MODE_C); // Mode C
                return;
            }

            // --- BOTTOM BAR SUR LES AUTRES VUES ---
            // Bouton retour a gauche (10, 190, 140, 40)
            if (is_in_rect(tx, ty, 10, 190, 140, 40)) {
                // Return arrete le chauffage si actif (parfois recommande)
                // Ou non dependant choix ux. Faisons le revenir au mode NULL.
                if (state.isHeating) {
                    extern void espnow_send_command(ESPNowCommand cmd); // hack cmd include
                    espnow_send_command(CMD_HEAT_OFF); // CMD_HEAT_OFF = 2
                    state.isHeating = false;
                }
                state.currentMode = MODE_NONE; // MODE_NONE
                saveSettings();
                change_view(VIEW_MENU);
                return;
            }
            
            // Bouton Action (Start/Stop) a droite (170, 190, 140, 40)
            if (is_in_rect(tx, ty, 170, 190, 140, 40)) {
                extern void espnow_send_command(ESPNowCommand cmd);
                
                if (state.isHeating) {
                    espnow_send_command(CMD_HEAT_OFF); // HEAT_OFF
                    state.isHeating = false;
                    state.currentMode = MODE_NONE;
                    saveSettings();
                } else {
                    espnow_send_command(CMD_HEAT_ON); // HEAT_ON
                    state.isHeating = true;
                    if (current_view == VIEW_MODE_A) state.currentMode = MODE_A_THERMOSTAT;
                    if (current_view == VIEW_MODE_B) { 
                        state.currentMode = MODE_B_TIMER;
                        state.timerRemainingSecs = state.timerMinutes * 60;
                    }
                    if (current_view == VIEW_MODE_C) state.currentMode = MODE_C_SETPOINT;
                    saveSettings();
                }
                return;
            }

            // --- CONTROLES VALEURS (+ et -) ---
            // Fonction utilitaire bouton : draw_button(10, y, 60, 40, "-", ...); et (250, y, 60, 40, "+", ...)
            
            if (current_view == VIEW_MODE_A) { // Mode A (Set 50, Hyst 120)
                if (is_in_rect(tx, ty, 10, 50, 70, 40)) {
                    state.setpoint -= SETPOINT_STEP;
                    if (state.setpoint < SETPOINT_MIN) state.setpoint = SETPOINT_MIN;
                }
                else if (is_in_rect(tx, ty, 240, 50, 70, 40)) {
                    state.setpoint += SETPOINT_STEP;
                    if (state.setpoint > SETPOINT_MAX) state.setpoint = SETPOINT_MAX;
                }
                else if (is_in_rect(tx, ty, 10, 120, 70, 40)) {
                    state.hysteresis -= HYSTERESIS_STEP;
                    if (state.hysteresis < HYSTERESIS_MIN) state.hysteresis = HYSTERESIS_MIN;
                }
                else if (is_in_rect(tx, ty, 240, 120, 70, 40)) {
                    state.hysteresis += HYSTERESIS_STEP;
                    if (state.hysteresis > HYSTERESIS_MAX) state.hysteresis = HYSTERESIS_MAX;
                }
            }
            else if (current_view == VIEW_MODE_B) { // Mode B (Timer 60)
                if (is_in_rect(tx, ty, 10, 60, 70, 40)) {
                    state.timerMinutes -= TIMER_STEP;
                    if (state.timerMinutes < TIMER_MIN) state.timerMinutes = TIMER_MIN;
                }
                else if (is_in_rect(tx, ty, 240, 60, 70, 40)) {
                    state.timerMinutes += TIMER_STEP;
                    if (state.timerMinutes > TIMER_MAX) state.timerMinutes = TIMER_MAX;
                }
            }
            else if (current_view == VIEW_MODE_C) { // Mode C (Set 80)
                if (is_in_rect(tx, ty, 10, 80, 70, 40)) {
                    state.setpoint -= SETPOINT_STEP;
                    if (state.setpoint < SETPOINT_MIN) state.setpoint = SETPOINT_MIN;
                }
                else if (is_in_rect(tx, ty, 240, 80, 70, 40)) {
                    state.setpoint += SETPOINT_STEP;
                    if (state.setpoint > SETPOINT_MAX) state.setpoint = SETPOINT_MAX;
                }
            }
        }
    } else {
        is_touch_down = false; // Reset au relachement
    }
}
