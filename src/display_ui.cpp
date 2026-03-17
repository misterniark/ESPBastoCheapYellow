#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"

// Instance TFT
TFT_eSPI tft = TFT_eSPI();

ScreenView current_view = VIEW_MENU;

// Etats precedents pour eviter de tout redessiner si inutile
bool force_redraw = true;
bool last_heating = false;
bool last_locked = false;
bool last_sensor_error = false;
bool last_relay = true;
uint16_t last_timer = 9999;
uint16_t last_timer_min = 9999;
OperatingMode last_mode = MODE_NONE;
float last_temp = -999.0f;
float last_humidity = -999.0f;
float last_setpoint = -999.0f;
float last_hysteresis = -999.0f;
uint32_t last_ui_update = 0;
bool sleep_hint_active = false;
uint32_t sleep_hint_started = 0;

void change_view(ScreenView new_view) {
    current_view = new_view;
    force_redraw = true;
    tft.fillScreen(COLOR_BG);
}

static void draw_header_title() {
    tft.fillRect(0, 0, 132, 30, COLOR_HEADER_BG);
    tft.setTextColor(COLOR_TEXT, COLOR_HEADER_BG);
    tft.setTextSize(2);
    tft.setCursor(5, 8);
    if (current_view == VIEW_MENU) tft.print("MENU");
    else if (current_view == VIEW_MODE_A) tft.print("THERMOSTAT");
    else if (current_view == VIEW_MODE_B) tft.print("MINUTEUR");
    else if (current_view == VIEW_MODE_C) tft.print("CONSIGNE");
    else tft.print("ALERTE");
}

static void draw_header_sensor_block() {
    tft.fillRect(132, 0, 92, 30, COLOR_HEADER_BG);
    if (!state.sensorError) {
        tft.setTextSize(1);
        tft.setTextColor(COLOR_TEXT, COLOR_HEADER_BG);
        tft.setCursor(135, 12);
        tft.printf("%.1fC %.0f%%", state.currentTemp, state.currentHumidity);
    }
}

static void draw_header_conn_block() {
    tft.fillRect(224, 0, 30, 30, COLOR_HEADER_BG);
    tft.setTextSize(2);
    if (state.relayConnected) {
        tft.setTextColor(COLOR_OK, COLOR_HEADER_BG);
        tft.setCursor(224, 8);
        tft.print("OK");
    } else {
        tft.setTextColor(COLOR_ALERT, COLOR_HEADER_BG);
        tft.setCursor(224, 8);
        tft.print("NC");
    }
}

static void draw_header_lock_block() {
    tft.fillRect(254, 0, 30, 30, COLOR_HEADER_BG);
    if (state.isLocked) {
        tft.setTextSize(2);
        tft.setTextColor(COLOR_LOCKED, COLOR_HEADER_BG);
        tft.setCursor(254, 8);
        tft.print("LK");
    }
}

static void draw_header_heat_block() {
    tft.fillRect(284, 0, 36, 30, COLOR_HEADER_BG);
    tft.setTextSize(2);
    if (state.isHeating) {
        tft.setTextColor(COLOR_ON, COLOR_HEADER_BG);
        tft.setCursor(284, 8);
        tft.print("ON");
    } else {
        tft.setTextColor(COLOR_OFF, COLOR_HEADER_BG);
        tft.setCursor(284, 8);
        tft.print("OFF");
    }
}

void draw_header() {
    if (force_redraw) {
        tft.fillRect(0, 0, 320, 30, COLOR_HEADER_BG);
        tft.drawLine(0, 30, 320, 30, COLOR_ACCENT);
        draw_header_title();
        draw_header_sensor_block();
        draw_header_conn_block();
        draw_header_lock_block();
        draw_header_heat_block();
        return;
    }

    if (state.currentTemp != last_temp || state.currentHumidity != last_humidity || state.sensorError != last_sensor_error) {
        draw_header_sensor_block();
    }
    if (state.relayConnected != last_relay) {
        draw_header_conn_block();
    }
    if (state.isLocked != last_locked) {
        draw_header_lock_block();
    }
    if (state.isHeating != last_heating) {
        draw_header_heat_block();
    }
}

void draw_button(int x, int y, int w, int h, const char* label, uint16_t color, uint16_t text_color) {
    tft.drawRect(x, y, w, h, color);
    tft.fillRect(x+2, y+2, w-4, h-4, COLOR_BTN_BG);
    tft.setTextColor(text_color, COLOR_BTN_BG);
    tft.setTextSize(2);
    int text_width = strlen(label) * 12;
    tft.setCursor(x + (w - text_width) / 2, y + (h - 16) / 2);
    tft.print(label);
}

