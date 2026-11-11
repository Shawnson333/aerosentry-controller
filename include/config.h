#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

/* ========================================================================
   AEROSENTRY CONFIGURATION
   All hardware pins, I2C addresses, safety thresholds, and UI colors live here.
   Change hardware settings in ONE place only.
   ======================================================================== */

/* --- I2C Bus (shared by display + relay expander + RTC) --- */
#define I2C_SDA_PIN     8
#define I2C_SCL_PIN     9

/* --- Display: Waveshare ESP32-S3 4.3-inch Type B, 800x480 --- */
#define DISPLAY_WIDTH       800
#define DISPLAY_HEIGHT      480

/* --- XL9535 Relay Expander --- */
/* Board is marked XL9535. DIP address 0x21. Active-high outputs.
   Register writes contain port 0 then port 1.
   K1 / P0.0 does not latch or drive. Leave it unused. */
#define RELAY_DRIVER_ENABLED    1
#define XL9535_ADDR             0x21

/* --- DS3231M real-time clock, on the shared I2C bus --- */
#define DS3231_ADDR             0x68

/* Relay map, XL9535 port 0, active-high. K1 is P0.0 and does not drive.
   K2 was confirmed on P0.1. K3-K8 follow that order from the port-0 sweep.
      bit 1 / K2 = Pump
      bit 2 / K3 = Heater mat
      bit 3 / K4 = Case fan. On while the pump runs, for all of recharge,
                   and while the Waveshare chip is hot.
      bit 4 / K5 = AUX, named but not driven by any mode
      bits 0, 5, 6, 7 = K1 and K6-K8, never energized
*/
#define RELAY_BIT_PUMP      1
#define RELAY_BIT_HEATER    2
#define RELAY_BIT_FAN       3
#define RELAY_BIT_AUX       4
#define RELAY_ALLOWED_MASK  ((1 << RELAY_BIT_PUMP) | (1 << RELAY_BIT_HEATER) | \
                             (1 << RELAY_BIT_FAN) | (1 << RELAY_BIT_AUX))

/* --- Safety / cycle timing (best guesses; tune after testing) --- */
#define SAT_LIMIT_RH            40.0f   // To-Engine RH hard-stop (%). Not used in Recharge.
#define SAT_HOLD_MS             30000UL // Continuous time above limit while the pump is on
#define GRACE_MS                2000UL  // Ignore saturation briefly after a cycle starts
#define SENSOR_STALE_MS         15000UL // No packet this long = sensor offline
#define SAMPLE_MAX_AGE_MS       3000UL  // Sample decisions need a reading this fresh
#define CHAMBER_RETRY_MS        6000UL  // Hold prior heat briefly, then stop if no valid sample

/* Purge and manual dryer run until the user stops them or To-Engine stays
   above SAT_LIMIT_RH for SAT_HOLD_MS. They do not end on a fixed timer. */

#define AUTO_SAMPLE_INTERVAL_MS 3600000UL // Auto: 1 hour between samples
#define AUTO_SAMPLE_RUN_MS      10000UL   // Auto: pump 10 seconds before a sample
#define AUTO_SAMPLE_SETTLE_MS   30000UL   // Auto: pause 30 seconds to acclimate

#define RECHARGE_MAX_MS         10800000UL // Recharge max 3 hours
#define RECHARGE_TARGET_RH      15.0f      // To-Engine RH that counts as a good cycle
#define RECHARGE_GOOD_CYCLES    3          // Consecutive good samples required
#define RECHARGE_TARGET_TEMP_F  240        // Chamber target, Fahrenheit
#define RECHARGE_TEMP_HYSTERESIS_F 5       // Heater back on this far below target
#define RECHARGE_PULSE_ON_MS    10000UL    // Recharge pump on time
#define RECHARGE_PULSE_WAIT_MS  60000UL    // Recharge wait between pulses
#define LOG_INTERVAL_MS         30000UL    // Log every sensor while a cycle runs
#define PURGE_RECENT_MS         3600000UL  // Dryer warns if no qualified purge in this window
#define PURGE_QUALIFY_MS        60000UL    // Pump must run this long before a purge counts

/* Hard thermal stops. Chamber is the heater control sensor.
   The SHT31s should stay well below chamber temperature. */
#define THERMAL_CHAMBER_F       260
#define THERMAL_SENSOR_F        250

/* ESP32-S3 die temperature, not case air. Starting guess so a hot
   controller can run the case fan even when the pump is off. */
#define CASE_FAN_CHIP_ON_C      65.0f
#define CASE_FAN_CHIP_OFF_C     55.0f

/* --- UI Colors (dark theme) --- */
#define UI_BG_COLOR         0x0A1628  // Deep navy background
#define UI_ACCENT_COLOR     0x00FF88  // Green accent
#define UI_WARN_COLOR       0xFF4444  // Red for warnings/stop
#define UI_TEXT_COLOR       0xFFFFFF  // White text

/* --- Logging --- */
#define LOG_FILENAME        "/hangar.csv"

#endif // CONFIG_H