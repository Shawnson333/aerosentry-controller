/* AeroSentry sensor node for the no-display Heltec WiFi LoRa 32 V4.3.
   Two SHT40 probes and one 100K NTC, sent as the existing SensorPacket. */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <math.h>
#include <HX711.h>
#include "config.h"
#include "sensor_packet.h"

static TwoWire toBus = TwoWire(0);
static TwoWire fromBus = TwoWire(1);
static HX711 pressureScale;
static bool pressureReady = false;
static int32_t pressureRaw = 0;
static uint32_t sequenceNumber = 0;
static bool radioReady = false;
static int chamberMillivolts = -1;
static int chamberSpread = -1;
static int valveMillivolts = -1;
static uint8_t chamberStable = 0;
static uint8_t chamberFaults = 0;

static uint8_t crc8(const uint8_t *data, int length) {
    uint8_t crc = 0xFF;
    for (int i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (crc << 1);
        }
    }
    return crc;
}

static int16_t toCenti(float value) {
    const float scaled = value * 100.0f;
    if (scaled > 32767.0f) return 32767;
    if (scaled < -32768.0f) return -32768;
    return (int16_t)lroundf(scaled);
}

static bool readSht40(TwoWire &bus, float &humidity, float &tempC) {
    const uint8_t addresses[] = {0x44, 0x45};
    for (uint8_t address : addresses) {
        bus.beginTransmission(address);
        bus.write(0xFD);  // High-precision measurement, heater off.
        if (bus.endTransmission() != 0) continue;
        delay(10);
        if (bus.requestFrom(address, (uint8_t)6) != 6) continue;
        uint8_t data[6];
        for (int i = 0; i < 6; ++i) data[i] = bus.read();
        if (crc8(data, 2) != data[2] || crc8(data + 3, 2) != data[5]) continue;

        const uint16_t rawTemp = ((uint16_t)data[0] << 8) | data[1];
        const uint16_t rawHumidity = ((uint16_t)data[3] << 8) | data[4];
        tempC = -45.0f + 175.0f * ((float)rawTemp / 65535.0f);
        humidity = -6.0f + 125.0f * ((float)rawHumidity / 65535.0f);
        humidity = constrain(humidity, 0.0f, 100.0f);
        return humidity >= 0.0f && humidity <= 100.0f &&
               tempC >= -40.0f && tempC <= 125.0f;
    }
    return false;
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

    const float volts = millivolts / 1000.0f;
    const float resistance = NTC_SERIES_OHMS * (NTC_VCC - volts) / volts;
    if (resistance < 100.0f || resistance > 2000000.0f) return false;
    const float invKelvin = (1.0f / (NTC_T0_C + 273.15f)) +
                            logf(resistance / NTC_R0_OHMS) / NTC_BETA;
    tempC = (1.0f / invKelvin) - 273.15f;
    return tempC >= -20.0f && tempC <= 150.0f;
}

/* Trimmed-mean read of the valve shunt. No spread gate here: a switching
   transient is real for an instant, and the Waveshare decides what the
   reading means based on whether the valve is commanded energized. */
static void readValveCurrent() {
    const int sampleCount = 8;
    int samples[sampleCount];
    for (int i = 0; i < sampleCount; ++i) {
        samples[i] = analogReadMilliVolts(VALVE_SHUNT_ADC_PIN);
        delay(2);
    }
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
    valveMillivolts = (int)(total / (sampleCount - 2));
}

/* Vent pressure. The module reports a differential count that grows when the
   line is pressurised. The absolute number depends on the module and the
   plumbing, so the Waveshare learns its own baseline and judges the step.
   A failed read is reported as not-valid rather than as a zero.
   The HX711 API: begin() returns void, read() blocks until data is ready and
   returns the raw count. */
static bool readPressure(int32_t &raw) {
    if (!pressureReady) return false;
    if (!pressureScale.is_ready()) {
        pressureScale.begin(PRESSURE_DOUT_PIN, PRESSURE_SCK_PIN);
        pressureReady = pressureScale.is_ready();
        if (!pressureReady) return false;
    }
    const long value = pressureScale.read();
    raw = (int32_t)value;
    return true;
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
    for (int i = 0; i < 6; ++i) peer.peer_addr[i] = 0xFF;
    peer.channel = AEROSENTRY_ESPNOW_CHANNEL;
    peer.encrypt = false;
    return esp_now_add_peer(&peer) == ESP_OK;
}

