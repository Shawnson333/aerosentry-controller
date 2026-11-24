#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

/* ========================================================================
   AEROSENTRY CONFIGURATION
   All hardware pins, I2C addresses, safety thresholds, and UI colors live here.
   Change hardware settings in ONE place only.
   ======================================================================== */

/* --- Firmware identity. Printed at boot and shown on the Admin page. --- */
#define FIRMWARE_VERSION    "1.0.0-beta.1"

/* --- Loop watchdog (2026-11-24) ---
   The Arduino core already runs the ESP-IDF task watchdog (5 s, reset on
   timeout), but only on the core 0 idle task. loop() runs on core 1 and was
   not watched, so a hang (I2C lock-up, LVGL stall) could leave the heater
   relay latched on in the XL9535 with only the 266 F fuse as protection.
   1 = also watch loop(): if one pass takes longer than 5 s the chip resets,
   setup() turns every relay off, and the boot screen reports code S18.
   Sensor dropouts do not trip it: they are handled inside loop(), which
   keeps running. The slowest loop pass since boot is shown on the Admin
   page ("Loop max") so the margin can be checked on the bench. */
#define LOOP_WATCHDOG_ENABLED   1

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
      bit 4 / K5 = To-Engine valve. Energized = dump the regen exhaust to
               atmosphere (the vent line the pressure sensor watches).
      bit 5 / K6 = From-Engine valve. Energized = pull fresh ambient air into
               the chamber for recharge, so no engine air is drawn in.
      bit 6 / K7 = AUX, future engine heater
      bit 7 / K8 = AUX, future user accessory
      bit 0 / K1 = unused, never energized
*/
#define RELAY_BIT_AUX_12V   0
#define RELAY_BIT_PUMP      1
#define RELAY_BIT_HEATER    2
#define RELAY_BIT_FAN       3
#define RELAY_BIT_VALVE_TO  4
#define RELAY_BIT_VALVE_FROM 5
#define RELAY_BIT_AUX_AC1   6
#define RELAY_BIT_AUX_AC2   7
#define RELAY_ALLOWED_MASK  ((1 << RELAY_BIT_AUX_12V) | (1 << RELAY_BIT_PUMP) | \
                             (1 << RELAY_BIT_HEATER) | (1 << RELAY_BIT_FAN) | \
                             (1 << RELAY_BIT_VALVE_TO) | (1 << RELAY_BIT_VALVE_FROM) | \
                             (1 << RELAY_BIT_AUX_AC1) | (1 << RELAY_BIT_AUX_AC2))

/* --- Remote control (user decision 2026-11-24) ---
   Starts are accepted only from the radio bridge Heltec. Its MAC is the
   sender of every "Heltec beacon" line in the bench logs (37,165 beacons in
   the 2026-11-23 dryer run). STOP is accepted from any sender.
   Recharge is never started remotely, and a remote dryer start never arms
   Auto Recharge: nobody at a phone can see that the unit is somewhere safe
   to get hot and expel water. */
#define REMOTE_BRIDGE_MAC           {0x10, 0xBD, 0xA3, 0x5A, 0x7C, 0x94}
#define REMOTE_REQUIRE_BRIDGE_MAC   1
#define REMOTE_RECHARGE_ALLOWED     0

/* --- Safety / cycle timing (best guesses; tune after testing) --- */
#define SAT_LIMIT_RH            40.0f   // To-Engine RH hard-stop (%). Not used in Recharge.
#define SAT_HOLD_MS             30000UL // Continuous time above limit while the pump is on
#define GRACE_MS                2000UL  // Ignore saturation briefly after a cycle starts
#define SENSOR_STALE_MS         15000UL // No packet this long = sensor offline
#define SAMPLE_MAX_AGE_MS       3000UL  // Sample decisions need a reading this fresh
#define CHAMBER_RETRY_MS        6000UL  // Hold prior heat briefly, then stop if no valid sample
#define SENSOR_LOSS_GRACE_MS    30000UL // Sensors may be missing this long before a cycle stops (worst radio gap seen: 3.2 s)

/* Purge and manual dryer run until the user stops them or To-Engine stays
   above SAT_LIMIT_RH for SAT_HOLD_MS. They do not end on a fixed timer. */

#define AUTO_SAMPLE_INTERVAL_MS 3600000UL // Auto: 1 hour between samples
#define AUTO_SAMPLE_RUN_MS      10000UL   // Auto: pump runs this long to bring engine air to the sensor before a sample
#define AUTO_SAMPLE_SETTLE_MS   10000UL   // Auto: pause 10 s after the pump stops, then read. 30 s let the air mix with dry chamber air and read falsely low; the sensor is rated accurate at 6 s
#define AUTO_MAX_RUN_MS         600000UL  // Auto: max continuous pump run 10 min
#define AUTO_COOLDOWN_MS        600000UL  // Auto: cooldown between quick cycles 10 min
#define AUTO_MAX_QUICK_CYCLES   3         // Auto: max quick cycles before regular rotation

