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

/* RGB pixel clock (2026-11-24, was 16 MHz). The magenta / gold full-screen
   flashes are the panel showing a frame whose pixel stream slipped by one
   byte: the navy background 0x0A1628 is RGB565 0x08A5, byte-swapped 0xA508
   is gold; the panel blue 0x1A2E4A is 0x1969, byte-swapped 0x6919 is
   magenta. That happens when the LCD DMA cannot fetch the framebuffer from
   PSRAM in time. Flash and PSRAM share one bus on this module, so a page
   change (a full-screen copy into PSRAM plus font and image reads from
   flash) can starve it. ESP-IDF 4.4 (Arduino core 2.0.17) has no bounce
   buffer and no restart-on-underrun, so the lever here is the PSRAM load
   the panel needs: 16 MHz = 32 MB/s, 14 MHz = 28 MB/s. Frame rate goes from
   39 Hz to 34 Hz. NOT VERIFIED ON THE PANEL. If flashes remain, try 12 MHz
   (29 Hz); the full fix is Arduino core 3.x (ESP-IDF 5) with bounce buffers.
   Timing per frame: (800+4+8+8) x (480+4+8+8) = 410,000 pixel clocks. */
#define DISPLAY_PCLK_HZ     14000000

/* --- XL9535 Relay Expander --- */
/* Board is marked XL9535. DIP address 0x21. Active-high outputs.
   Register writes contain port 0 then port 1.
   K1 / P0.0 has been reported dead on three relay boards. The driver matches
   the reference XL9535 driver, so the cause is not known (2026-11-24). Leave
   it unused until Admin > Relay test has been run on the bench. */
#define RELAY_DRIVER_ENABLED    1
#define XL9535_ADDR             0x21

/* --- DS3231M real-time clock, on the shared I2C bus --- */
#define DS3231_ADDR             0x68

/* Relay map, XL9535 port 0, active-high. K1 is P0.0 (reported dead, see above).
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
#define AUTO_SAMPLE_RUN_MS      30000UL   // Auto: pump runs this long to pull a full breath of engine air to the sensor before a sample (user, 2026-11-24; was 10 s)
#define AUTO_SAMPLE_SETTLE_MS   10000UL   // Auto: pause 10 s after the pump stops, then read. 30 s let the air mix with dry chamber air and read falsely low; the sensor is rated accurate at 6 s
#define AUTO_MAX_RUN_MS         600000UL  // Auto: max continuous pump run 10 min
#define AUTO_COOLDOWN_MS        600000UL  // Auto: cooldown between quick cycles 10 min
#define AUTO_MAX_QUICK_CYCLES   3         // Auto: max quick cycles before regular rotation

/* After the cool down safety check passes (2026-11-25): vent outside for this long to clear
   condensate left in the line between the chamber and the valve, then seal for drying. Run as
   segments, each judged by the vent pressure check against a fresh pump-off reference. */
#define RECHARGE_PURGE_MS           900000UL   // 15 min
#define RECHARGE_PURGE_SEGMENT_MS   30000UL    // pump on this long per segment
#define RECHARGE_MAX_MS         21600000UL // Recharge max 6 hours, counted from the moment the chamber first reaches target
#define RECHARGE_HEATUP_MAX_MS  3600000UL  // Backstop: chamber must reach target within 1 hour of start, or stop (S14)
/* Heater response check (user idea, 2026-11-24). While the heater is
   switched on, the chamber must rise HEATER_MIN_RISE_F (or reach target)
   within HEATER_RESPONSE_MS of heater-ON time, or the recharge stops with
   S19 through the failure cool down. Bench heat-ups rose 53 F in the first
   2 minutes from cold (82 F start) and 17 F in 2 minutes from 222 F; in the
   hold the heater takes 240 F back to 245 F in about a minute. So a working
   heater never comes near this, and a dead one is caught in minutes in any
   phase, not after the 1 h backstop (heat-up) or the 6 h limit (hold). */
#define HEATER_RESPONSE_MS      240000UL   // 4 min of heater-on time
#define HEATER_MIN_RISE_F       10.0f      // ...must raise the chamber this much
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
#define RECHARGE_FAILED_FAN_TEMP_F  120.0f     // Cool down (normal and failure) ends at or below this

/* Recharge temperature (2026-11-21). Heat-up ramps the heater 30 s on / 15 s off
   until the chamber first reaches 240F. Then the normal hold: heater off at
   240F, back on at 235F (5F hysteresis).
   Software thermal trip 255F (THERMAL_CHAMBER_F below uses this value).
   Mechanical thermal fuse 266F (hardware, not firmware). */
#define RECHARGE_TARGET_TEMP_F      240
#define RECHARGE_TEMP_HYSTERESIS_F  5
#define RECHARGE_SOFT_OVERTEMP_F  255    // Software thermal trip, stops all outputs
#define RECHARGE_HARD_OVERTEMP_F  266    // Mechanical thermal fuse, hardware only (documentation)

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

