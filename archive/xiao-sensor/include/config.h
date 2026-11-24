#ifndef XIAO_CONFIG_H
#define XIAO_CONFIG_H

#include <Arduino.h>
#include "sensor_packet.h"

/* XIAO ESP32-C3. The chip has one I2C controller, so the two buses are
   selected one at a time. Both SHT31 boards may use the same ADDR pin. */
#define TO_SDA_PIN          6    // D4, To-Engine SDA
#define TO_SCL_PIN          7    // D5, To-Engine SCL
#define FROM_SDA_PIN        8    // D8, From-Engine SDA
#define FROM_SCL_PIN        9    // D9, From-Engine SCL
#define THERMISTOR_ADC_PIN  3    // D1

/* Tried on each bus. ADDR to GND is 0x44. ADDR to 3V3 is 0x45. */

/* 3V3 -- thermistor -- D1 -- 10k -- GND.
   Keep this 1 only while the external pulldown is installed. A missing pulldown
   can look like a real temperature, so Recharge requires the chamber-valid flag. */
#define CHAMBER_DIVIDER_INSTALLED  1
#define NTC_SERIES_OHMS     10000.0f
#define NTC_R0_OHMS         100000.0f
#define NTC_BETA            3950.0f
#define NTC_T0_C            25.0f
#define NTC_VCC             3.3f
/* Quiet bench spread was 1-12 mV. Finger contact reached 36 mV while the
   average remained a valid temperature. Reject only a disconnect-like swing. */
#define NTC_MAX_SPREAD_MV   150

#define SEND_INTERVAL_MS    1000UL

#endif
