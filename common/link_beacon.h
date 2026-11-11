#pragma once

#include <stdint.h>

/* Hear-test packet from the Heltec. Not a sensor reading and not a command.
   The magic byte keeps it from being parsed as a SensorPacket. */

#define AEROSENTRY_BEACON_MAGIC 0xB4
#define AEROSENTRY_BEACON_KIND  1

struct __attribute__((packed)) LinkBeacon {
    uint8_t magic;
    uint8_t kind;
    uint16_t reserved;
    uint32_t sequence;
};
