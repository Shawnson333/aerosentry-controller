#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// I2C Bus Pins (Waveshare ESP32-S3-Touch-LCD-4.3B)
#define I2C_SDA_PIN     8
#define I2C_SCL_PIN     9
#define I2C_PORT        I2C_NUM_0

// XL9535 GPIO Expander
#define XL_ADDR         0x20
#define REG_OUTPUT      0x02
#define REG_CONFIG      0x06

// Updated Relay Bit Mapping
#define BIT_PUMP        0   // Bit 0: Air Pump
#define BIT_HEATER      1   // Bit 1: Dryer Heater
#define BIT_FAN         2   // Bit 2: Case Fan (Reserved)
#define BIT_RESERVED    3   // Bit 3: Reserved

// Safety Thresholds
#define SAT_LIMIT_RH    40.0f
#define SAT_HOLD_MS     30000UL
#define GRACE_MS        2000UL

#endif