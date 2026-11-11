#pragma once

#include <stdint.h>

/* Commands and replies between the Heltec and the Waveshare.
   Not a sensor packet and not a beacon. A command is only a request. */

#define AEROSENTRY_LINK_MAGIC 0xA7

#define LINK_KIND_COMMAND 1
#define LINK_KIND_ACK     2
#define LINK_KIND_STATUS  3

#define REMOTE_PURGE        1
#define REMOTE_DRYER_MANUAL 2
#define REMOTE_DRYER_AUTO   3
#define REMOTE_RECHARGE     4
#define REMOTE_STOP         5

#define LINK_FLAG_PUMP     0x01
#define LINK_FLAG_HEATER   0x02
#define LINK_FLAG_FAN      0x04
#define LINK_FLAG_TO       0x08
#define LINK_FLAG_FROM     0x10
#define LINK_FLAG_CHAMBER  0x20
#define LINK_FLAG_RUNNING  0x40
#define LINK_FLAG_RELAYS   0x80

struct __attribute__((packed)) LinkCommand {
    uint8_t magic;
    uint8_t kind;
    uint8_t command;
    uint8_t dryerOnMinutes;
    uint8_t dryerOffHours;
    uint8_t dryerTargetRH;
    uint16_t sequence;
};

struct __attribute__((packed)) LinkAck {
    uint8_t magic;
    uint8_t kind;
    uint8_t command;
    uint8_t accepted;
    uint16_t sequence;
    char text[80];
};

struct __attribute__((packed)) LinkStatus {
    uint8_t magic;
    uint8_t kind;
    uint8_t mode;
    uint8_t flags;
    int16_t toRhCenti;
    int16_t fromRhCenti;
    int16_t chamberFTimes10;
    int16_t chipCTimes10;
    uint16_t sequence;
};
