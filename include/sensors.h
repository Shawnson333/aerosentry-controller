#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include "sensor_packet.h"

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
    uint32_t timestamp;        // millis() when the last packet arrived
    uint32_t chamberTimestamp; // millis() when the last valid chamber sample arrived
    bool toValid;
    bool fromValid;
    bool chamberValid;
    bool valid;              // true after at least one packet has been received
};

extern SensorReading sensors;

bool sensorsBegin();
bool sensorsUpdate();
bool sensorPacketFresh();
bool toEngineReady();
bool fromEngineReady();
bool chamberReady();
// Brief recovery window; may hold the prior heater state, never newly enable heat.
bool chamberRetryPending();
bool toEngineSampleReady();
bool fromEngineSampleReady();

#endif // SENSORS_H