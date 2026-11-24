#pragma once

#include <esp_now.h>

typedef enum { WIFI_PS_NONE = 0 } wifi_ps_type_t;
typedef enum { WIFI_SECOND_CHAN_NONE = 0 } wifi_second_chan_t;

inline esp_err_t esp_wifi_set_ps(wifi_ps_type_t) { return ESP_OK; }
inline esp_err_t esp_wifi_set_channel(uint8_t, wifi_second_chan_t) { return ESP_OK; }
