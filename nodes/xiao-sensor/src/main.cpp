/* AeroSentry XIAO ESP32-C3 sensor node.
   Reads two SHT31-D sensors and one 100K NTC 3950, then broadcasts
   the shared packet. Missing sensors are sent with their flag clear. */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <math.h>
#include "config.h"

static uint32_t sequenceNumber = 0;
static bool radioReady = false;

static uint8_t crc8(const uint8_t *data, int length) {
    uint8_t crc = 0xFF;
    for (int i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x80) crc = (uint8_t)((crc << 1) ^ 0x31);
            else crc <<= 1;
        }
    }
    return crc;
}

static int16_t toCenti(float value) {
    float scaled = value * 100.0f;
    if (scaled > 32767.0f) return 32767;
    if (scaled < -32768.0f) return -32768;
    return (int16_t)lroundf(scaled);
}

static bool readSht31(uint8_t address, float &humidity, float &tempC) {
    Wire.beginTransmission(address);
    Wire.write(0x24);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) return false;

    delay(20);
    if (Wire.requestFrom(address, (uint8_t)6) != 6) return false;

    uint8_t data[6];
    for (int i = 0; i < 6; ++i) data[i] = Wire.read();
    if (crc8(data, 2) != data[2] || crc8(data + 3, 2) != data[5]) return false;

    uint16_t rawTemp = ((uint16_t)data[0] << 8) | data[1];
    uint16_t rawHumidity = ((uint16_t)data[3] << 8) | data[4];
    tempC = -45.0f + 175.0f * ((float)rawTemp / 65535.0f);
    humidity = 100.0f * ((float)rawHumidity / 65535.0f);
    return humidity >= 0.0f && humidity <= 100.5f && tempC >= -40.0f && tempC <= 125.0f;
}

static bool readChamber(float &tempC) {
    int low = 4096;
    int high = 0;
    long total = 0;
    for (int i = 0; i < 5; ++i) {
        int sample = analogReadMilliVolts(THERMISTOR_ADC_PIN);
        if (sample < low) low = sample;
        if (sample > high) high = sample;
        total += sample;
        delay(4);
    }
    if ((high - low) > 12) return false;

    int millivolts = (int)(total / 5);
    if (millivolts < 80 || millivolts > 3100) return false;

    float volts = millivolts / 1000.0f;
    float resistance = NTC_SERIES_OHMS * (NTC_VCC - volts) / volts;
    if (resistance < 100.0f || resistance > 2000000.0f) return false;

    float t0Kelvin = NTC_T0_C + 273.15f;
    float invKelvin = (1.0f / t0Kelvin) + logf(resistance / NTC_R0_OHMS) / NTC_BETA;
    tempC = (1.0f / invKelvin) - 273.15f;
    return tempC >= -20.0f && tempC <= 150.0f;
}

static bool radioBegin() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_ps(WIFI_PS_NONE);
    if (esp_wifi_set_channel(AEROSENTRY_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
        return false;
    }
    if (esp_now_init() != ESP_OK) return false;

    esp_now_peer_info_t peer = {};
    memset(&peer, 0, sizeof(peer));
    for (int i = 0; i < 6; ++i) peer.peer_addr[i] = 0xFF;
    peer.channel = AEROSENTRY_ESPNOW_CHANNEL;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) return false;
    return true;
}

void setup() {
    Serial.begin(115200);
    delay(800);
    Serial.println(F("\n=== AeroSentry XIAO sensor node ==="));

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    pinMode(THERMISTOR_ADC_PIN, INPUT);
    analogReadResolution(12);
    analogSetPinAttenuation(THERMISTOR_ADC_PIN, ADC_11db);

    radioReady = radioBegin();
    Serial.println(radioReady ? F("ESP-NOW broadcast ready") : F("ESP-NOW init failed"));
    Serial.printf("Channel %d  MAC %s\n", AEROSENTRY_ESPNOW_CHANNEL, WiFi.macAddress().c_str());
    Serial.println(F("To-Engine SHT31 ADDR to GND (0x44)"));
    Serial.println(F("From-Engine SHT31 ADDR to 3V3 (0x45)"));
    Serial.println(F("Thermistor: 3V3 -- NTC -- D1 -- 10k -- GND"));
}

void loop() {
    SensorPacket packet = {};
    packet.version = AEROSENTRY_PACKET_VERSION;
    packet.sequence = sequenceNumber++;

    float humidity = 0.0f;
    float tempC = 0.0f;
    if (readSht31(SHT31_ADDR_TO, humidity, tempC)) {
        packet.flags |= SENSOR_FLAG_TO_VALID;
        packet.toEngineRhCenti = toCenti(humidity);
        packet.toEngineTempCenti = toCenti(tempC);
    }
    if (readSht31(SHT31_ADDR_FROM, humidity, tempC)) {
        packet.flags |= SENSOR_FLAG_FROM_VALID;
        packet.fromEngineRhCenti = toCenti(humidity);
        packet.fromEngineTempCenti = toCenti(tempC);
    }
    static uint8_t chamberStable = 0;
    if (readChamber(tempC)) {
        if (chamberStable < 3) chamberStable++;
    } else {
        chamberStable = 0;
    }
    if (chamberStable >= 3) {
        packet.flags |= SENSOR_FLAG_CHAMBER_VALID;
        packet.chamberTempCenti = toCenti(tempC);
    }

    bool sent = false;
    if (radioReady) {
        uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        sent = esp_now_send(broadcast, (uint8_t *)&packet, sizeof(packet)) == ESP_OK;
    }

    Serial.printf("seq %lu flags 0x%02X sent %d\n",
                  (unsigned long)packet.sequence, packet.flags, sent ? 1 : 0);
    delay(SEND_INTERVAL_MS);
}