#define RECHARGE_MAX_MS         21600000UL // Recharge max 6 hours, counted from the moment the chamber first reaches target
#define RECHARGE_HEATUP_MAX_MS  3600000UL  // Backstop: chamber must reach target within 1 hour of start, or stop
#define RECHARGE_TARGET_RH      15.0f      // To-Engine RH that counts as a good cycle
#define RECHARGE_GOOD_CYCLES    3          // Consecutive good samples required
#define RECHARGE_PULSE_ON_MS    10000UL    // Recharge pump on time, first hour
#define RECHARGE_FIRST_PULSE_MS 10000UL    // First pulse also flushes the soaked probes
#define RECHARGE_PULSE_WAIT_MS  60000UL    // Recharge pump off time between pulses (1 min, both stages)
#define RECHARGE_LONG_PULSE_ON_MS    10000UL   // Pump on time after the first hour. Same as the first hour for now (10 s). Longer pulses dropped the To-Engine exit air from about 150F to 130F, which means the beads were running cooler. Raise this to try longer pulses again.
#define RECHARGE_LONG_PULSE_AFTER_MS 3600000UL // Long pulses start this long after the chamber first reaches target
#define RECHARGE_RAMP_ON_MS     30000UL    // Heat-up ramp: heater on 30 s
#define RECHARGE_RAMP_OFF_MS    15000UL    // Heat-up ramp: heater off 15 s, until target is first reached
#define RECHARGE_COOLDOWN_MS    7200000UL  // Recharge cool down, up to 2 hours (ends early at 120F), valves open loop
#define RECHARGE_COOLDOWN_PULSE_ON_MS  10000UL  // Loose cooldown pulse: 10 s ON
#define RECHARGE_COOLDOWN_INTERVAL_MS   600000UL // Loose cooldown pulse: every 10 min (10 min OFF)
#define SAFETY_PULSE_MS         10000UL    // Safety check pump run 10 sec
#define SAFETY_SAMPLE_INTERVAL_MS 60000UL  // Safety check sample delay 1 min
#define SAFETY_SAMPLE_COUNT     2          // Safety check runs twice
#define RECHARGE_FAILED_FAN_MAX_MS  7200000UL  // Failed recharge fan-hold 2 h backstop
#define RECHARGE_FAILED_FAN_TEMP_F  120.0f     // Failed fan-hold + cooldown early-exit temp

/* Recharge temperature (2026-11-21). Heat-up ramps the heater 30 s on / 15 s off
   until the chamber first reaches 245F. Then the normal hold: heater off at
   245F, back on at 240F (5F hysteresis).
   Software thermal trip 255F (THERMAL_CHAMBER_F below uses this value).
   Mechanical thermal fuse 266F (hardware, not firmware). */
#define RECHARGE_TEST_MODE       1
#if RECHARGE_TEST_MODE
#define RECHARGE_TARGET_TEMP_F      245
#define RECHARGE_TEMP_HYSTERESIS_F  5
#else
#define RECHARGE_TARGET_TEMP_F      245
#define RECHARGE_TEMP_HYSTERESIS_F  5
#endif
#define RECHARGE_SOFT_OVERTEMP_F  255    // Software thermal trip, stops all outputs
#define RECHARGE_HARD_OVERTEMP_F  266    // Mechanical thermal fuse, hardware only (documentation)

/* Valve position verification by humidity trend is retired. The vent pressure
   check is a direct mechanical measurement and does not depend on ambient
   humidity, which made the old comparison unreliable in humid air and in a
   humid engine. The trend code is kept in statemachine.cpp for reference but
   is not compiled in. */
#define VALVE_TREND_CHECK_ENABLED 0

/* Valve coil current sense. The sensor node reads a 1-ohm shunt in the valve
   12 V return on GPIO 3. The 4 W coil draws about 0.33 A, so the node reports
   about 330 mV while the valve is energized and near 0 mV when it is off.
   DISABLED for the first all-up bench test because the shunt is not wired
   yet. Set to 1 once the shunt and the common ground are in place. */
#define VALVE_CURRENT_CHECK_ENABLED 0
#define VALVE_CURRENT_MIN_MV       150    // Below this while commanded on = fault
#define VALVE_CURRENT_GRACE_MS     3000   // Ignore readings just after energizing
#define VALVE_CURRENT_CHECK_MS     1000   // Cadence of the current check
#define VALVE_CURRENT_FAULT_COUNT  3      // Consecutive low readings to alarm

/* The old From-Engine climb ladder is retired in favour of the vent pressure
   check. The code is kept but not compiled in. */
#define VALVE_TREND_CHECK_ENABLED 0

/* Vent pressure check. The sensor node reads a one-port gauge pressure module
   on the dump line, upstream of the vent. When the pump runs with the dump
   valve open, the vent line must show a pressure step. No step means the
   valve did not open, so the regen exhaust is not leaving the system.
   The raw reading is logged every packet so the curve can be studied.

   Ambient baseline (2026-11-20): sample ambient every 30 s while the pump is
   OFF (heating and inter-pulse waits). The baseline is a running average of
   those samples, re-learned on every recharge cycle, so barometric drift
   cannot fake a good step. A pulse is "good" only if the raw reading rises
   to >= baseline + 20,000 counts at any point during the 10 s pump pulse.
   The first pulse also flushes the soaked probes: it must also hit 20,000
   on that same 10 s window. */
#define VENT_PRESSURE_CHECK_ENABLED  1
#define VENT_PRESSURE_MIN_STEP      20000   // Raw counts above the ambient average
#define VENT_PRESSURE_IDLE_BASELINE  0     // Baseline learned while the pump is off
#define VENT_PRESSURE_SAMPLES       6     // Pulses required before alarming (unchanged)
#define VENT_PRESSURE_LEARN_MS      30000UL // Baseline learning window at start
#define VENT_PRESSURE_AMBIENT_SAMPLE_MS 30000UL // Take an ambient sample every 30 s while pump OFF
#define LOG_INTERVAL_MS         30000UL    // Log every sensor while a cycle runs
#define PURGE_RECENT_MS         3600000UL  // Dryer warns if no qualified purge in this window
#define PURGE_QUALIFY_MS        60000UL    // Pump must run this long before a purge counts

/* Hard thermal stops. Chamber is the heater control sensor.
   The SHT31s should stay well below chamber temperature. */
#define THERMAL_CHAMBER_F       RECHARGE_SOFT_OVERTEMP_F  // one number: 255F
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