/* Vent pressure check. The sensor node reads a one-port gauge pressure module
   on the dump line, upstream of the vent. When the pump runs with the dump
   valve open, the vent line must show a pressure step. No step means the
   valve did not open, so the regen exhaust is not leaving the system.
   The raw reading is logged every packet so the curve can be studied.

   Ambient baseline (2026-11-20): sample ambient every 30 s while the pump is
   OFF (heating and inter-pulse waits). The baseline is a running average of
   those samples, re-learned on every recharge cycle, so barometric drift
   cannot fake a good step. A pulse is "good" only if the raw reading rises
   to >= baseline + 10,000 counts at any point during the 10 s pump pulse.
   One pulse without the step stops the cycle (S09). Since 2026-11-24 the
   check fails closed: no recharge, and no pump pulse, without a fresh
   pressure reading (S08 if it goes missing). Cool down and safety pulses
   are judged too. */
#define VENT_PRESSURE_CHECK_ENABLED  1
#define VENT_PRESSURE_MIN_STEP      10000   // Raw counts a pulse must rise above the pump-off reference just before it (was 20000 until 2026-11-25; the soft vent line gave a steady ~21000, a wrong valve gives ~2000)
#define VENT_PRESSURE_BLOCKED_STEP  500000  // A rise this big means the vent line is blocked (S20), not flowing
/* Reference taken before every pump pulse (2026-11-25): the median of the pump-off readings from the last
   VENT_REF_WINDOW_MS. It replaces the running average, which drifted away from the real idle level as the
   chamber heated and made good pulses look short. */
#define VENT_REF_WINDOW_MS          6000UL  // Use pump-off readings this recent
#define VENT_REF_MIN_SAMPLES        3       // ...and need at least this many
#define VENT_REF_MAX_SPREAD         8000    // ...that agree within this many counts, or the pulse waits
#define VENT_REF_WAIT_MAX_MS        120000UL // Unsteady this long: fail closed (S21)
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

/* --- SHT40 probe heater policy (2026-11-24, see include/probe_care.h) ---
   The node's hard limits (under 60 C probe, 10% duty, 10 s between pulses)
   are in common/probe_heat.h. Each pulse is 200 mW for 1 s. */
#define SENSOR_NODE_MAC                 {0xF8, 0x5B, 0x1B, 0xBF, 0x3F, 0xEC}  // heat requests go here (unicast)
#define PROBE_DRY_PULSES                2          // a normal burn-off: 2 pulses, 10 s apart
#define PROBE_DRY_PULSES_LONG           3          // safety samples and the post-recharge series
#define PROBE_REQUEST_RETRY_MS          2000UL     // resend an unacknowledged heat request
#define PROBE_REQUEST_TRIES             4
#define RECHARGE_PROBE_DRY_AT_MS        15000UL    // R1: dry To-Engine this far into each 60 s pulse wait
#define SAFETY_PREP_MAX_MS              240000UL   // R1: wait at most this long for the probe before a safety pulse
#define SAFETY_PREP_DRIED_MS            180000UL   // ...a burn-off this recent counts
#define AUTO_PROBE_WAIT_MAX_MS          60000UL    // R1: an auto dryer decision waits this long at most for the probe
#define SAT_CONFIRM_DRIED_MS            600000UL   // R1: a saturation stop needs a burn-off this recent...
#define SAT_CONFIRM_MAX_MS              120000UL   // ...or stops anyway after waiting this long for one
#define PROBE_CONDENSE_SPREAD_C         3.0f       // R2: within 3 C (5.4 F) of the dew point = condensation zone (about 85% RH)
#define PROBE_ZONE_INTERVAL_MS          600000UL   // R2: dry every 10 min in the zone
#define PROBE_WET_RH                    95.0f      // R3: a reading this high starts a wet history
#define PROBE_WET_HOLD_MS               129600000UL // R3: wet history lasts 36 h (bench: readings ran high 24-36 h after a recharge)
#define PROBE_WET_INTERVAL_MS           900000UL   // R3: dry every 15 min during a wet history
#define PROBE_POST_RECHARGE_MS          1800000UL  // R3: for 30 min after a recharge...
#define PROBE_POST_RECHARGE_INTERVAL_MS 180000UL   // ...dry every 3 min (3 pulses)
#define PROBE_DISPLAY_TAU_MS            20000UL    // R5: user screens smooth humidity over about 20 s

/* --- Relay test (Admin > Relay test), bench diagnostic for the K1 question --- */
#define RELAY_TEST_PULSE_MS     2000UL  // One pin is driven this long, then everything returns to normal

/* --- UI Colors (dark theme) --- */
#define UI_BG_COLOR         0x0A1628  // Deep navy background
#define UI_PANEL_COLOR      0x12304F  // Secondary buttons, panels
#define UI_INFO_COLOR       0x3399FF  // Auto Cycle button
#define UI_ACCENT_COLOR     0x00E5FF  // Cyan accent ("Jarvis" look)
#define UI_WARN_COLOR       0xFF4444  // Red for warnings/stop
#define UI_TEXT_COLOR       0xFFFFFF  // White text

/* --- Logging --- */
#define LOG_FILENAME        "/hangar.csv"

#endif // CONFIG_H
