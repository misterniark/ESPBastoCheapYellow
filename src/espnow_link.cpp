#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "config.h"

// Variables locales
uint32_t last_ping_time = 0;
bool waiting_ack = false;
uint32_t ack_wait_start = 0;
uint8_t retry_count = 0;
ESPNowCommand pending_command;
static constexpr uint8_t RX_QUEUE_SIZE = 4;
volatile uint8_t _rx_queue[RX_QUEUE_SIZE] = {0};
volatile uint8_t _rx_head = 0;
volatile uint8_t _rx_tail = 0;
volatile bool _rx_overflow = false;

// Callback envoi
void OnDataSent(const wifi_tx_info_t *mac_addr, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        // Envoi reussi physiquement, mais on attendra l'ACK applicatif de reception si necessaire
        Serial.println("[ESPNOW] Paquet envoye avec succes.");
    } else {
        Serial.println("[ESPNOW] Echec d'envoi du paquet.");
    }
}

static bool isBroadcastMac(const uint8_t *mac) {
    const uint8_t broadcast[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return memcmp(mac, broadcast, sizeof(broadcast)) == 0;
}

// Callback reception (contexte WiFi task — ne touche que le buffer)
void OnDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *incomingData, int len) {
    if (len != sizeof(uint8_t)) {
        return;
    }

    if (!isBroadcastMac(RELAY_MAC) && memcmp(esp_now_info->src_addr, RELAY_MAC, 6) != 0) {
        return;
    }

    uint8_t next_head = (uint8_t)((_rx_head + 1) % RX_QUEUE_SIZE);
    if (next_head == _rx_tail) {
        _rx_overflow = true;
        return;
    }

    _rx_queue[_rx_head] = incomingData[0];
    _rx_head = next_head;
}

static ESPNowResponse expectedAckFor(ESPNowCommand cmd) {
    switch (cmd) {
        case CMD_HEAT_ON:  return ACK_ON;
        case CMD_HEAT_OFF: return ACK_OFF;
        case CMD_PING:     return ACK_PONG;
        default:           return (ESPNowResponse)0;
    }
}

static void processResponse(ESPNowResponse resp) {
    Serial.printf("[ESPNOW] Reponse recue : %d\n", resp);

    if (!state.relayConnected) {
        state.relayConnected = true;
        Serial.println("[ESPNOW] Connexion relais retablie.");
    }

    bool matched = waiting_ack && (resp == expectedAckFor(pending_command));
    if (matched) {
        waiting_ack = false;
        retry_count = 0;
    }

    switch (resp) {
        case ACK_ON:
            if (matched) {
                Serial.println("[ESPNOW] Relais confirme : ON");
                state.isHeating = true;
                state.heatingRequested = true;
            } else {
                Serial.println("[ESPNOW] ACK_ON ignore (stale ou hors sequence).");
            }
            break;
        case ACK_OFF:
            if (matched) {
                Serial.println("[ESPNOW] Relais confirme : OFF");
                state.isHeating = false;
                state.heatingRequested = false;
                if (state.modeStopPending) {
                    finalize_mode_stop();
                }
            } else {
                Serial.println("[ESPNOW] ACK_OFF ignore (stale ou hors sequence).");
            }
            break;
        case ACK_PONG:
            Serial.println("[ESPNOW] Relais confirme : PONG");
            state.pingFailures = 0;
            break;
        case ACK_LOCKED:
            Serial.println("[ESPNOW] Relais confirme : VERROUILLE (LOCK)");
            state.isLocked = true;
            state.lastActivityTime = millis();
            display_ui_cancel_sleep_hint();
            waiting_ack = false;
            retry_count = 0;
            state.modeStopPending = false;

            if (state.currentMode != MODE_NONE) {
                state.currentMode = MODE_NONE;
                state.timerRemainingSecs = 0;
                state.isHeating = false;
                state.heatingRequested = false;
                saveSettings();
            }
            change_view(VIEW_MENU);
            break;
        case ACK_UNLOCKED:
            Serial.println("[ESPNOW] Relais confirme : DEVERROUILLE");
            state.isLocked = false;
            state.lastActivityTime = millis();
            display_ui_cancel_sleep_hint();
            break;
        default:
            Serial.println("[ESPNOW] Reponse inconnue.");
            break;
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

    last_ping_time = millis();
    espnow_send_command(CMD_PING);
}

bool espnow_send_command(ESPNowCommand cmd) {
    if (waiting_ack) {
        if (pending_command == CMD_PING) {
            Serial.printf("[ESPNOW] PING abandonne (supersede par cmd %d)\n", cmd);
        } else {
            Serial.printf("[ESPNOW] Cmd %d abandonnee (supersede par cmd %d)\n", pending_command, cmd);
        }
        waiting_ack = false;
        retry_count = 0;
    }

    uint8_t payload = (uint8_t)cmd;
    esp_err_t result = esp_now_send(RELAY_MAC, &payload, sizeof(payload));

    if (result == ESP_OK) {
        Serial.printf("[ESPNOW] Commande envoyee : %d\n", cmd);
        pending_command = cmd;
        waiting_ack = true;
        ack_wait_start = millis();
        retry_count = 0;
        return true;
    } else {
        Serial.println("[ESPNOW] Erreur a l'envoi de la commande.");
        return false;
    }
}

void espnow_link_loop() {
    uint32_t now = millis();

    // Traitement thread-safe des réponses bufferisées
    while (_rx_tail != _rx_head) {
        ESPNowResponse resp = (ESPNowResponse)_rx_queue[_rx_tail];
        _rx_tail = (uint8_t)((_rx_tail + 1) % RX_QUEUE_SIZE);
        processResponse(resp);
    }

    if (_rx_overflow) {
        _rx_overflow = false;
        Serial.println("[ESPNOW] Attention: file RX pleine, au moins une reponse a ete perdue.");
    }

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
                Serial.println("[ESPNOW] Timeout de la commande (3 retries echoues).");
                waiting_ack = false;
                
                if (pending_command == CMD_PING) {
                    state.pingFailures++;
                    if (state.pingFailures >= 3) {
                        state.relayConnected = false;
                        Serial.println("[ESPNOW] ALERTE : Connexion Relais Perdue !");
                    }
                } else if (pending_command == CMD_HEAT_ON || pending_command == CMD_HEAT_OFF) {
                    if (pending_command == CMD_HEAT_OFF) {
                        cancel_mode_stop();
                    }
                    state.heatingRequested = state.isHeating;
                    Serial.printf("[ESPNOW] Intention chauffage reconciliee avec etat confirme (%d)\n", state.isHeating);
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
