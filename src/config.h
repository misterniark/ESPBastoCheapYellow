#pragma once

#include <Arduino.h>

// --- PINS DEFINITIONS ---

// TFT ILI9341 (SPI TFT)
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  -1
#define TFT_BL   21

// Touch XPT2046 (SPI Dedicated)
#define TOUCH_IRQ  36
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CLK  25
#define TOUCH_CS   33

// MicroSD
#define SD_MISO 19
#define SD_MOSI 23
#define SD_SCK  18
#define SD_CS   5

// RGB LED (Active LOW)
#define LED_R 4
#define LED_G 16
#define LED_B 17
#define LED_ON  LOW
#define LED_OFF HIGH

// LDR
#define LDR_PIN 34

// Speaker
#define SPEAKER_PIN 26

// I2C (AHT21 / MAX17048) - CN1 Connector
#define I2C_SDA 27
#define I2C_SCL 22

// Buttons
#define BOOT_BTN 0


// --- SYSTEM PARAMETERS ---

// Power Management
#define SCREEN_TIMEOUT_MS  60000 // 60s
#define SENSOR_READ_MS     2000  // 2s
#define THERMOSTAT_EVAL_MS 60000 // 60s
#define SENSOR_ERROR_TIMEOUT_MS 300000 // 5 minutes (5 * 60 * 1000)

// Default Settings
#define DEFAULT_SETPOINT   20.0f
#define DEFAULT_HYSTERESIS 3.0f
#define DEFAULT_TIMER_MIN  30

// Limits
#define SETPOINT_MIN 10.0f
#define SETPOINT_MAX 35.0f
#define SETPOINT_STEP 0.5f

#define HYSTERESIS_MIN 1.0f
#define HYSTERESIS_MAX 5.0f
#define HYSTERESIS_STEP 1.0f

#define TIMER_MIN 1
#define TIMER_MAX 120
#define TIMER_STEP 1

// --- ESP-NOW CONFIG ---
#define ESPNOW_PING_INTERVAL_MS 60000
#define ESPNOW_ACK_TIMEOUT_MS   1000
#define ESPNOW_MAX_RETRIES      3
#define ESPNOW_RETRY_DELAY_MS   500

// Target MAC Address (Broadcast by default, change to specific relay MAC)
// IMPORTANT : L'encryption ne fonctionne qu'avec l'adresse MAC specifique du relais, pas en Broadcast.
const uint8_t RELAY_MAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Clefs de chiffrement ESP-NOW (16 caracteres obligatoires)
const char ESPNOW_PMK[17] = "ESPBastoPMKKey00";
const char ESPNOW_LMK[17] = "ESPBastoLMKKey00";

// ESP-NOW Commands (Controller -> Relay)
enum ESPNowCommand : uint8_t {
    CMD_HEAT_ON  = 1,
    CMD_HEAT_OFF = 2,
    CMD_PING     = 3
};

// ESP-NOW Responses (Relay -> Controller)
enum ESPNowResponse : uint8_t {
    ACK_ON       = 11,
    ACK_OFF      = 12,
    ACK_PONG     = 13,
    ACK_LOCKED   = 14,
    ACK_UNLOCKED = 15
};

// --- APPLICATION STATE ---

enum OperatingMode : uint8_t {
    MODE_NONE = 0,
    MODE_A_THERMOSTAT = 1, // Thermostat Hystérésis
    MODE_B_TIMER = 2,      // Minuteur
    MODE_C_SETPOINT = 3    // Consigne Simple
};

struct AppState {
    OperatingMode currentMode;
    bool isHeating;
    bool isLocked;
    bool relayConnected;
    uint8_t pingFailures;

    float currentTemp;
    float currentHumidity;
    bool sensorError;
    uint32_t sensorErrorStartTime;

    // Settings
    float setpoint;
    float hysteresis;
    uint16_t timerMinutes;
    uint16_t timerRemainingSecs;
    
    // UI State
    bool screenAwake;
    uint32_t lastActivityTime;
};

extern AppState state;

// --- RETRO-FUTURISTIC COLORS (TFT_eSPI RGB565) ---
// Rendu visuel type "cockpit rétro-futuriste" défini dans les specs
#define COLOR_BG        0x0821 // Fond anthracite très sombre
#define COLOR_TEXT      0x07FF // Texte cyan clair
#define COLOR_ACCENT    0xFC00 // Boutons action orange/ambre
#define COLOR_ON        0xF800 // Rouge chaud / Orange feu
#define COLOR_OFF       0x7BEF // Gris sombre
#define COLOR_OK        0x07E0 // Vert / Cyan
#define COLOR_ALERT     0xF800 // Rouge de danger
#define COLOR_LOCKED    0xFFE0 // Jaune / Ambre

// --- FUNCTION PROTOTYPES POUR LES SOUS-MODULES ---
// Déclarations pour l'orchestration dans main.cpp
void display_ui_init();
void display_ui_loop();
void display_ui_wake();
void display_ui_sleep();

void touch_ui_init();
void touch_ui_loop();

void sensors_init();
void sensors_loop();

void espnow_link_init();
void espnow_link_loop();
void espnow_send_command(ESPNowCommand cmd);
