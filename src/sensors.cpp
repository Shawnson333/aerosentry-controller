/* ========================================================================
   SENSORS — ESP-NOW receiver (Waveshare side)
   Accepts the shared SensorPacket from the XIAO on a fixed channel.
   A packet with all validity flags clear still counts as a live radio.
   ======================================================================== */

#include "sensors.h"
#include "config.h"
#include "link_beacon.h"
#include "link_packet.h"
#include "radio_link.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

SensorReading sensors = {};

static portMUX_TYPE sensorMux = portMUX_INITIALIZER_UNLOCKED;
static SensorPacket latestPacket = {};
static volatile bool packetReady = false;
static LinkBeacon latestBeacon = {};
static uint8_t beaconMac[6] = {};
static volatile bool beaconReady = false;
static bool radioReady = false;

static bool ageOk(uint32_t maxAgeMs) {
    if (!sensors.valid || sensors.timestamp == 0) return false;
    return (millis() - sensors.timestamp) <= maxAgeMs;
}

bool sensorPacketFresh() {
    return ageOk(SENSOR_STALE_MS);
}

bool toEngineReady() {
    return sensorPacketFresh() && sensors.toValid;
}

bool fromEngineReady() {
    return sensorPacketFresh() && sensors.fromValid;
}

bool chamberReady() {
    if (!sensorPacketFresh() || !sensors.chamberValid || sensors.chamberTimestamp == 0) {
        return false;
    }
    return (millis() - sensors.chamberTimestamp) <= SAMPLE_MAX_AGE_MS;
}

bool chamberRetryPending() {
    if (chamberReady() || sensors.chamberTimestamp == 0) return false;
    // This also catches radio loss once the last valid chamber sample ages out.
    return (millis() - sensors.chamberTimestamp) <= CHAMBER_RETRY_MS;
}

bool toEngineSampleReady() {
    return toEngineReady() && ageOk(SAMPLE_MAX_AGE_MS);
}

bool fromEngineSampleReady() {
    return fromEngineReady() && ageOk(SAMPLE_MAX_AGE_MS);
}

static float fromCenti(int16_t value) {
    return value / 100.0f;
}

static void onEspNowReceive(const uint8_t *mac, const uint8_t *data, int len) {
    if (data == nullptr) return;

    /* Hear test only. A beacon must not update sensor state or start a cycle. */
    if (len == (int)sizeof(LinkBeacon)) {
        LinkBeacon incoming;
        memcpy(&incoming, data, sizeof(incoming));
        if (incoming.magic == AEROSENTRY_BEACON_MAGIC && incoming.kind == AEROSENTRY_BEACON_KIND) {
            portENTER_CRITICAL(&sensorMux);
            latestBeacon = incoming;
            if (mac != nullptr) memcpy(beaconMac, mac, sizeof(beaconMac));
            beaconReady = true;
            portEXIT_CRITICAL(&sensorMux);
            radioLinkNotePeer(mac);
            return;
        }
    }

    if (len >= 2 && data[0] == AEROSENTRY_LINK_MAGIC) {
        radioLinkAccept(mac, data, len);
        return;
    }

    if (len != (int)sizeof(SensorPacket)) return;

    SensorPacket incoming;
    memcpy(&incoming, data, sizeof(incoming));
    if (incoming.version != AEROSENTRY_PACKET_VERSION) return;

    portENTER_CRITICAL(&sensorMux);
    latestPacket = incoming;
    packetReady = true;
    portEXIT_CRITICAL(&sensorMux);
}

bool sensorsBegin() {
    sensors = {};
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_ps(WIFI_PS_NONE);
    if (esp_wifi_set_channel(AEROSENTRY_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
        Serial.println(F("ESP-NOW channel set failed"));
        return false;
    }
    if (esp_now_init() != ESP_OK) {
        Serial.println(F("ESP-NOW init failed"));
        return false;
    }
    if (esp_now_register_recv_cb(onEspNowReceive) != ESP_OK) {
        Serial.println(F("ESP-NOW receive callback failed"));
        return false;
    }
    radioReady = true;
    Serial.printf("ESP-NOW listening on channel %d\n", AEROSENTRY_ESPNOW_CHANNEL);
    return true;
}

bool sensorsUpdate() {
    if (radioReady && beaconReady) {
        LinkBeacon beacon;
        uint8_t mac[6] = {};
        portENTER_CRITICAL(&sensorMux);
        beacon = latestBeacon;
        memcpy(mac, beaconMac, sizeof(mac));
        beaconReady = false;
        portEXIT_CRITICAL(&sensorMux);
        Serial.printf("Heltec beacon seq %lu from %02X:%02X:%02X:%02X:%02X:%02X\n",
                      (unsigned long)beacon.sequence,
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    if (!radioReady || !packetReady) return false;

    SensorPacket packet;
    portENTER_CRITICAL(&sensorMux);
    packet = latestPacket;
    packetReady = false;
    portEXIT_CRITICAL(&sensorMux);

    sensors.toValid = (packet.flags & SENSOR_FLAG_TO_VALID) != 0;
    sensors.fromValid = (packet.flags & SENSOR_FLAG_FROM_VALID) != 0;
    sensors.chamberValid = (packet.flags & SENSOR_FLAG_CHAMBER_VALID) != 0;
    if (sensors.toValid) {
        sensors.toEngineRH = fromCenti(packet.toEngineRhCenti);
        sensors.toEngineTemp = fromCenti(packet.toEngineTempCenti);
    }
    if (sensors.fromValid) {
        sensors.fromEngineRH = fromCenti(packet.fromEngineRhCenti);
        sensors.fromEngineTemp = fromCenti(packet.fromEngineTempCenti);
    }
    if (sensors.chamberValid) {
        sensors.chamberTemp = fromCenti(packet.chamberTempCenti);
        sensors.chamberTimestamp = millis();
    }
    sensors.timestamp = millis();
    sensors.valid = true;

    const uint32_t logTime = millis();
    Serial.printf("%lu ms ESP-NOW seq %lu flags 0x%02X",
                  (unsigned long)logTime, (unsigned long)packet.sequence, packet.flags);
    if (sensors.chamberValid) {
        Serial.printf(" chamber %.1fF", sensors.chamberTemp * 9.0f / 5.0f + 32.0f);
    } else if (chamberRetryPending()) {
        Serial.print(F(" chamber retry"));
    } else {
        Serial.print(F(" chamber --"));
    }
    if (sensors.chamberTimestamp != 0) {
        Serial.printf(" age %lu ms", (unsigned long)(logTime - sensors.chamberTimestamp));
    }
    Serial.println();
    return true;
}