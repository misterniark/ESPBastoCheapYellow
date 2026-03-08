#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "config.h"

// Déclaration externe pour forcer le retour au menu en cas de lock
enum ScreenView { VIEW_MENU, VIEW_MODE_A, VIEW_MODE_B, VIEW_MODE_C, VIEW_ALERT_CONN, VIEW_ALERT_SENSOR };
extern void change_view(ScreenView view); // dans display_ui.cpp
extern void saveSettings();

// Variables locales
uint32_t last_ping_time = 0;
bool waiting_ack = false;
uint32_t ack_wait_start = 0;
uint8_t retry_count = 0;
ESPNowCommand pending_command;

// Callback envoi
void OnDataSent(const wifi_tx_info_t *mac_addr, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        // Envoi reussi physiquement, mais on attendra l'ACK applicatif de reception si necessaire
        Serial.println("[ESPNOW] Paquet envoye avec succes.");
    } else {
        Serial.println("[ESPNOW] Echec d'envoi du paquet.");
    }
}

// Callback reception
void OnDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *incomingData, int len) {
    if (len == sizeof(uint8_t)) {
        ESPNowResponse resp = (ESPNowResponse)incomingData[0];
        Serial.printf("[ESPNOW] Reponse recue : %d\n", resp);

        waiting_ack = false; // L'ACK a ete recu, fin des retries
        retry_count = 0;
        
        // RAZ des compteurs de pannes
        if (!state.relayConnected) {
            state.relayConnected = true;
            Serial.println("[ESPNOW] Connexion relais retablie.");
        }
        state.pingFailures = 0;

        switch (resp) {
            case ACK_ON:
                Serial.println("[ESPNOW] Relais confirme : ON");
                break;
            case ACK_OFF:
                Serial.println("[ESPNOW] Relais confirme : OFF");
                break;
            case ACK_PONG:
                Serial.println("[ESPNOW] Relais confirme : PONG");
                break;
            case ACK_LOCKED:
                Serial.println("[ESPNOW] Relais confirme : VERROUILLE (LOCK)");
                state.isLocked = true;
                
                // Arreter le thermostat/minuteur en cours
                if (state.currentMode != MODE_NONE) {
                    state.currentMode = MODE_NONE;
                    state.timerRemainingSecs = 0;
                    state.isHeating = false;
                    saveSettings();
                }
                
                // Forcer le retour au menu principal
                change_view(VIEW_MENU);
                break;
            case ACK_UNLOCKED:
                Serial.println("[ESPNOW] Relais confirme : DEVERROUILLE");
                state.isLocked = false;
                break;
            default:
                Serial.println("[ESPNOW] Reponse inconnue.");
                break;
        }
    }
}

void espnow_link_init() {
    Serial.println("[ESPNOW] Initialisation WiFi STA mode");
    WiFi.mode(WIFI_STA);
    
    // Modem sleep est recommande par les specs
    WiFi.setSleep(WIFI_PS_MIN_MODEM);

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESPNOW] Erreur init ESP-NOW");
        return;
    }

    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);

    // Init de la Primary Master Key (PMK)
    esp_now_set_pmk((uint8_t *)ESPNOW_PMK);

    // Enregistrement du pair (Relais)
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, RELAY_MAC, 6);
    peerInfo.channel = 0; // utilise channel actuel
    
    // Detection broadcast : le chiffrement ESP-NOW ne fonctionne PAS en broadcast
    const uint8_t broadcast[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    bool isBroadcast = (memcmp(RELAY_MAC, broadcast, 6) == 0);
    
    if (isBroadcast) {
        peerInfo.encrypt = false;
        Serial.println("[ESPNOW] ⚠ MAC Broadcast detectee : chiffrement DESACTIVE.");
        Serial.println("[ESPNOW]   → Remplacez RELAY_MAC par la MAC reelle du relais pour activer le chiffrement.");
    } else {
        peerInfo.encrypt = true;
        memcpy(peerInfo.lmk, ESPNOW_LMK, 16);
        Serial.println("[ESPNOW] Chiffrement ESP-NOW ACTIVE (MAC unicast).");
    }
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("[ESPNOW] Echec ajout du relai (peer)");
        return;
    }
    
    Serial.println("[ESPNOW] Init terminee.");
}

void espnow_send_command(ESPNowCommand cmd) {
    uint8_t payload = (uint8_t)cmd;
    esp_err_t result = esp_now_send(RELAY_MAC, &payload, sizeof(payload));

    if (result == ESP_OK) {
        Serial.printf("[ESPNOW] Commande envoyee : %d\n", cmd);
        pending_command = cmd;
        waiting_ack = true;
        ack_wait_start = millis();
        retry_count = 0;
    } else {
        Serial.println("[ESPNOW] Erreur à l'envoi de la commande.");
    }
}

void espnow_link_loop() {
    uint32_t now = millis();

    // Gestion de l'ACK et retry
    if (waiting_ack) {
        uint32_t current_delay = (retry_count == 0) ? ESPNOW_ACK_TIMEOUT_MS : ESPNOW_RETRY_DELAY_MS;
        
        if (now - ack_wait_start >= current_delay) {
            if (retry_count < ESPNOW_MAX_RETRIES) {
                retry_count++;
                Serial.printf("[ESPNOW] Retry %d/%d pour commande %d\n", retry_count, ESPNOW_MAX_RETRIES, pending_command);
                uint8_t payload = (uint8_t)pending_command;
                esp_now_send(RELAY_MAC, &payload, sizeof(payload));
                ack_wait_start = now;
            } else {
                Serial.println("[ESPNOW] Timeout de la commande (3 retries echooues).");
                waiting_ack = false;
                
                // Increment des echecs de ping *uniquement* si c'etait un CMD_PING
                if (pending_command == CMD_PING) {
                    state.pingFailures++;
                    if (state.pingFailures >= 3) {
                        state.relayConnected = false;
                        Serial.println("[ESPNOW] ALERTE : Connexion Relais Perdue !");
                    }
                }
            }
        }
    }

    // Ping periodique
    if (!waiting_ack && (now - last_ping_time >= ESPNOW_PING_INTERVAL_MS)) {
        last_ping_time = now;
        espnow_send_command(CMD_PING);
    }
}
