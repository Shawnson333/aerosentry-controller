/* Heltec V4.3 sensor bench test.
   Two SHT40 humidity probes on separate I2C buses, one thermistor.
   Serial output only. No radio, no commands, no relays. */

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

// --- I2C buses ---
#define BUS_A_SDA 17  // To-Engine SHT40
#define BUS_A_SCL 18
#define BUS_B_SDA 15  // From-Engine SHT40
#define BUS_B_SCL 16

// --- Thermistor ---
#define NTC_PIN 4
#define NTC_SERIES_OHMS 10000.0f
#define NTC_R0_OHMS 100000.0f
#define NTC_BETA 3950.0f
#define NTC_T0_C 25.0f
#define NTC_VCC 3.3f

static TwoWire WireA = TwoWire(0);
static TwoWire WireB = TwoWire(1);

// ---- SHT40 helpers ----

static uint8_t crc8(const uint8_t *data, int len) {
    uint8_t crc = 0xFF;
    for (int i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (crc << 1);
    }
    return crc;
}

static bool sht40Read(TwoWire &bus, uint8_t addr, float &rh, float &tempC) {
    // Measure with high precision (no heater).
    bus.beginTransmission(addr);
    bus.write(0xFD);
    if (bus.endTransmission() != 0) return false;
    delay(10);
    if (bus.requestFrom(addr, (uint8_t)6) != 6) return false;
    uint8_t buf[6];
    for (int i = 0; i < 6; ++i) buf[i] = bus.read();
    if (crc8(buf, 2) != buf[2] || crc8(buf + 3, 2) != buf[5]) return false;
    uint16_t rawT = ((uint16_t)buf[0] << 8) | buf[1];
    uint16_t rawRH = ((uint16_t)buf[3] << 8) | buf[4];
    tempC = -45.0f + 175.0f * ((float)rawT / 65535.0f);
    rh = -6.0f + 125.0f * ((float)rawRH / 65535.0f);
    rh = constrain(rh, 0.0f, 100.0f);
    return rh >= 0.0f && rh <= 100.0f && tempC >= -40.0f && tempC <= 125.0f;
}

static uint8_t scanBus(TwoWire &bus) {
    Serial.print("  scanning 0x44...");
    bus.beginTransmission(0x44);
    if (bus.endTransmission() == 0) { Serial.println(" found"); return 0x44; }
    Serial.print(" no. scanning 0x45...");
    bus.beginTransmission(0x45);
    if (bus.endTransmission() == 0) { Serial.println(" found"); return 0x45; }
    Serial.println(" no.");
    return 0;
}

static float readThermistor() {
    int samples[8];
    for (int i = 0; i < 8; ++i) {
        samples[i] = analogReadMilliVolts(NTC_PIN);
        delay(2);
    }
    // trimmed mean
    for (int i = 1; i < 8; ++i) {
        int v = samples[i];
        int j = i;
        while (j > 0 && samples[j-1] > v) { samples[j] = samples[j-1]; --j; }
        samples[j] = v;
    }
    long total = 0;
    for (int i = 1; i < 7; ++i) total += samples[i];
    int mV = total / 6;
    // Print raw diagnostic
    Serial.printf("  NTC raw mV: %d  (min %d, max %d)\n", mV, samples[0], samples[7]);
    if (mV < 80 || mV > 3100) return NAN;
    float volts = mV / 1000.0f;
    float r = NTC_SERIES_OHMS * (NTC_VCC - volts) / volts;
    if (r < 100.0f || r > 2000000.0f) {
        Serial.printf("  NTC resistance out of range: %.0f ohms\n", r);
        return NAN;
    }
    float invK = (1.0f / (NTC_T0_C + 273.15f)) + logf(r / NTC_R0_OHMS) / NTC_BETA;
    return (1.0f / invK) - 273.15f;
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println(F("\n=== Heltec V4.3 Sensor Bench Test ==="));
    Serial.println(F("Bus A: GPIO 17/18 (To-Engine SHT40)"));
    Serial.println(F("Bus B: GPIO 15/16 (From-Engine SHT40)"));
    Serial.println(F("Thermistor: GPIO 4"));

    pinMode(NTC_PIN, INPUT);
    analogReadResolution(12);
    analogSetPinAttenuation(NTC_PIN, ADC_11db);

    Serial.print("Bus A... ");
    WireA.begin(BUS_A_SDA, BUS_A_SCL);
    WireA.setClock(100000);
    Serial.println("started");

    Serial.print("Bus B... ");
    WireB.begin(BUS_B_SDA, BUS_B_SCL);
    WireB.setClock(100000);
    Serial.println("started");
}

void loop() {
    delay(3000);

    float rh = NAN, tc = NAN;

    // Bus A
    Serial.println("--- Bus A (GPIO 17/18) ---");
    uint8_t addrA = scanBus(WireA);
    if (addrA) {
        if (sht40Read(WireA, addrA, rh, tc)) {
            Serial.printf("  SHT40 at 0x%02X: %.2f%% RH, %.2f C (%.1f F)\n",
                          addrA, rh, tc, tc * 9.0f/5.0f + 32.0f);
        } else {
            Serial.println("  CRC/read failed");
        }
    }

    // Bus B
    Serial.println("--- Bus B (GPIO 15/16) ---");
    uint8_t addrB = scanBus(WireB);
    if (addrB) {
        if (sht40Read(WireB, addrB, rh, tc)) {
            Serial.printf("  SHT40 at 0x%02X: %.2f%% RH, %.2f C (%.1f F)\n",
                          addrB, rh, tc, tc * 9.0f/5.0f + 32.0f);
        } else {
            Serial.println("  CRC/read failed");
        }
    }

    // Thermistor
    float ntcC = readThermistor();
    Serial.print("Thermistor (GPIO 4): ");
    if (ntcC == ntcC) {
        Serial.printf("%.1f C (%.1f F)\n", ntcC, ntcC * 9.0f/5.0f + 32.0f);
    } else {
        Serial.println("invalid / open circuit");
    }
    Serial.println();
}
