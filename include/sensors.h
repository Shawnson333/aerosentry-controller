#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>

/* ========================================================================
   SENSOR DATA — ESP-NOW packet from XIAO ESP32-C3
   The XIAO sends both humidity sensors plus the chamber thermistor.
   ======================================================================== */

struct SensorReading {
    float toEngineRH;
    float toEngineTemp;      // Celsius
    float fromEngineRH;
    float fromEngineTemp;    // Celsius
    float chamberTemp;       // Celsius, thermistor on the XIAO
    uint32_t timestamp;      // millis() when the last packet arrived
    bool toValid;
    bool fromValid;
    bool chamberValid;
    bool valid;              // true after at least one packet has been received
};

/* V1 payload the XIAO should send. Packed, little-endian.
   flags bit0 = to-engine valid, bit1 = from-engine valid, bit2 = chamber valid.
   Humidity is centi-percent (4012 = 40.12%). Temperature is centi-celsius.
   Chamber temperature is the thermistor on the XIAO, not a humidity sensor. */
struct __attribute__((packed)) SensorPacket {
    uint8_t version;             // 1
    uint8_t flags;
    int16_t toEngineRhCenti;
    int16_t toEngineTempCenti;
    int16_t fromEngineRhCenti;
    int16_t fromEngineTempCenti;
    int16_t chamberTempCenti;
    uint32_t sequence;
};

extern SensorReading sensors;

bool sensorsBegin();
bool sensorsUpdate();
bool sensorPacketFresh();
bool toEngineReady();
bool fromEngineReady();
bool chamberReady();
bool toEngineSampleReady();
bool fromEngineSampleReady();

#endif // SENSORS_H