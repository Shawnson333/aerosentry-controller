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
static int chamberMillivolts = -1;
static int chamberSpread = -1;

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

static bool readSht31(uint8_t sda, uint8_t scl, uint8_t address,
                      float &humidity, float &tempC) {
    static int activeSda = -1;
    static int activeScl = -1;
    if (activeSda != sda || activeScl != scl) {
        if (activeSda >= 0) Wire.end();
        delay(2);
        if (!Wire.begin(sda, scl)) return false;
        Wire.setClock(100000);
        activeSda = sda;
        activeScl = scl;
    }

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
    const int sampleCount = 8;
    int samples[sampleCount];
    int low = 4096;
    int high = 0;
    for (int i = 0; i < sampleCount; ++i) {
        samples[i] = analogReadMilliVolts(THERMISTOR_ADC_PIN);
        if (samples[i] < low) low = samples[i];
        if (samples[i] > high) high = samples[i];
        delay(2);
    }
    chamberSpread = high - low;

    for (int i = 1; i < sampleCount; ++i) {
        const int value = samples[i];
        int j = i;
        while (j > 0 && samples[j - 1] > value) {
            samples[j] = samples[j - 1];
            --j;
        }
        samples[j] = value;
    }
    long total = 0;
    for (int i = 1; i < sampleCount - 1; ++i) total += samples[i];
    const int millivolts = (int)(total / (sampleCount - 2));
    chamberMillivolts = millivolts;
    if (chamberSpread > NTC_MAX_SPREAD_MV) return false;
    if (millivolts < 80 || millivolts > 3100) return false;

    float volts = millivolts / 1000.0f;
    float resistance = NTC_SERIES_OHMS * (NTC_VCC - volts) / volts;
    if (resistance < 100.0f || resistance > 2000000.0f) return false;

    float t0Kelvin = NTC_T0_C + 273.15f;
    float invKelvin = (1.0f / t0Kelvin) + logf(resistance / NTC_R0_OHMS) / NTC_BETA;
    tempC = (1.0f / invKelvin) - 273.15f;
    return tempC >= -20.0f && tempC <= 150.0f;
}

static uint8_t findSht31(uint8_t sda, uint8_t scl) {
    for (uint8_t address = 0x44; address <= 0x45; ++address) {
        float humidity = 0.0f;
        float tempC = 0.0f;
        if (readSht31(sda, scl, address, humidity, tempC)) return address;
    }
    return 0;
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

    pinMode(THERMISTOR_ADC_PIN, INPUT);
    analogReadResolution(12);
    analogSetPinAttenuation(THERMISTOR_ADC_PIN, ADC_11db);

    radioReady = radioBegin();
    Serial.println(radioReady ? F("ESP-NOW broadcast ready") : F("ESP-NOW init failed"));
    Serial.printf("Channel %d  MAC %s\n", AEROSENTRY_ESPNOW_CHANNEL, WiFi.macAddress().c_str());
    Serial.println(F("To-Engine I2C D4/D5, From-Engine I2C D8/D9, thermistor D1"));
    const uint8_t toAddr = findSht31(TO_SDA_PIN, TO_SCL_PIN);
    const uint8_t fromAddr = findSht31(FROM_SDA_PIN, FROM_SCL_PIN);
    Serial.printf("To-Engine SHT31 %s\n", toAddr ? (toAddr == 0x44 ? "0x44" : "0x45") : "not found");
    Serial.printf("From-Engine SHT31 %s\n", fromAddr ? (fromAddr == 0x44 ? "0x44" : "0x45") : "not found");
    Serial.println(F("Thermistor: 3V3 -- NTC -- D1 -- 10k -- GND"));
    Serial.println(CHAMBER_DIVIDER_INSTALLED
                       ? F("Chamber divider installed")
                       : F("Chamber reading disabled until the 10k is installed"));
}

void loop() {
    SensorPacket packet = {};
    packet.version = AEROSENTRY_PACKET_VERSION;
    packet.sequence = sequenceNumber++;

    float toHumidity = 0.0f;
    float fromHumidity = 0.0f;
    float tempC = 0.0f;
    if (readSht31(TO_SDA_PIN, TO_SCL_PIN, 0x44, toHumidity, tempC) ||
        readSht31(TO_SDA_PIN, TO_SCL_PIN, 0x45, toHumidity, tempC)) {
        packet.flags |= SENSOR_FLAG_TO_VALID;
        packet.toEngineRhCenti = toCenti(toHumidity);
        packet.toEngineTempCenti = toCenti(tempC);
    }
    if (readSht31(FROM_SDA_PIN, FROM_SCL_PIN, 0x44, fromHumidity, tempC) ||
        readSht31(FROM_SDA_PIN, FROM_SCL_PIN, 0x45, fromHumidity, tempC)) {
        packet.flags |= SENSOR_FLAG_FROM_VALID;
        packet.fromEngineRhCenti = toCenti(fromHumidity);
        packet.fromEngineTempCenti = toCenti(tempC);
    }
    static uint8_t chamberStable = 0;
    static uint8_t chamberFaults = 0;
    if (CHAMBER_DIVIDER_INSTALLED && readChamber(tempC)) {
        chamberFaults = 0;
        if (chamberStable < 3) chamberStable++;
    } else {
        if (chamberFaults < 3) chamberFaults++;
        // One or two isolated bad samples recover on the next good sample.
        // Three consecutive faults require three good samples to requalify.
        if (chamberFaults >= 3) chamberStable = 0;
    }
    if (chamberStable >= 3 && chamberFaults == 0) {
        packet.flags |= SENSOR_FLAG_CHAMBER_VALID;
        packet.chamberTempCenti = toCenti(tempC);
    }

    bool sent = false;
    if (radioReady) {
        uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        sent = esp_now_send(broadcast, (uint8_t *)&packet, sizeof(packet)) == ESP_OK;
    }

    Serial.printf("%lu ms seq %lu flags 0x%02X toRH %.1f fromRH %.1f chamberC %.1f mV %d spread %d stable %u faults %u sent %d\n",
                  (unsigned long)millis(), (unsigned long)packet.sequence, packet.flags,
                  (packet.flags & SENSOR_FLAG_TO_VALID) ? toHumidity : -1.0f,
                  (packet.flags & SENSOR_FLAG_FROM_VALID) ? fromHumidity : -1.0f,
                  (packet.flags & SENSOR_FLAG_CHAMBER_VALID) ? tempC : -1.0f,
                  chamberMillivolts, chamberSpread, chamberStable, chamberFaults, sent ? 1 : 0);
    delay(SEND_INTERVAL_MS);
}