void setup() {
    Serial.begin(115200);
    delay(800);
    Serial.println(F("\n=== AeroSentry Heltec V4.3 sensor node ==="));

    toBus.begin(TO_SDA_PIN, TO_SCL_PIN);
    toBus.setClock(100000);
    fromBus.begin(FROM_SDA_PIN, FROM_SCL_PIN);
    fromBus.setClock(100000);
    pinMode(THERMISTOR_ADC_PIN, INPUT);
    analogReadResolution(12);
    analogSetPinAttenuation(THERMISTOR_ADC_PIN, ADC_11db);
    pinMode(VALVE_SHUNT_ADC_PIN, INPUT);
    analogSetPinAttenuation(VALVE_SHUNT_ADC_PIN, ADC_11db);

    pressureScale.begin(PRESSURE_DOUT_PIN, PRESSURE_SCK_PIN);
    pressureReady = pressureScale.is_ready();
    Serial.printf("Vent pressure DOUT %d SCK %d %s\n",
                  PRESSURE_DOUT_PIN, PRESSURE_SCK_PIN,
                  pressureReady ? "ready" : "NOT READY");

    radioReady = radioBegin();
    Serial.printf("Channel %d  MAC %s  %s\n",
                  AEROSENTRY_ESPNOW_CHANNEL, WiFi.macAddress().c_str(),
                  radioReady ? "ready" : "FAILED");
    Serial.println(F("To-Engine 17/18, From-Engine 15/16, thermistor GPIO 4, valve shunt GPIO 3"));
}

void loop() {
    readValveCurrent();
    SensorPacket packet = {};
    packet.version = AEROSENTRY_PACKET_VERSION;
    packet.sequence = sequenceNumber++;

    float toHumidity = 0.0f;
    float fromHumidity = 0.0f;
    float tempC = 0.0f;
    if (readSht40(toBus, toHumidity, tempC)) {
        packet.flags |= SENSOR_FLAG_TO_VALID;
        packet.toEngineRhCenti = toCenti(toHumidity);
        packet.toEngineTempCenti = toCenti(tempC);
    }
    if (readSht40(fromBus, fromHumidity, tempC)) {
        packet.flags |= SENSOR_FLAG_FROM_VALID;
        packet.fromEngineRhCenti = toCenti(fromHumidity);
        packet.fromEngineTempCenti = toCenti(tempC);
    }

    if (CHAMBER_DIVIDER_INSTALLED && readChamber(tempC)) {
        chamberFaults = 0;
        if (chamberStable < 3) chamberStable++;
    } else {
        if (chamberFaults < 3) chamberFaults++;
        if (chamberFaults >= 3) chamberStable = 0;
    }
    if (chamberStable >= 3 && chamberFaults == 0) {
        packet.flags |= SENSOR_FLAG_CHAMBER_VALID;
        packet.chamberTempCenti = toCenti(tempC);
    }
    packet.valveAdcMillivolts = (uint16_t)(valveMillivolts > 0 ? valveMillivolts : 0);

    int32_t raw = 0;
    if (readPressure(raw)) {
        packet.flags |= SENSOR_FLAG_PRESSURE_VALID;
        packet.ventPressureRaw = raw;
        pressureRaw = raw;
    } else {
        packet.ventPressureRaw = 0;
    }

    uint8_t sent = 0;
    if (radioReady) {
        const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        for (uint8_t copy = 0; copy < SEND_REPEAT_COUNT; ++copy) {
            if (copy > 0) delay(SEND_REPEAT_GAP_MS);
            if (esp_now_send(broadcast, (uint8_t *)&packet, sizeof(packet)) == ESP_OK) sent++;
        }
    }
    Serial.printf("%lu ms seq %lu flags 0x%02X toRH %.2f fromRH %.2f chamberC %.1f mV %d spread %d valveMv %d pressRaw %ld stable %u faults %u sent %d\n",
                  (unsigned long)millis(), (unsigned long)packet.sequence, packet.flags,
                  (packet.flags & SENSOR_FLAG_TO_VALID) ? toHumidity : -1.0f,
                  (packet.flags & SENSOR_FLAG_FROM_VALID) ? fromHumidity : -1.0f,
                  (packet.flags & SENSOR_FLAG_CHAMBER_VALID) ? tempC : -1.0f,
                  chamberMillivolts, chamberSpread, valveMillivolts,
                  (long)pressureRaw,
                  chamberStable, chamberFaults, sent);
    delay(SEND_INTERVAL_MS);
}
