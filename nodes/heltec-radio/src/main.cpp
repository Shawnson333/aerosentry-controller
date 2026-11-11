/* Heltec V4.3 hear test. Broadcasts a beacon on ESP-NOW channel 1.
   This firmware cannot start a cycle and does not touch the power amplifier. */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>
#include "link_beacon.h"
#include "sensor_packet.h"

static bool radioReady = false;
static uint32_t sequenceNumber = 0;

static bool radioBegin() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_ps(WIFI_PS_NONE);
    if (esp_wifi_set_channel(AEROSENTRY_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
        return false;
    }
    if (esp_now_init() != ESP_OK) return false;

    esp_now_peer_info_t peer = {};
    for (int i = 0; i < 6; ++i) peer.peer_addr[i] = 0xFF;
    peer.channel = AEROSENTRY_ESPNOW_CHANNEL;
    peer.encrypt = false;
    return esp_now_add_peer(&peer) == ESP_OK;
}

void setup() {
    Serial.begin(115200);
    delay(800);
    Serial.println(F("\n=== AeroSentry Heltec V4.3 beacon ==="));
    Serial.println(F("Hear test only. No commands. No relays."));
    radioReady = radioBegin();
    Serial.printf("Channel %d  MAC %s  %s\n",
                  AEROSENTRY_ESPNOW_CHANNEL,
                  WiFi.macAddress().c_str(),
                  radioReady ? "ready" : "FAILED");
}

void loop() {
    if (!radioReady) {
        delay(1000);
        return;
    }

    LinkBeacon beacon = {};
    beacon.magic = AEROSENTRY_BEACON_MAGIC;
    beacon.kind = AEROSENTRY_BEACON_KIND;
    beacon.sequence = sequenceNumber++;

    const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const esp_err_t result = esp_now_send(broadcast, (const uint8_t *)&beacon, sizeof(beacon));
    Serial.printf("sent beacon %lu %s\n",
                  (unsigned long)beacon.sequence,
                  result == ESP_OK ? "ok" : "failed");
    delay(1000);
}
