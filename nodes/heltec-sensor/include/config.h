#ifndef HELTEC_SENSOR_CONFIG_H
#define HELTEC_SENSOR_CONFIG_H

#include <Arduino.h>

/* No-display Heltec WiFi LoRa 32 V4.3. Both probes may answer at 0x44,
   so they use independent buses on the top connector. */
#define TO_SDA_PIN          17
#define TO_SCL_PIN          18
#define FROM_SDA_PIN        15
#define FROM_SCL_PIN        16
#define THERMISTOR_ADC_PIN  4

/* 3V3 -- 100K NTC 3950 -- GPIO 4 -- 10k -- GND. */
#define CHAMBER_DIVIDER_INSTALLED  1
#define NTC_SERIES_OHMS     10000.0f
#define NTC_R0_OHMS         100000.0f
#define NTC_BETA            3950.0f
#define NTC_T0_C            25.0f
#define NTC_VCC             3.3f
#define NTC_MAX_SPREAD_MV   150

/* Valve coil current sense. A 1-ohm shunt sits in the valve 12 V return:
   valve(-) -- GPIO 3 tap -- 1 ohm -- 12 V(-). The 4 W coil draws about
   0.33 A, so GPIO 3 reads about 330 mV while the valve is energized and
   near 0 mV when it is off. The 12 V supply negative must be common with
   this board's ground. A flyback diode belongs across the valve coil. */
#define VALVE_SHUNT_ADC_PIN 3

#define SEND_INTERVAL_MS    1000UL

#endif
