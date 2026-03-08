#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"

// Instance TFT
TFT_eSPI tft = TFT_eSPI();

// Enum des ecrans 
enum ScreenView { 
    VIEW_MENU, 
    VIEW_MODE_A, 
    VIEW_MODE_B, 
    VIEW_MODE_C, 
    VIEW_ALERT_CONN, 
    VIEW_ALERT_SENSOR 
};
ScreenView current_view = VIEW_MENU;

// Etats precedents pour eviter de tout redessiner si inutile
bool force_redraw = true;
bool last_heating = false;
bool last_locked = false;
bool last_relay = true;
uint16_t last_timer = 9999;
float last_temp = -999.0f;
float last_setpoint = -999.0f;
float last_hysteresis = -999.0f;
uint32_t last_ui_update = 0;

void change_view(ScreenView new_view) {
    current_view = new_view;
    force_redraw = true;
    tft.fillScreen(COLOR_BG);
}

void draw_header() {
    // Si la temperature ou l'etat a change
    if (force_redraw || state.currentTemp != last_temp || state.isHeating != last_heating || 
        state.isLocked != last_locked || state.relayConnected != last_relay) {
        
        tft.fillRect(0, 0, 320, 30, tft.color565(20, 20, 20));
        tft.drawLine(0, 30, 320, 30, COLOR_ACCENT);

        tft.setTextColor(COLOR_TEXT);
        tft.setTextSize(2);
        
        // Titre gauge gauche
        tft.setCursor(10, 8);
        if (current_view == VIEW_MENU) tft.print("MENU");
        else if (current_view == VIEW_MODE_A) tft.print("THERMOSTAT");
        else if (current_view == VIEW_MODE_B) tft.print("MINUTEUR");
        else if (current_view == VIEW_MODE_C) tft.print("CONSIGNE");
        else tft.print("ALERTE");

        // Temperature et humidite courante au milieu
        if (!state.sensorError) {
            tft.setCursor(140, 8);
            tft.printf("%.1fC %.0f%%", state.currentTemp, state.currentHumidity);
        }

        // Icones status a droite
        int x_icons = 220;
        tft.setCursor(x_icons, 8);
        
        // Connexion
        if (state.relayConnected) {
            tft.setTextColor(COLOR_OK);
            tft.print("OK ");
        } else {
            tft.setTextColor(COLOR_ALERT);
            tft.print("NC ");
        }

        // Verrou
        if (state.isLocked) {
            tft.setTextColor(COLOR_LOCKED);
            tft.print("LOCK ");
        }

        // Chauffage
        if (state.isHeating) {
            tft.setTextColor(COLOR_ON);
            tft.print("ON");
        } else {
            tft.setTextColor(COLOR_OFF);
            tft.print("OFF");
        }
    }
}

void draw_button(int x, int y, int w, int h, const char* label, uint16_t color, uint16_t text_color) {
    tft.drawRect(x, y, w, h, color);
    tft.fillRect(x+2, y+2, w-4, h-4, tft.color565(30, 30, 30));
    tft.setTextColor(text_color);
    tft.setTextSize(2);
    // Rough centering
    int text_width = strlen(label) * 12; // approx width size 2
    tft.setCursor(x + (w - text_width) / 2, y + (h - 16) / 2);
    tft.print(label);
}

void draw_value_selector(int y, const char* label, float val, const char* unit, bool isFloat = true) {
    tft.setTextColor(COLOR_TEXT);
    tft.setTextSize(1);
    tft.setCursor(10, y - 10);
    tft.print(label);

    draw_button(10, y, 70, 40, "-", COLOR_ACCENT, COLOR_ACCENT);
    
    tft.drawRect(90, y, 140, 40, COLOR_TEXT);
    tft.setTextColor(COLOR_TEXT);
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
        tft.setTextColor(COLOR_TEXT);
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
    if (state.isHeating) {
        draw_button(170, 190, 140, 40, "ARRETER", COLOR_ALERT, COLOR_ALERT);
    } else {
        draw_button(170, 190, 140, 40, right_action, COLOR_ACCENT, COLOR_ACCENT);
    }
}

void draw_menu() {
    if (force_redraw) {
        draw_button(10, 45, 300, 45, "1. THERMOSTAT HYSTERESIS", COLOR_ACCENT, COLOR_TEXT);
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
        if (state.currentTemp != last_temp) draw_center_temp(168);
        if (state.isHeating != last_heating) draw_bottom_bar("DEMARRER");
    }
}

void draw_mode_b() {
    if (force_redraw || state.timerMinutes != last_setpoint || state.timerRemainingSecs != last_timer || state.isHeating != last_heating) {
        tft.fillRect(0, 31, 320, 158, COLOR_BG);
        draw_value_selector(60, "DUREE", state.timerMinutes, "min", false);
        
        tft.setTextColor(COLOR_ACCENT);
        tft.setTextSize(4);
        char buf[16];
        int m = state.timerRemainingSecs / 60;
        int s = state.timerRemainingSecs % 60;
        if (!state.isHeating && state.timerRemainingSecs == 0) {
            snprintf(buf, sizeof(buf), "%02d:00", state.timerMinutes);
        } else {
            snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
        }
        int left = 160 - (strlen(buf)*24)/2;
        tft.setCursor(left, 130);
        tft.print(buf);

        draw_bottom_bar("DEMARRER");

        last_setpoint = state.timerMinutes;
        last_timer = state.timerRemainingSecs;
    }
}

void draw_mode_c() {
    if (force_redraw || state.setpoint != last_setpoint) {
        tft.fillRect(0, 31, 320, 158, COLOR_BG);
        draw_value_selector(80, "TEMPERATURE CIBLE", state.setpoint, "C");
        draw_center_temp(140);
        draw_bottom_bar("DEMARRER");
        
        last_setpoint = state.setpoint;
    } else {
        if (state.currentTemp != last_temp) draw_center_temp(140);
        if (state.isHeating != last_heating) draw_bottom_bar("DEMARRER");
    }
}

void draw_alerts() {
    if (force_redraw) {
        tft.drawRect(10, 40, 300, 150, COLOR_ALERT);
        tft.fillRect(12, 42, 296, 146, tft.color565(40, 0, 0)); // Fond rouge sombre
        tft.setTextColor(COLOR_TEXT);
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
    // Reveil ecran : on ecrit GPIO(TFT_BL) en HIGH et on demande a l'ecran de sortir de SLPIN
    tft.writecommand(ILI9341_SLPOUT);
    delay(120);
    digitalWrite(TFT_BL, HIGH);
    state.screenAwake = true;
    state.lastActivityTime = millis();
}

void display_ui_sleep() {
    // Ce code n'eteint que le controleur ILI9341, le Retro-Eclairage est deja off
    tft.writecommand(ILI9341_SLPIN);
}

void display_ui_loop() {
    uint32_t now = millis();
    
    // Forcer le view alert s'il le faut
    if (state.pingFailures >= 3 && current_view != VIEW_ALERT_CONN && current_view != VIEW_ALERT_SENSOR) {
        display_ui_wake();
        change_view(VIEW_ALERT_CONN);
    } else if (state.sensorError && current_view != VIEW_ALERT_SENSOR && current_view != VIEW_ALERT_CONN) {
        display_ui_wake();
        change_view(VIEW_ALERT_SENSOR);
    }

    // Update ecran à 10 FPS max pour ne pas ralentir le reste
    if (now - last_ui_update > 100) {
        last_ui_update = now;

        if (state.screenAwake) {
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
            last_heating = state.isHeating;
            last_locked = state.isLocked;
            last_relay = state.relayConnected;
            force_redraw = false;
        }
    }
}
