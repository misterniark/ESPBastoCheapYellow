#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include "config.h"

Adafruit_AHTX0 aht;
bool aht_initialized = false;

// Variables pour le lissage (Exponential Moving Average)
const float EMA_ALPHA = 0.2f; // Facteur de lissage (20% nouvelle valeur, 80% ancienne)
float smoothed_temp = 0.0f;
float smoothed_hum = 0.0f;
bool first_read = true;

uint32_t last_sensor_read = 0;

void sensors_init() {
    Serial.println("[Sensors] Initialisation I2C sur CN1 (SDA:27, SCL:22)");
    Wire.begin(I2C_SDA, I2C_SCL);

    if (aht.begin(&Wire, 0, 0x38)) {
        Serial.println("[Sensors] AHT21 trouve et initialise !");
        aht_initialized = true;
        state.sensorError = false;
    } else {
        Serial.println("[Sensors] ERREUR : AHT21 introuvable !");
        aht_initialized = false;
        state.sensorError = true;
        state.sensorErrorStartTime = millis();
    }
}

void sensors_loop() {
    uint32_t now = millis();

    // Lecture toutes les 2 secondes (SENSOR_READ_MS)
    if (now - last_sensor_read >= SENSOR_READ_MS) {
        last_sensor_read = now;

        if (!aht_initialized) {
            // Tentative de reconnexion
            if (aht.begin(&Wire, 0, 0x38)) {
                aht_initialized = true;
                state.sensorError = false;
                Serial.println("[Sensors] AHT21 reconnecte !");
            } else {
                if (!state.sensorError) {
                    state.sensorError = true;
                    state.sensorErrorStartTime = now;
                    Serial.println("[Sensors] Perte de connexion AHT21 !");
                }
                return;
            }
        }

        sensors_event_t humidity, temp;
        // La lecture AHTX0 retourne true si succes
        if (aht.getEvent(&humidity, &temp)) {
            float t = temp.temperature;
            float h = humidity.relative_humidity;

            // Exclusion de valeurs physiquement aberrantes
            if (t > -40.0f && t < 120.0f) {
                if (first_read) {
                    smoothed_temp = t;
                    smoothed_hum = h;
                    first_read = false;
                } else {
                    smoothed_temp = (EMA_ALPHA * t) + ((1.0f - EMA_ALPHA) * smoothed_temp);
                    smoothed_hum = (EMA_ALPHA * h) + ((1.0f - EMA_ALPHA) * smoothed_hum);
                }

                state.currentTemp = smoothed_temp;
                state.currentHumidity = smoothed_hum;
                
                // RAZ de l'erreur
                if (state.sensorError) {
                    state.sensorError = false;
                    Serial.println("[Sensors] Lecture OK, erreur effacee.");
                }
            } else {
                Serial.println("[Sensors] Valeurs aberrantes ignorees.");
            }
        } else {
            Serial.println("[Sensors] Echec de la lecture AHT21.");
            if (!state.sensorError) {
                state.sensorError = true;
                state.sensorErrorStartTime = now;
            }
        }
    }
}
