/* ========================================================================
   SENSORS — ESP-NOW receiver (Waveshare side)

   Stub until the XIAO ESP32-C3 bridge is connected. No packet has arrived,
   so start commands show a sensor-unavailable warning and no cycle can run.
   The packet layout lives in sensors.h.
   ======================================================================== */

#include "sensors.h"
#include "config.h"

SensorReading sensors = {};

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
    return sensorPacketFresh() && sensors.chamberValid;
}

bool toEngineSampleReady() {
    return toEngineReady() && ageOk(SAMPLE_MAX_AGE_MS);
}

bool fromEngineSampleReady() {
    return fromEngineReady() && ageOk(SAMPLE_MAX_AGE_MS);
}

bool sensorsBegin() {
    sensors = {};
    return false;
}

bool sensorsUpdate() {
    return false;
}