void draw_value_selector(int y, const char* label, float val, const char* unit, bool isFloat = true) {
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setTextSize(1);
    tft.setCursor(10, y - 10);
    tft.print(label);

    draw_button(10, y, 70, 40, "-", COLOR_ACCENT, COLOR_ACCENT);
    
    tft.fillRect(91, y + 1, 138, 38, COLOR_BG);
    tft.drawRect(90, y, 140, 40, COLOR_TEXT);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setTextSize(3);
    char buf[16];
    if (isFloat) snprintf(buf, sizeof(buf), "%.1f %s", val, unit);
    else snprintf(buf, sizeof(buf), "%d %s", (int)val, unit);
    
    int strw = strlen(buf) * 18;
    tft.setCursor(90 + (140 - strw)/2, y + 10);
    tft.print(buf);

    draw_button(240, y, 70, 40, "+", COLOR_ACCENT, COLOR_ACCENT);
}

void draw_center_temp(int y) {
    tft.fillRect(0, y, 320, 18, COLOR_BG);
    if (!state.sensorError) {
        tft.setTextColor(COLOR_TEXT, COLOR_BG);
        tft.setTextSize(2);
        char buf[32];
        snprintf(buf, sizeof(buf), "TEMP. ACTUELLE: %.1fC", state.currentTemp);
        int w = strlen(buf) * 12;
        tft.setCursor((320 - w)/2, y);
        tft.print(buf);
    }
}

void draw_bottom_bar(const char* right_action) {
    draw_button(10, 190, 140, 40, "RETOUR", COLOR_TEXT, COLOR_TEXT);
    if (state.currentMode != MODE_NONE) {
        draw_button(170, 190, 140, 40, "ARRETER", COLOR_ALERT, COLOR_ALERT);
    } else {
        if (!state.relayConnected) {
            draw_button(170, 190, 140, 40, right_action, COLOR_OFF, COLOR_OFF);
        } else {
            draw_button(170, 190, 140, 40, right_action, COLOR_ACCENT, COLOR_ACCENT);
        }
    }
}

void draw_menu() {
    if (force_redraw) {
        draw_button(10, 45, 300, 45, "1. THERMOSTAT", COLOR_ACCENT, COLOR_TEXT);
        draw_button(10, 100, 300, 45, "2. MODE MINUTEUR", COLOR_ACCENT, COLOR_TEXT);
        draw_button(10, 155, 300, 45, "3. MODE CONSIGNE", COLOR_ACCENT, COLOR_TEXT);
    }
}

void draw_mode_a() {
    if (force_redraw || state.setpoint != last_setpoint || state.hysteresis != last_hysteresis) {
        tft.fillRect(0, 31, 320, 158, COLOR_BG);
        draw_value_selector(50, "CONSIGNE", state.setpoint, "C");
        draw_value_selector(120, "HYSTERESIS", state.hysteresis, "C");
        draw_center_temp(168);
        draw_bottom_bar("DEMARRER");
        
        last_setpoint = state.setpoint;
        last_hysteresis = state.hysteresis;
    } else {
        if (state.currentTemp != last_temp || state.sensorError != last_sensor_error) draw_center_temp(168);
        if (state.isHeating != last_heating || state.currentMode != last_mode || state.relayConnected != last_relay) {
            draw_bottom_bar("DEMARRER");
        }
    }
}

static void draw_timer_readout() {
    tft.fillRect(40, 118, 240, 42, COLOR_BG);
    tft.setTextColor(COLOR_ACCENT, COLOR_BG);
    tft.setTextSize(4);

    char buf[16];
    int m = state.timerRemainingSecs / 60;
    int s = state.timerRemainingSecs % 60;
    if (!state.isHeating && state.timerRemainingSecs == 0) {
        snprintf(buf, sizeof(buf), "%02d:00", state.timerMinutes);
    } else {
        snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    }

    int left = 160 - (strlen(buf) * 24) / 2;
    tft.setCursor(left, 130);
    tft.print(buf);
}

void draw_mode_b() {
    if (force_redraw || state.timerMinutes != last_timer_min || state.currentMode != last_mode || state.relayConnected != last_relay) {
        tft.fillRect(0, 31, 320, 158, COLOR_BG);
        draw_value_selector(60, "DUREE", state.timerMinutes, "min", false);
        draw_timer_readout();
        draw_bottom_bar("DEMARRER");
    } else {
        if (state.timerRemainingSecs != last_timer || state.isHeating != last_heating) {
            draw_timer_readout();
        }
        if (state.isHeating != last_heating || state.relayConnected != last_relay) {
            draw_bottom_bar("DEMARRER");
        }
    }

    last_timer_min = state.timerMinutes;
    last_timer = state.timerRemainingSecs;
}

