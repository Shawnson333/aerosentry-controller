#pragma once

#include <stdint.h>

/* Shared ESP-NOW payload. The sensor node (Heltec V4.3) is the sender and the
   Waveshare is the receiver; both must use this exact layout. Packed,
   little-endian. Humidity is centi-percent (4012 = 40.12%). Temperature is
   centi-celsius. Version 3 adds the vent pressure reading. */

#define AEROSENTRY_PACKET_VERSION 3
#define AEROSENTRY_ESPNOW_CHANNEL 1

#define SENSOR_FLAG_TO_VALID      0x01
#define SENSOR_FLAG_FROM_VALID    0x02
#define SENSOR_FLAG_CHAMBER_VALID 0x04
#define SENSOR_FLAG_PRESSURE_VALID 0x08

struct __attribute__((packed)) SensorPacket {
    uint8_t version;
    uint8_t flags;
    int16_t toEngineRhCenti;
    int16_t toEngineTempCenti;
    int16_t fromEngineRhCenti;
    int16_t fromEngineTempCenti;
    int16_t chamberTempCenti;
    uint16_t valveAdcMillivolts;  // 1-ohm shunt in the valve 12 V return
    int32_t ventPressureRaw;      // raw counts from the vent pressure module
    uint32_t sequence;
};
