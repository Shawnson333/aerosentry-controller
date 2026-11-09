#pragma once

#include <stdint.h>

/* Shared ESP-NOW payload. Both the XIAO sender and the Waveshare receiver
   must use this exact layout. Packed, little-endian.
   Humidity is centi-percent (4012 = 40.12%). Temperature is centi-celsius. */

#define AEROSENTRY_PACKET_VERSION 1
#define AEROSENTRY_ESPNOW_CHANNEL 1

#define SENSOR_FLAG_TO_VALID      0x01
#define SENSOR_FLAG_FROM_VALID    0x02
#define SENSOR_FLAG_CHAMBER_VALID 0x04

struct __attribute__((packed)) SensorPacket {
    uint8_t version;
    uint8_t flags;
    int16_t toEngineRhCenti;
    int16_t toEngineTempCenti;
    int16_t fromEngineRhCenti;
    int16_t fromEngineTempCenti;
    int16_t chamberTempCenti;
    uint32_t sequence;
};
