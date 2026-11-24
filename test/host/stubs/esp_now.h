/* Host-test ESP-NOW. The registered receive callback is kept so the test can
   deliver packets exactly as the Wi-Fi task would. Sends are recorded. */
#pragma once

#include <Arduino.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_ESPNOW_EXIST 0x306b

typedef void (*esp_now_recv_cb_t)(const uint8_t *mac, const uint8_t *data, int len);

typedef struct {
    uint8_t peer_addr[6];
    uint8_t channel;
    bool encrypt;
} esp_now_peer_info_t;

esp_err_t esp_now_init();
esp_err_t esp_now_register_recv_cb(esp_now_recv_cb_t cb);
esp_err_t esp_now_add_peer(const esp_now_peer_info_t *peer);
esp_err_t esp_now_send(const uint8_t *mac, const uint8_t *data, size_t len);

extern esp_now_recv_cb_t hostEspNowReceive;
extern int hostEspNowSends;
extern uint8_t hostLastSendMac[6];
extern uint8_t hostLastSend[128];
extern size_t hostLastSendLen;