void draw_mode_c() {
    if (force_redraw || state.setpoint != last_setpoint) {
        tft.fillRect(0, 31, 320, 158, COLOR_BG);
        draw_value_selector(80, "TEMPERATURE CIBLE", state.setpoint, "C");
        draw_center_temp(140);
        draw_bottom_bar("DEMARRER");
        
        last_setpoint = state.setpoint;
    } else {
        if (state.currentTemp != last_temp || state.sensorError != last_sensor_error) draw_center_temp(140);
        if (state.isHeating != last_heating || state.currentMode != last_mode || state.relayConnected != last_relay) {
            draw_bottom_bar("DEMARRER");
        }
    }
}

void draw_alerts() {
    if (force_redraw) {
        tft.drawRect(10, 40, 300, 150, COLOR_ALERT);
        tft.fillRect(12, 42, 296, 146, COLOR_ALERT_BG);
        tft.setTextColor(COLOR_TEXT, COLOR_ALERT_BG);
        tft.setTextSize(2);

        if (current_view == VIEW_ALERT_CONN) {
            tft.setCursor(20, 60);
            tft.print("CONNEXION RELAIS PERDUE");
            tft.setTextSize(1);
            tft.setCursor(20, 100);
            tft.print("Verifiez le module distant");
        } else {
            tft.setCursor(20, 60);
            tft.print("ERREUR CAPTEUR AHT21");
            tft.setTextSize(1);
            tft.setCursor(20, 100);
            tft.print("Verifiez CN1: SDA=27 / SCL=22");
        }
        
        draw_button(90, 140, 140, 40, "OK", COLOR_ALERT, COLOR_TEXT);
    }
}

void display_ui_init() {
    Serial.println("[Display] Init TFT");
    tft.init();
    tft.setRotation(1); // Mode paysage 320x240
    tft.fillScreen(COLOR_BG);
    
    // Pour ne pas attendre, on force le rendu initial
    force_redraw = true;
    current_view = VIEW_MENU;
}

void display_ui_wake() {
    tft.writecommand(ILI9341_SLPOUT);
    delay(120);
    digitalWrite(TFT_BL, HIGH);
    state.screenAwake = true;
    state.lastActivityTime = millis();
    sleep_hint_active = false;
    force_redraw = true;
}

void display_ui_show_sleep_hint() {
    if (sleep_hint_active) {
        return;
    }

    tft.fillScreen(COLOR_BG);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setTextSize(2);
    const char* line1 = "MISE EN VEILLE...";
    tft.setCursor((320 - strlen(line1) * 12) / 2, 90);
    tft.print(line1);
    tft.setTextSize(1);
    const char* line2 = "Toucher l'ecran pour reveiller";
    tft.setCursor((320 - strlen(line2) * 6) / 2, 130);
    tft.print(line2);
    sleep_hint_active = true;
    sleep_hint_started = millis();
}

void display_ui_sleep() {
    sleep_hint_active = false;
    tft.writecommand(ILI9341_SLPIN);
}

bool display_ui_sleep_hint_active() {
    return sleep_hint_active;
}

bool display_ui_sleep_hint_expired() {
    return sleep_hint_active && (millis() - sleep_hint_started >= 1500);
}

void display_ui_cancel_sleep_hint() {
    if (sleep_hint_active) {
        sleep_hint_active = false;
        last_ui_update = 0;
        force_redraw = true;
        tft.fillScreen(COLOR_BG);
    }
}

void display_ui_loop() {
    uint32_t now = millis();
    
    // Forcer le view alert s'il le faut
    if (state.pingFailures >= 3 && current_view != VIEW_ALERT_CONN && current_view != VIEW_ALERT_SENSOR) {
        display_ui_wake();
        change_view(VIEW_ALERT_CONN);
    } else if (state.sensorError && current_view != VIEW_ALERT_SENSOR && current_view != VIEW_ALERT_CONN
               && (now - state.sensorAlertAckedTime >= SENSOR_ALERT_COOLDOWN_MS)) {
        display_ui_wake();
        change_view(VIEW_ALERT_SENSOR);
    }

    // Update ecran à 10 FPS max pour ne pas ralentir le reste
    if (now - last_ui_update > 100) {
        last_ui_update = now;

        if (state.screenAwake) {
            if (sleep_hint_active) {
                return;
            }

            draw_header();
            
            switch (current_view) {
                case VIEW_MENU:   draw_menu(); break;
                case VIEW_MODE_A: draw_mode_a(); break;
                case VIEW_MODE_B: draw_mode_b(); break;
                case VIEW_MODE_C: draw_mode_c(); break;
                case VIEW_ALERT_CONN:
                case VIEW_ALERT_SENSOR: draw_alerts(); break;
            }

            last_temp = state.currentTemp;
            last_humidity = state.currentHumidity;
            last_heating = state.isHeating;
            last_locked = state.isLocked;
            last_sensor_error = state.sensorError;
            last_relay = state.relayConnected;
            last_mode = state.currentMode;
            force_redraw = false;
        }
    }
}
