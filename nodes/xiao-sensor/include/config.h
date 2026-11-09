#ifndef XIAO_CONFIG_H
#define XIAO_CONFIG_H

#include <Arduino.h>
#include "sensor_packet.h"

/* XIAO ESP32-C3 pins. D8/D9 are the labeled I2C pins.
   D1/GPIO3 is an ADC pin and is not a boot strap pin. */
#define I2C_SDA_PIN         8    // D8
#define I2C_SCL_PIN         9    // D9
#define THERMISTOR_ADC_PIN  3    // D1

#define SHT31_ADDR_TO       0x44 // ADDR pin tied to GND
#define SHT31_ADDR_FROM     0x45 // ADDR pin tied to 3V3

/* 3V3 -- thermistor -- D1 -- 10k -- GND */
#define NTC_SERIES_OHMS     10000.0f
#define NTC_R0_OHMS         100000.0f
#define NTC_BETA            3950.0f
#define NTC_T0_C            25.0f
#define NTC_VCC             3.3f

#define SEND_INTERVAL_MS    1000UL

#endif
