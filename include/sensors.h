#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include "sensor_packet.h"

/* ========================================================================
   SENSOR DATA — ESP-NOW packet from the Heltec V4.3 sensor node
   Both humidity probes, the chamber thermistor, the valve shunt and the
   vent pressure module, about once a second (each packet sent twice).
   ======================================================================== */

struct SensorReading {
    float toEngineRH;
    float toEngineTemp;      // Celsius
    float fromEngineRH;
    float fromEngineTemp;    // Celsius
    float chamberTemp;       // Celsius, thermistor on the sensor node
    int valveAdcMillivolts;  // 1-ohm shunt in the valve 12 V return, from the sensor node
    int32_t ventPressureRaw; // raw counts from the vent pressure module, last valid sample
    bool pressureValid;      // true when the latest packet had a good pressure reading
    uint32_t timestamp;        // millis() when the last packet arrived
    uint32_t toTimestamp;      // millis() when the last valid To-Engine sample arrived
    uint32_t fromTimestamp;    // millis() when the last valid From-Engine sample arrived
    uint32_t chamberTimestamp; // millis() when the last valid chamber sample arrived
    uint32_t pressureTimestamp; // millis() when the last valid vent pressure sample arrived
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
// Vent pressure reading valid and no older than SAMPLE_MAX_AGE_MS.
bool pressureReady();

#endif // SENSORS_H