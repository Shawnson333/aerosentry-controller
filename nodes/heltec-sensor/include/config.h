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

/* Vent pressure module. One-port gauge sensor on the dump line, upstream of
   the vent, reached through a 2.5 mm tube off a 1/4 in tee. It is NOT an I2C
   device: it is an HX711 load-cell ADC behind a pressure die, so it needs two
   plain GPIOs. GPIO 47 and 48 are physically adjacent on header J2.
   DOUT reads the data line, SCK the clock. */
#define PRESSURE_DOUT_PIN 48
#define PRESSURE_SCK_PIN  47

#define SEND_INTERVAL_MS    1000UL

/* ESP-NOW broadcast has no acknowledgement and no retry. The 2026-11-23
   dryer log lost about 0.4% of frames, and two lost in a row is a 3.2 s
   gap. Each packet is sent this many times, SEND_REPEAT_GAP_MS apart, with
   the same sequence number; the controller keeps the first copy. */
#define SEND_REPEAT_COUNT   2
#define SEND_REPEAT_GAP_MS  30UL

#endif
