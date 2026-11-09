#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

/* ========================================================================
   AEROSENTRY CONFIGURATION
   All hardware pins, I2C addresses, safety thresholds, and UI colors live here.
   Change hardware settings in ONE place only.
   ======================================================================== */

/* --- I2C Bus (shared by display + relay expander) --- */
#define I2C_SDA_PIN     8
#define I2C_SCL_PIN     9

/* --- Display: Waveshare ESP32-S3 4.3-inch Type B, 800x480 --- */
#define DISPLAY_WIDTH       800
#define DISPLAY_HEIGHT      480

/* --- XL9535 Relay Expander --- */
/* Board is marked XL9535. DIP address 0x21. Active-high outputs.
   Register writes contain port 0 then port 1. K1 mapping is unconfirmed. */
#define RELAY_DRIVER_ENABLED    1
#define XL9535_ADDR             0x21

/* Relay physical channel mapping (1-based, matching the 8-channel board):
     Channel 1 (bit 0) = Pump
     Channel 2 (bit 1) = Heater mat
     Channel 3 (bit 2) = Case fan (reserved)
     Channels 4–8      = Future expansion
*/
#define RELAY_BIT_PUMP      0
#define RELAY_BIT_HEATER    1
#define RELAY_BIT_FAN       2

/* --- Safety / cycle timing (best guesses; tune after testing) --- */
#define SAT_LIMIT_RH            40.0f   // To-Engine RH hard-stop (%). Not used in Recharge.
#define SAT_HOLD_MS             30000UL // Continuous time above limit while the pump is on
#define GRACE_MS                2000UL  // Ignore saturation briefly after a cycle starts
#define SENSOR_STALE_MS         15000UL // No packet this long = sensor offline
#define SAMPLE_MAX_AGE_MS       3000UL  // Sample decisions need a reading this fresh

/* Purge and manual dryer run until the user stops them or To-Engine stays
   above SAT_LIMIT_RH for SAT_HOLD_MS. They do not end on a fixed timer. */

#define AUTO_SAMPLE_INTERVAL_MS 3600000UL // Auto: 1 hour between samples
#define AUTO_SAMPLE_RUN_MS      10000UL   // Auto: pump 10 seconds before a sample
#define AUTO_SAMPLE_SETTLE_MS   30000UL   // Auto: pause 30 seconds to acclimate

#define RECHARGE_MAX_MS         10800000UL // Recharge max 3 hours
#define RECHARGE_TARGET_RH      15.0f      // To-Engine RH that counts as a good cycle
#define RECHARGE_GOOD_CYCLES    3          // Consecutive good samples required
#define RECHARGE_TARGET_TEMP_F  240        // Chamber thermistor target before pulsing the pump
#define RECHARGE_PULSE_ON_MS    10000UL    // Recharge pump on time
#define RECHARGE_PULSE_WAIT_MS  60000UL    // Recharge wait between pulses
#define LOG_INTERVAL_MS         30000UL    // Log every sensor while a cycle runs

/* --- UI Colors (dark theme) --- */
#define UI_BG_COLOR         0x0A1628  // Deep navy background
#define UI_ACCENT_COLOR     0x00FF88  // Green accent
#define UI_WARN_COLOR       0xFF4444  // Red for warnings/stop
#define UI_TEXT_COLOR       0xFFFFFF  // White text

/* --- Logging --- */
#define LOG_FILENAME        "/hangar.csv"

#endif // CONFIG_H