# AeroSentry Controller: Handoff

Written 2026-11-23 by the previous agent (Cline). **Updated 2026-11-24 by Claude** after the v1 beta build:
branch `v1-beta`, firmware `1.0.0-beta.1`. Repo: `https://github.com/Shawnson333/aerosentry-controller`.

**How far to trust this file.** Every pin, constant, path and behavior below was read from the files on disk.
Anything that could not be checked is marked **Unverified**. Where an older document disagrees with the code, the
code wins, and the stale document is listed in section 4.

## READ THIS FIRST

1. **The v1 beta is flashed (2026-11-24) but mostly unproven.** Controller and sensor node run branch `v1-beta`;
   boot checks passed. Everything else is open in `docs/BENCH_TEST_CHECKLIST.md`. Rollback images of the old
   firmware are in `Documents\AeroSentry firmware backups\2026-11-24-before-v1-beta`.
2. **Do not flash again until the user says so.** The Waveshare's CH422G answers at the relay board's address
   (proven 2026-11-24): never read the relay board and trust the result; the driver only writes.
3. The COM4 disconnect at 22:46:15 on 2026-11-23 was the user unplugging the board. Auto dryer run 2 ran 10.3 h
   cleanly.
4. **User working style:** capture logs, do not poll or monitor; say plainly what is unverified; never flash
   without being told; always pin the upload port; do not guess.
5. Before changing controller logic, run `python tools/host_test.py` (section 4). It runs the real state machine
   against simulated sensors, relays and a chamber, and checks the safety invariants at every step.

---

## 1. Product goals and system overview

### What it is
A hangar-based controller that keeps an aircraft engine dry. A pump circulates air through a desiccant chamber (about 4 lb of beads, user figure). The **From-Engine** line brings engine air into the chamber and the **To-Engine** line returns dried air. When the beads are saturated, a **recharge** heats the chamber to 245 F and pumps the released moisture out through a dump valve to atmosphere. Regeneration steam must never be pumped into the engine. That is the central safety goal. (The line roles are inferred from the UI prompts and the code; the plumbing itself is not documented in one place.)

### System parts
| Part | Role |
|---|---|
| Waveshare ESP32-S3 4.3-inch Touch LCD, Type B (800x480) | The controller. State machine, touch UI (LVGL 8.3), relay driver, RTC, ESP-NOW receiver. Firmware is the root PlatformIO project. |
| Heltec WiFi LoRa 32 V4.3, no OLED (sensor node) | Reads two SHT40 probes, the chamber NTC thermistor, the valve-shunt voltage and the vent pressure module. Broadcasts one `SensorPacket` about once a second. Firmware: `nodes/heltec-sensor`. |
| Second Heltec V4.3 (radio bridge, COM6) | USB serial to the PC. Forwards remote commands to the Waveshare on ESP-NOW and prints status. Firmware: `nodes/heltec-radio`. |
| 8-channel XL9535 relay board | Switches pump, heater mat, case fan and two valves (plus unused aux channels). |
| DS3231M RTC | Battery-backed clock, shared I2C bus. |
| PC running `tools/remote_bridge.py` | Phone page over Tailscale, talks to the radio bridge over USB serial. |

### Operating modes
| Mode | What the outputs do | Ends when | Sensors required to start |
|---|---|---|---|
| Purge | Pump on, heater off, valves de-energized. A purge counts as "qualified" after 60 s of pump (`PURGE_QUALIFY_MS`). | User Stop, or To-Engine RH above 40% for 30 s with the pump on (`SATURATED`). | To-Engine |
| Manual dryer | Pump on for `dryerOnMinutes` (1-5, default 3), off for `dryerOffHours` (1-3, default 1), repeating. | User Stop, or saturation. | To-Engine |
| Auto dryer | See the phase list below. Valves stay de-energized. | User Stop, or saturation. | To-Engine and From-Engine |
| Recharge | See the sequence below. Both valves energized the whole time. | Normal completion, or any stop code in `docs/ERROR_CODES.md`. | To-Engine, chamber thermistor and vent pressure. Local only (no remote Recharge). |

Every mode also ends on a thermal trip (S11-S13) and, after the 30 s grace, a sensor-loss code (S04-S08). Saturation stop applies to Purge, Manual and Auto only. If "Allow Auto Recharge" is on AND the dryer was started at the screen (after the unattended-recharge warning), saturation starts a recharge (only if its sensors, including vent pressure, are present) and resumes that dryer cycle after the safety check passes. A remote dryer start never arms it. **That auto-recharge path has not been exercised on hardware** (simulated only).

**Auto dryer phases** (the `AutoPhase` enum value in brackets is what the serial log prints as `Auto phase N`):
- `SAMPLE_RUN` [4]: pump 10 s (`AUTO_SAMPLE_RUN_MS`); `Auto phase 4` is printed at cycle start since 2026-11-24. Then `SAMPLE_SETTLE` [5]: pump off 10 s in source (`AUTO_SAMPLE_SETTLE_MS`; 30 s in the flashed build). Then read From-Engine. At or below target (default 20%, range 15-30) go to `WAITING`; above target go to `DRYING`.
- `DRYING` [0]: pump on until From-Engine is at or below target (readings ignored for the first 10 s) or 10 min (`AUTO_MAX_RUN_MS`), then `COOLDOWN` [1] for 10 min, then `QUICK_SAMPLE` [2] (pump 10 s, settle, read). Up to 3 quick cycles (`AUTO_MAX_QUICK_CYCLES`), then `WAITING`.
- `WAITING` [3]: pump off 1 h (`AUTO_SAMPLE_INTERVAL_MS`), then `SAMPLE_RUN`.
- **Source vs board:** in the source tree the cycle STARTS in `SAMPLE_RUN`. The flashed build starts in `DRYING` and reads From-Engine within about 1 s of pump start. That read was old chamber air (13-15.7%), which is the bug the sample fix addresses.

**Recharge sequence** (`RechargePhase`, printed as `Recharge phase N`):
1. `HEATING` [0]: both valves energized at mode entry, heater on. Heater ramps 30 s on / 15 s off until the chamber first reaches 240 F. Pump off. The vent baseline is learned here (30 s of pump-off samples); the first pulse waits for it. Backstop: if 240 F is not reached within 1 h, failure cool down then S14.
2. `PULSE_ON` [1]: pump on 10 s. Peak vent pressure is tracked on every new pressure packet. At the end the vent check judges the pulse (a miss stops the cycle, S09; a pulse with no pressure sample is not judged and does not count), then it counts To-Engine samples at or below 15% (three in a row go to cool down). A sensor dropout mid-pulse stops the pump at once and the pulse is re-run after the wait.
3. `PULSE_WAIT` [2]: pump off 60 s, then `PULSE_ON`. Heater holds 240 F: off at 240, on at 235.
4. Max run: 6 h counted from the moment the chamber first reaches target. Latched once, then `COOLDOWN`.
5. `COOLDOWN` [3]: heater off, case fan on, valves still open, pump 10 s every 10 min only while the vent check can confirm it (each pulse judged), ends at chamber 120 F or below, or after 2 h. The failure cool down is the same phase, entered once, and runs on time alone if the sensors are gone.
6. `SAFETY_PULSE` [4] pump 10 s (judged by the vent check), `SAFETY_WAIT` [5] 60 s, then To-Engine must read 15% or below. Twice in a row completes the cycle (then `PURGE` [7], see below; the 6 h limit no longer matters once the safety check passes). Any sample above 15% stops with `SAFETY_FAILED`.

7. `PURGE` [7] (2026-11-25): after the safety check passes, 15 min (`RECHARGE_PURGE_MS`) venting outside, valves open, heater off, pump in three 5 min segments (`RECHARGE_PURGE_SEGMENT_MS`) of steady air, each started against a fresh pump-off reference and judged at its end. While a segment runs, from 15 s in, the pressure must stay at least 10,000 over that reference (below it for 10 s = S09) and under 500,000 (S20), so a valve that moves is caught in seconds. Then `finishRecharge()`: seal, resume the dryer cycle that asked for the recharge, or stop `RECHARGE_COMPLETE`. A time-limited recharge whose safety check passed is also complete (S17 is retired). A fault in the purge stops at once.

### Non-negotiable operational logic and safety interlocks
| Interlock | Where | Value |
|---|---|---|
| Software thermal trip, chamber | `THERMAL_CHAMBER_F` = `RECHARGE_SOFT_OVERTEMP_F` in `include/config.h` | 255 F, stops everything, latched until it falls, blocks new starts |
| Software thermal trip, humidity probes | `THERMAL_SENSOR_F` | 250 F on either SHT40 |
| Mechanical thermal fuse | Hardware only. Nothing in firmware reads it. `RECHARGE_HARD_OVERTEMP_F` is documentation. | 266 F |
| Vent pressure check | `judgeVentPulse()`, `ventSensorOk()` | Every pump pulse (recharge, cool down, safety) must reach the pump-off reference taken just before it + 10,000 counts, or stop with `VALVE_SUSPECT` (S09) at once; a rise of 500,000 or more stops with `VENT_BLOCKED` (S20); no steady reference for 2 min fails closed with `VENT_UNSTABLE` (S21). **Fails closed** (2026-11-24): no recharge start and no pump pulse without a fresh pressure reading; missing for 30 s in a recharge goes to the failure cool down with the pump off (S08). |
| Sensor loss | `update()`, `updateRecharge()` | 30 s grace (`SENSOR_LOSS_GRACE_MS`). Dryers and purge hold, then stop. Recharge: pump off at once, then the failure cool down, entered once, advancing on time even with no sensors. |
| Heater without a fresh chamber reading | `updateRecharge()` and `regulateHeater()` | Never newly enabled without a fresh (3 s) chamber sample. `regulateHeater()` may hold the PREVIOUS command for up to 6 s (`CHAMBER_RETRY_MS`) after the last valid sample. Off in every cool down and safety phase. |
| Heat-up backstop | `RECHARGE_HEATUP_MAX_MS` | 1 h (previous agent's pick, not the user's). Now cools down before stopping (S14). |
| Recharge max run | `RECHARGE_MAX_MS` | 6 h from first reaching target |
| Cooldown before the safety check | `RECHARGE_COOLDOWN_MS`, `RECHARGE_FAILED_FAN_TEMP_F` | Ends at 120 F or after 2 h. Pump pulses only when the vent check can confirm them. |
| Loop watchdog | `LOOP_WATCHDOG_ENABLED`, `enableLoopWDT()` in `setup()` | 5 s. A hang resets the chip; `setup()` turns every relay off before anything else; boot shows S18. |
| Remote commands | `src/radio_link.cpp` | No remote Recharge. Starts only from the bridge MAC. Remote dryer starts never arm Auto Recharge. Stop from anyone. |
| Valves de-energize on any stop | `stopWith()` calls `relay.allOff()` | The valves are normally closed per the notes, so a stop or power loss returns flow to the engine loop (not verified by me) |
| STOP is always accepted | `StateMachine::command()` | Queued, applied on the next `update()` |
| Start gating | `startBlockReason()` | Required sensors present, no cycle running, no thermal limit exceeded |

### What a shippable v1 means
Documented boundaries (`docs/PROJECT_NOTES.md`, section "V1 boundaries", partly stale): local touch-screen operation is the priority; one wired sensor node supplies both humidity readings over ESP-NOW; the history viewer is a web page; SD logging is deferred; remote control is a stand-in USB bridge for now.

**Proposed exit criteria (the previous agent's proposal, NOT approved by the user):**
1. One unattended recharge ends in `RECHARGE_COMPLETE` with the cool down reaching 120 F and both safety samples at or below 15%. The cool down fix has never run on the heater.
2. One auto dryer run of 12 h or more with no stops, on the sample-fix build, with the first reading after cycle start correct.
3. The error message stays on screen until OK, seen on the display (never seen yet).
4. The open safety decisions in section 5 (P0) are answered.
5. A tagged commit of the tested state.

---

## 2. Hardware architecture and pinout reference

All pins below were checked against `include/config.h`, `src/display.cpp`, `src/relay.cpp` and `nodes/*/include/config.h` on 2026-11-23.

### Boards and PlatformIO environments
| Env | Board / platform | Notes |
|---|---|---|
| `waveshare-esp32-s3-touch-lcd-43` (root `platformio.ini`) | `esp32-s3-devkitc-1`, `espressif32` (unpinned; resolved 6.11.0), Arduino | 16 MB flash, `qio_opi` PSRAM, native USB CDC console |
| `heltec-v4-sensor` (`nodes/heltec-sensor`) | `esp32-s3-devkitc-1`, Arduino | `dio` flash, 16 MB, lib `bogde/HX711 @ ^0.7.5` (resolved 0.7.5) |
| `heltec-v4-beacon` (`nodes/heltec-radio`) | `esp32-s3-devkitc-1`, Arduino | `dio` flash, 16 MB, no libs |
| `heltec-sensor-test` (`nodes/heltec-sensor-test`) | `esp32-s3-devkitc-1`, Arduino | Serial-only bench test, sends nothing. NTC pin hard-coded to 4. |
| `xiao-esp32-c3` (`nodes/xiao-sensor`) | `seeed_xiao_esp32c3`, Arduino | RETIRED. `upload_flags = --no-stub`. Does not fill the pressure fields. |

Root env build flags (exact): `-std=gnu++17` (with `build_unflags = -std=gnu++11`), `-DARDUINO_USB_MODE=1`, `-DARDUINO_USB_CDC_ON_BOOT=1`, `-DBOARD_HAS_PSRAM`, `-mfix-esp32-psram-cache-issue`, `-DLV_CONF_INCLUDE_SIMPLE`, `-Wl,-u,_printf_float`, `-Iinclude`, `-Icommon`. Other settings: `monitor_speed = 115200`, `monitor_filters = esp32_exception_decoder`, `board_build.arduino.memory_type = qio_opi`, `board_build.flash_size = 16MB`, `board_upload.flash_size = 16MB`.

Node envs use the same `-std`, USB flags, `-Iinclude` and `-I../../common`. The XIAO env adds `-Wl,-u,_printf_float`.

Because USB CDC is on at boot, the console is the chip's native USB (GPIO 19/20). The COM port disappears and re-enumerates on every reset, so serial captures must reconnect.

**Partition table (decoded from the built `partitions.bin`; there is no custom CSV in the repo):** nvs 0x9000/0x5000, otadata 0xE000/0x2000, app0 0x10000/0x330000, app1 0x340000/0x330000, spiffs 0x670000/0x180000, coredump 0x7F0000/0x10000. That is an 8 MB layout although 16 MB flash is configured. The app limit is 3,342,336 bytes. The current `firmware.bin` is 1,855,920 bytes; the linker reports 1,855,553 bytes used (55.5%).

**Board identities** (from the session record): Waveshare COM4, MAC `94:a9:90:df:bb:54`. Radio bridge COM6, MAC `10:bd:a3:5a:7c:94`. Sensor node MAC `F8:5B:1B:BF:3F:EC`. COM numbers are Windows assignments and can change.

### Waveshare ESP32-S3 GPIO map (controller)
| GPIO | Function | Source |
|---|---|---|
| 8 | I2C SDA (shared: CH422G, GT911, XL9535, DS3231M) | `I2C_SDA_PIN` |
| 9 | I2C SCL | `I2C_SCL_PIN` |
| 4 | GT911 touch INT. Held low during touch reset so the GT911 comes up at 0x5D, then released to input. | `display.cpp` |
| 46 / 3 / 5 / 7 | LCD HSYNC / VSYNC / DE / PCLK | `display.cpp` |
| 14, 38, 18, 17, 10 | LCD B0-B4 | `display.cpp` |
| 39, 0, 45, 48, 47, 21 | LCD G0-G5 | `display.cpp` |
| 1, 2, 42, 41, 40 | LCD R0-R4 | `display.cpp` |
| 19 / 20 | Native USB D- / D+ (console and flashing) | hardware |
| 43 / 44 | Go to the onboard CH422G, not a usable UART | notes |
| 15 / 16 | Planned UART to a future Board A. Not used by current firmware. | notes only |

**Do not confuse boards.** GPIO 3, 47 and 48 are LCD pins on the Waveshare and are sensor-node pins on the Heltec (valve shunt 3, pressure SCK 47, pressure DOUT 48).

LCD timing: 16 MHz pixel clock, 800x480, `pclk_active_neg = 1`, framebuffer in PSRAM, `disp_gpio_num = -1`.

### I2C devices on GPIO 8/9
| Address | Device | Notes |
|---|---|---|
| 0x24 | CH422G mode write | `0x01` = output mode. Each CH422G command has its own address, not a register pointer. |
| 0x38 | CH422G output write | EXIO1 = touch reset (active low), EXIO3 = LCD reset. Outputs start at 0xFF. |
| 0x5D (or 0x14) | GT911 touch | Both tried at boot, 0x5D found on the bench |
| 0x21 | XL9535 relay expander | `XL9535_ADDR`. Registers: 0x02 output, 0x04 polarity, 0x06 direction |
| 0x68 | DS3231M RTC | Power from 3.3 V only, never the 5 V coil rail (5 V would reach GPIO 8/9 through the pull-ups) |

The bus clock is set to 100 kHz in `initializeExpanderAndTouchReset()`.

### Relay map (XL9535 port 0, active-high, bench board)
| Bit | Relay | Function | Driven by firmware |
|---|---|---|---|
| 0 | K1 | Aux, switched 12 V. **The bench board's K1 channel is faulty and does not latch. Never energize it.** | No |
| 1 | K2 | Pump | Yes |
| 2 | K3 | Heater mat (about 500 W, 120 V AC per the notes) | Yes |
| 3 | K4 | Case fan. On while the pump runs, for all of recharge, and while the chip is hot. | Yes |
| 4 | K5 | To-Engine valve. Energized = dump regen exhaust to atmosphere (the vent line the pressure sensor watches). | Yes |
| 5 | K6 | From-Engine valve. Energized = pull fresh ambient air into the chamber. | Yes |
| 6 | K7 | Aux AC 1, 120 V AC (future engine heater) | No |
| 7 | K8 | Aux AC 2, 120 V AC (future accessory) | No |

- Active-high, port 1 unused and always written 0.
- Both valves are energized together for recharge and de-energized otherwise. `setValve()` switches both.
- All eight bits are in `RELAY_ALLOWED_MASK`, including K1, K7 and K8, although nothing drives them. Some notes still say K7/K8 are "masked off"; the code does not mask them.
- A custom two-board carrier is planned with a different map (K1-K4 = 120 V AC, K5-K8 = 12 V DC). It is a constant swap of the `RELAY_BIT_*` values in `config.h`. Not built.

**Fail-safe defaults.**
- `RelayDriver::begin()` writes polarity 0, then output 0, then direction 0 (outputs), so every relay is off before the pins become outputs.
- `StateMachine::begin()` and every `stopWith()` call `relay.allOff()`.
- **Unverified:** what the relay board does while the MCU is held in reset, or during the XL9535 power-up window before `begin()`.
- **Unverified:** relay behavior if the firmware hangs. The loop watchdog (5 s) should reset the chip and `setup()` turns every relay off first; Admin > Relay test > Watchdog test checks it (checklist section 4).

### Sensor node (Heltec V4.3) pins, `nodes/heltec-sensor/include/config.h`
| GPIO | Function |
|---|---|
| 17 / 18 | To-Engine SHT40 SDA / SCL (`TwoWire(0)`, 100 kHz) |
| 15 / 16 | From-Engine SHT40 SDA / SCL (`TwoWire(1)`, 100 kHz) |
| 4 | Chamber thermistor ADC. 3V3 - 100K NTC 3950 - GPIO 4 - 10k - GND. 11 dB attenuation. |
| 3 | Valve coil current shunt (1 ohm in the valve 12 V return). Read in firmware, but the **check is disabled on the Waveshare** (shunt not installed). GPIO 3 is an ESP32-S3 strapping pin. |
| 48 | HX711 DOUT (vent pressure). An old note had DOUT and SCK swapped; the code is the authority. |
| 47 | HX711 SCK |

- SHT40 probes are tried at 0x44 then 0x45, command 0xFD, 10 ms conversion wait, CRC-8 checked.
- The HX711 module must run from **3.3 V, not 5 V**.
- Thermistor: 8 samples, trimmed mean. A spread over 150 mV is rejected and the reading is valid only between 80 and 3100 mV. The chamber flag needs 3 stable good reads in a row.
- Radio: `WIFI_STA`, power save off, ESP-NOW channel 1, unencrypted broadcast to FF:FF:FF:FF:FF:FF with no ack or retry.
- Send period: `SEND_INTERVAL_MS` 1000 ms plus read time. Logs show packets about 1.05 s apart.

### Power, thermal and timing constraints in the hardware design
- Heater mat about 500 W on 120 V AC. The 266 F mechanical thermal fuse is the only protection I know of that does not depend on firmware.
- Pump about 18 W. Valve coils 12 V, 4 W each (about 0.33 A). Case fan 12 V up to 0.6 A.
- Chamber held 237-250 F (mean 243.6 F, 37% heater duty) in the 8 h run7 recharge log.
- The desiccant dries slowly. To-Engine RH at the pulses fell about 0.13-0.18 points per minute, and about 6 h at temperature did not reach 15% with 4 lb.
- To-Engine exit air read about 150 F with 10 s pulses and about 130 F with longer pulses (user observation from the display, not in a log).
- The vent baseline raw count differed between sessions (about 1,199,000 on 2026-11-18, about 4,735,000 on 2026-11-22/23). The absolute value means nothing. Only the step above the running ambient average is judged.

---

## 3. Codebase organization and architecture

### Directory tree (tracked and generated, excluding `.git` and `.pio`)
```
aerosentry-controller/
  platformio.ini              Root env: Waveshare controller (the only env in the root project)
  README.md                   Short hardware summary, build commands, checkpoint list (partly stale)
  HANDOFF.md                  This file
  .gitignore                  .pio, .vscode browse db / c_cpp_properties / launch.json / ipch
  .github/
    copilot-instructions.md   Rules Copilot applies to every chat and edit
  .vscode/
    extensions.json           Recommends platformio.platformio-ide (the only tracked .vscode file)
  assets/
    aerosentry-logo.jpg       Source image for the idle/boot logo
  common/                     Wire formats shared by controller and all nodes (-Icommon)
    sensor_packet.h           SensorPacket v3, flags, channel number
    link_packet.h             LinkCommand / LinkAck / LinkStatus and REMOTE_* codes
    link_beacon.h             LinkBeacon hear-test packet (magic 0xB4)
  docs/
    PROJECT_NOTES.md          Running engineering log, newest sections at the end
    ERROR_CODES.md            Stop codes S01-S18
    BENCH_TEST_CHECKLIST.md   What still has to be proven on the hardware, in order
  archive/
    xiao-sensor/              RETIRED XIAO ESP32-C3 node and its known-good binary
  include/                    Controller headers
    config.h                  ALL controller pins, I2C addresses, timings, thresholds, UI colors
    statemachine.h            StateMachine class, enums (SystemMode, Command, StopReason)
    sensors.h                 SensorReading struct and readiness helpers
    relay.h                   RelayDriver (XL9535)
    radio_link.h              Remote command path
    system_clock.h            RTC and software clock API
    display.h                 Display and touch hardware API
    lvgl_v8_port.h            LVGL port API
    lv_conf.h                 LVGL config (16-bit color, 64 KB heap, millis tick, Montserrat 14/20/28/36)
    logging.h                 Logger API (stub, SD disabled)
    logo_img.h                extern for the logo image
    ui.h                      UI API for all screens
    README                    PlatformIO boilerplate
  lib/README, test/README     PlatformIO boilerplate
  test/host/                  Stubs, fakes and scenarios for tools/host_test.py (host simulation)
  src/
    main.cpp                  setup() and loop()
    statemachine.cpp          About 1,240 lines. All cycle logic, thermal checks, stop messages, status text
    sensors.cpp               ESP-NOW receive, packet parsing, readiness checks
    radio_link.cpp            Command queue from the radio bridge, status and ack replies
    relay.cpp                 XL9535 driver
    system_clock.cpp          DS3231M read/write, software fallback in NVS (Preferences)
    display.cpp               CH422G reset, GT911 touch, ESP-IDF RGB panel, flush
    lvgl_v8_port.cpp          LVGL init, two 800x20 DMA draw buffers, touch input
    logging.cpp               SD logger stub; keeps the last stop events in RAM for Admin
    run_state.cpp             Cycle-running flag in NVS and the reset cause, for S18 at boot
    ui.cpp                    uiBegin, uiLoop, uiHandleStartResult
    ui_main.cpp               Main screen, purge/dryer/recharge prompts, 10-tap Admin entry
    ui_dryer_setup.cpp        Manual dryer on/off time
    ui_auto_cycle.cpp         Auto dryer target RH and auto-recharge checkbox
    ui_clock.cpp              Clock editor
    ui_warning.cpp            Warning and choice dialogs, uiWarningActive()
    ui_logo.cpp               Boot splash and 3-minute idle logo
    ui_admin.cpp              Diagnostics: sensors, relays, vent pressure, last stops, firmware, loop max
    ui_relay_test.cpp         Admin > Relay test: pulse any XL9535 pin, read it back; I2C scan; watchdog test
    logo_img.c                4.7 MB generated image data. Do not hand-edit.
  nodes/
    heltec-sensor/            ACTIVE sensor node (env heltec-v4-sensor)
    heltec-radio/             ACTIVE radio bridge (env heltec-v4-beacon)
    heltec-sensor-test/       Bench test, serial only
  tools/
    capture_serial.py         Timed serial capture that survives native-USB resets (--port, --seconds, --reset)
    peek_port.py              Read a port without pulsing DTR/RTS
    reset_and_peek.py         Pulse DTR/RTS then print the boot log
    watch_pressure.py         Endless log writer without reset (--port, --log), reconnects
    remote_bridge.py          HTTP server on port 8080 plus USB serial to the radio bridge
    make_logo.py              Regenerates src/logo_img.c (--input, --output)
    host_test.py              Builds and runs the host simulation (test/host)
  web/
    index.html                Phone control page (uses /api/status and /api/command)
    history.html              History page mockup (fetches /hangar.csv; the notes say its charts are sample drawings)
```

Generated and not tracked: `.pio/`, `nodes/*/.pio/`. Serial logs and helper scripts live outside the repo in `%TEMP%` (section 4).

### Execution model
- **Single-threaded Arduino loop.** No FreeRTOS tasks, timers or interrupts are created anywhere in `src/`. The only concurrency is the ESP-NOW receive callback, which runs in the Wi-Fi task.
- `onEspNowReceive()` copies a sensor packet under `portENTER_CRITICAL(&sensorMux)` into `latestPacket` and sets `packetReady`. Link commands are copied under `linkMux` into `pendingCommand`. The main loop consumes both. Nothing else crosses threads.
- LVGL runs in the same loop (`lvgl_port_loop()` calls `lv_timer_handler()`), with `LV_TICK_CUSTOM` reading `millis()`.
- `displayWritePixels()` can block up to 25 ms for small updates while it waits for the RGB frame-done flag.
- All timers are `millis()` subtraction (unsigned, rollover-safe). Phase timers use `_phaseStartTime` and `phaseElapsed()`.

`loop()` order in `src/main.cpp`:
1. `sensorsUpdate()`: consume one pending packet, update `sensors`, print one `ESP-NOW seq ...` line.
2. `radioLinkUpdate()`: apply a queued remote command, else send a status packet every 2 s.
3. `stateMachine.update()`.
4. If the display is up: once per second update the clock, status line, sensors, outputs and Admin page. On a mode change update the mode label. If `takeStopMessage()` returns text, show a warning with an OK button.
5. `uiLoop()`: idle logo check, then LVGL.
6. `delay(5)`.

`setup()`: Serial, `Wire.begin(8, 9)`, `relay.begin()` (retried after the display init because the touch reset can disturb the bus), display and LVGL, `sensorsBegin()`, logger (disabled), RTC, `stateMachine.begin()`, `uiBegin()`.

### `StateMachine::update()` flow
1. `checkThermal()`, then `learnVentBaseline()`.
2. `handleCommand()` consumes one pending command (STOP, or a start that passes `startBlockReason()`), then updates fan and outputs and returns.
3. If not idle: note a dropout (`Sensor dropout - holding`) or a recovery (`Sensors back after N ms`). Recharge always runs `updateRecharge(sensorsOk)`, which handles a dropout itself (pump off, heater only with a fresh chamber reading, failure cool down once after 30 s). Purge and the dryers hold with no decisions, then stop with the specific sensor-loss code after 30 s.
4. With the sensors OK: end the 2 s start grace, run the per-mode update, then `checkSaturation()` and `maybeLog()`.
5. `checkThermal()` again, `updateFan()`, `applyOutputs()`.

Outputs are never written inside the mode code. The mode code sets `_pumpOn`, `_heaterOn`, `_fanOn` and `_valveEnergized`. `applyOutputs()` compares them with the `_applied*` copies and writes the relay only on change, printing `Pump ON`, `Heater OFF` and so on. `stopWith()` and `checkThermal()` write the relay directly and call `allOff()`.

### Data flow
```
sensor node (1 Hz broadcast) --ESP-NOW ch1--> onEspNowReceive() --> SensorReading `sensors`
                                                                          |
 touch UI / radio command --> StateMachine::command() --> update() --> applyOutputs() --> RelayDriver --> XL9535
                                                                          |
              statusText(), takeStopMessage(), vent*() getters --> UI (main, warning, Admin)
PC browser --HTTP 8080--> remote_bridge.py --USB serial--> radio bridge --ESP-NOW--> radioLinkAccept() --> pending command
```

Packet routing in `onEspNowReceive()`:
- Length equals `sizeof(LinkBeacon)` (8 bytes) with magic 0xB4: a beacon. It is printed and never used for state.
- Length at least 2 with first byte 0xA7: a link command, passed to `radioLinkAccept()`.
- Length equals `sizeof(SensorPacket)` (22 bytes, computed from the struct) with version 3: a sensor packet.
- Anything else is dropped.

Changing a packet struct means reflashing the Waveshare and the sensor node together.

Sensor validity rules (`src/sensors.cpp`): a packet is "fresh" for 15 s (`SENSOR_STALE_MS`). Each humidity or chamber reading must also have arrived within 3 s (`SAMPLE_MAX_AGE_MS`) to count as ready. `chamberRetryPending()` is a 6 s window after the last valid chamber sample.

### Stop reasons and what the user sees
Every stop has a permanent code S01-S18. The full table (cause, what the controller does, what to check) is
`docs/ERROR_CODES.md`. The screen shows the cause, the reading behind it, what to do, and
`<mode> ended. Code Sxx`. Serial prints one line per stop, `Stop: S09 VALVE_SUSPECT mode=Recharge | ...`, and
`Logger::logStop()` receives the same code for the future SD log. Admin shows the last three stops.

Messages stay on screen until OK. `uiShowWarning()` sets `warningActive`, and `uiLogoTick()` will not start the idle logo while it is set. **Never seen working on the display yet.**

### External libraries and dependencies
| Dependency | Version | Where declared | Used by |
|---|---|---|---|
| `lvgl/lvgl` | 8.3.11 (pinned) | root `platformio.ini` `lib_deps` | Controller UI |
| `bogde/HX711` | `^0.7.5`, resolved 0.7.5 | `nodes/heltec-sensor/platformio.ini` | Pressure module |
| Platform `espressif32` | unpinned, resolved 6.11.0 | all envs | Arduino-ESP32 core, ESP-IDF headers (`esp_lcd_panel_rgb.h`, `esp_now.h`, `esp_wifi.h`) |
| PlatformIO Core | 6.2.0 on this PC | not in repo | Builds |
| `pyserial` | not pinned, no requirements file | imported by every `tools/*.py` | Serial tools and bridge |
| Arduino built-ins | with the core | | `Wire`, `WiFi`, `Preferences` (clock fallback in NVS) |

`.pio/libdeps/waveshare-esp32-s3-touch-lcd-43/ESP32_Display_Panel` exists on disk but is not in `lib_deps` and nothing in `src/` or `include/` references it. It is a leftover. The display is driven directly with ESP-IDF `esp_lcd` calls in `src/display.cpp`.

The platform is unpinned, so a fresh machine may resolve a different `espressif32` and break the build. Pinning it is on the roadmap.

---

## 4. Revision tracking, notes and workflow conventions

### Where things live
| What | Where |
|---|---|
| Engineering log (newest sections at the end) | `docs/PROJECT_NOTES.md`. Latest entries: "2026-11-23 sensor dropouts stopped the first auto dryer test" and "2026-11-23 evening state before the handoff". |
| Short summary and checkpoint list | `README.md` |
| Custom carrier board design, power budget, relay remap | `docs/PROJECT_NOTES.md`, section "Custom carrier board design" (text only) |
| Schematics, datasheets, CAD | **None in the repo.** Datasheet figures are quoted inside the notes (Songle relay, Heltec V4.3 pin map). |
| Changelog | None. Git history plus the dated notes sections. |
| Serial logs from bench runs | Outside the repo, in `%TEMP%` (`C:\Users\sjohn\AppData\Local\Temp`): `aerosentry-recharge-run6.log`, `aerosentry-recharge-run7.log`, `aerosentry-recharge-watch.log`, `aerosentry-first-system-test-auto-dryer.log`, `aerosentry-auto-dryer-run2-grace30.log`, `aerosentry-pressure-watch.log` (2026-11-18). They are not backed up and not in git. |
| Analysis helper scripts | Also in `%TEMP%`. The session record names `analyze_dropout.py`, `analyze_dropout2.py`, `max_gaps.py`, `autostart_check.py`, `apply_*.py`. I did not re-list them. Treat them as disposable. |
| Backup of an earlier bad commit | `%TEMP%\aerosentry-garbled-backup`, a directory. **I did not open it**; the notes call it the backup of the garbled 2026-11-20 rewrite (see section 6). |

### Versioning and flags
- **No firmware version string exists.** The only version-like items are `AEROSENTRY_PACKET_VERSION` = 3 (in `common/sensor_packet.h`, must match on both ends), the git tag `bench-2026-11-18` (`35cd9ad`), and the build hash recorded by hand in the notes.
- Identify a flashed image by its SHA-256 and byte count, as the notes do. The image currently in `.pio` is 1,855,920 bytes, SHA-256 `C5A921598D85F755FBF08E8DFFB9D1AC3D7043A0804FC5177BE5D677D9EBAEFD`. It is built, not flashed.
- Compile-time switches in `include/config.h`:

| Switch | Value | Meaning |
|---|---|---|
| `RELAY_DRIVER_ENABLED` | 1 | 0 makes `relay.begin()` return false and all relay writes are skipped |
| `VENT_PRESSURE_CHECK_ENABLED` | 1 | The divert safety check (fails closed) |
| `VALVE_CURRENT_CHECK_ENABLED` | 0 | Needs the shunt wired on the sensor node |
| `LOOP_WATCHDOG_ENABLED` | 1 | Task watchdog on `loop()` (5 s) and the Admin watchdog test |
| `REMOTE_REQUIRE_BRIDGE_MAC` | 1 | Starts only from `REMOTE_BRIDGE_MAC` |
| `REMOTE_RECHARGE_ALLOWED` | 0 | Remote Recharge is refused |
| `DISPLAY_PCLK_HZ` | 14000000 | RGB pixel clock; was 16 MHz (display flashes) |

`FIRMWARE_VERSION` ("1.0.0-beta.1") is printed at boot and shown on Admin.

- Changing a tunable: edit `include/config.h`, rebuild, flash, then add a dated note in `docs/PROJECT_NOTES.md` saying what changed and what was not verified. The previous agent did this every time.

### Conventions found in the code and followed during development
- **One place for settings.** Pins, addresses, thresholds and timings are `#define`s in `include/config.h` with a unit suffix (`_MS`, `_F`, `_C`, `_MV`). Node pins live in `nodes/*/include/config.h`. Do not hard-code new numbers in `statemachine.cpp`.
- **Units.** Sensor temperatures in `SensorReading` are Celsius. Every threshold named `*_F` is Fahrenheit. Convert with `celsiusToFahrenheit()`.
- **Naming.** Classes `PascalCase`, functions and methods `camelCase`, members `_leadingUnderscore`, macros `UPPER_SNAKE`, enum class values `UPPER_SNAKE`, UI functions `ui*`, LVGL callbacks `cb*`, file-local helpers `static`.
- **File layout.** One UI screen per `ui_*.cpp`, all declared in `include/ui.h`. Logic never touches LVGL; the UI reads `stateMachine` getters.
- **No blocking in the controller loop** except the 5 ms `delay` and the bounded display wait. Use `millis()` deltas.
- **Output discipline.** Only `applyOutputs()` and the stop paths touch the relay. Mode code sets intent flags.
- **Comments explain why and carry a date** where a number came from a bench result (for example "2026-11-23").
- **Serial log lines are part of the interface.** Analysis scripts search for `Auto phase`, `Recharge phase`, `Vent pressure raw`, `Sensor dropout - holding`, `Sensors back after`, `Stop:`, `Mode ->`, `Pump ON/OFF`. Do not reword them casually.
- **Line endings are CRLF** in every file I checked (`README.md`, `PROJECT_NOTES.md`, `statemachine.cpp`, `config.h`). `PROJECT_NOTES.md` is UTF-8 with no BOM. Keep both. One damaged character sequence (a mojibake dash) remains in the notes.
- **Commit style** so far: one short imperative sentence ("Add vent pressure check, two valves, and bench test mode.").

### Documents and comments that are stale. The code wins.
| Where | What is wrong |
|---|---|
| `docs/PROJECT_NOTES.md` "Audit restoration 2026-11-20" | Says there is no 120 F cooldown exit and the loose 10 s / 10 min pulses were removed. Both are in the code. |
| `docs/PROJECT_NOTES.md` "V1 boundaries" and "Next: Heltec V4" | Plan around the XIAO and MeshCore. The XIAO is retired (`archive/`). |
| `docs/PROJECT_NOTES.md` valve sections | Say K7/K8 are "masked off" and describe the vent check before it failed closed. The code enables all eight channels; see `docs/ERROR_CODES.md` for the current vent rules. |
| `docs/PROJECT_NOTES.md` "Cycle rules" | Marked superseded in place (2026-11-24). |

Fixed on 2026-11-24: the README, the XIAO comments in `sensors.cpp`/`sensors.h`, the 15 s pulse comment, the
"Chamber retry, 6s max" status text, and the "Result: pending" line.

### Build, flash and capture commands (Windows, PowerShell, from the repo root)
All three environments and the host simulation were built on 2026-11-24 with no warnings.
```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
$py  = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
& $pio run                                                   # build the controller
& $pio run -d nodes/heltec-sensor                            # build the sensor node (not run in this step)
& $pio run -d nodes/heltec-radio                             # build the radio bridge (not run in this step)
& $pio run --target upload --upload-port COM4                # flash the Waveshare (ONLY when the user says so)
& $py tools/capture_serial.py --port COM4 --seconds 20 --reset
& $py tools/host_test.py                                     # host simulation, no board needed
```
- The host simulation needs PlatformIO's MinGW compiler once: `& $pio pkg install -g --tool "platformio/toolchain-gccmingw32"`. It compiles the real `statemachine.cpp`, `sensors.cpp`, `relay.cpp`, `radio_link.cpp` and `logging.cpp` against `test/host/stubs`, simulates the chamber, sensor node, vent pressure and XL9535 registers, runs 27 scenarios, and checks at every step: heater only in recharge with a fresh chamber reading; recharge pump only with the valves open and fresh pressure; valves only in recharge; all relays off when idle; K1/K7/K8 never driven. Add a scenario for any logic change.
- The Waveshare is the board on COM4 (MAC `94:a9:90:df:bb:54`). The radio bridge is COM6. Always pin `--upload-port`. Without it PlatformIO can pick the wrong board.
- A running `capture_serial.py` holds the port. Stop it before flashing. At 23:08 two such processes (PIDs 9992 and 24636) were still waiting to reconnect to COM4.
- The previous agent read the image back with `esptool.py verify_flash` after each flash. The exact arguments are not recorded in the repo. The app image sits at 0x10000 per the partition table, and `verify_flash` exists in the installed esptool. I did not run it.
- `pio pkg list` crashed once with a `UnicodeEncodeError` when its output was piped. `pio run` was unaffected.

---

## 5. Current status: working vs. incomplete

### 5.1 Verified on hardware before 2026-11-24 (do not break these)
| Feature | Evidence | Caveat |
|---|---|---|
| XL9535 relay writes | 1,180 output-register writes in the run7 recharge log, 48 in dryer run 2, all with I2C result 0 | |
| Heater regulation at 245 F | Run7 (8 h): chamber 237-250 F, mean 243.6 F, 37% heater duty | |
| Recharge pulses and vent check | Run7 and the stopped run: steps about 79,000-133,000 against the 20,000 gate, 55 of 55 passed in one run | A disconnected line stops on pulse 1 (bench, 2026-11-20) |
| Recharge max-run latch at 6 h | Run7 reached the latch | The cool down after it was the buggy one (Celsius vs 120) |
| No false saturation stop in recharge | Run7, 8 h with 10 s pulses | |
| Auto dryer hourly cycle | Dryer run 2: 10 hourly samples in 10.3 h, no stop | Build without the sample fix |
| Sensor dropout grace | Dryer run 2: 3 dropouts, recovered in 105-171 ms | The 30 s limit has never been reached on hardware |
| Touch, display, clock, UI navigation | 2026-11-18 bench and later | Flashes (see 5.2) |

### 5.2 Built 2026-11-24, not yet on hardware (branch `v1-beta`)
All of this is in `docs/BENCH_TEST_CHECKLIST.md`, with pass criteria. Summary:
- Stop codes S01-S18 with cause, reading, action and code; one serial line per stop; Admin shows the last 3.
- Loop watchdog, relays off first in `setup()`, S18 at boot after a mid-cycle restart or any watchdog / crash /
  brownout reset; Admin > Relay test > Watchdog test.
- Recharge: vent check fails closed (S08, S09); cool down and safety pulses judged; sensor loss stops the pump at
  once, then a failure cool down entered once that advances on time; heat-up timeout cools down first.
- Readiness: a valid sample within 3 s (To, From, pressure); the chamber keeps the stricter rule.
- Remote: no remote Recharge, bridge-only starts, Stop from anyone, Auto Recharge armed only by a local start.
- UI: new Recharge prompt with Back, unattended-recharge warning, synced Auto Recharge checkboxes, valves on the
  main screen.
- Display pixel clock 14 MHz (flash mitigation). Relay test screen and boot I2C scan (K1 investigation).
- Sensor node sends each packet twice; the controller keeps one copy.
- `Auto phase 4` printed at auto dryer start; the auto dryer sample fix (start in `SAMPLE_RUN`, 10 s pump,
  10 s settle) is also unflashed.
- Recharge cool down to 120 F, heat-up backstop, safety check after a real cool down, thermal trips at 255 F /
  250 F: still never run on hardware (simulated only).

### 5.3 Known issues still open
1. **K1 relay channel** reported dead on three boards. Bench 2026-11-24: the Waveshare's CH422G answers at every
   XL9535 address and ANDs `26 FD` into every read at 0x21, so bit 0 (K1) always reads 0 while writes are clean.
   That explains a "dead" K1 under any read-modify-write driver or readback display. Next: drive P0.0 from
   Admin > Relay test and listen for K1 (checklist section 3). Long term, a separate I2C bus for the relay board
   if reads are ever needed.
2. **Display flashes:** cause identified (byte-swapped frames from PSRAM starvation); 14 MHz is a mitigation, not
   proven. Next steps: 12 MHz, then Arduino core 3.x (ESP-IDF 5) with RGB bounce buffers.
3. **Warning message vs screen saver:** the fix has never been seen working.
4. **Vent reference (2026-11-25)** is the median of the pump-off readings from the 6 s before each pulse, not a running
   average. Real pulse rises on 2026-11-25 were only 21,000-28,000 (earlier runs 70,000-130,000), so there is little margin.
5. **Remote link is a MAC filter, not authentication.** ESP-NOW is unencrypted. Real authentication needs
   encrypted unicast or a signed counter in `LinkCommand` (all three boards rebuilt).
6. The 1 h heat-up backstop is not the user's number. The desiccant amount (4 lb vs about 2 lb) is undecided.
7. Valve coil current check is disabled until the shunt is wired.

### 5.4 Roadmap
**Beta (now):** run `docs/BENCH_TEST_CHECKLIST.md` top to bottom. Exit criteria for v1.0: one unattended
recharge ending S16 with the cool down reaching 120 F; a 12 h auto dryer run with a correct first sample; the
stop message staying until OK; no display flashes in 50 page changes; then tag the tested commit.

**After v1.0:**
- SD event and history logging. `Logger::logStop()` already receives every stop code; the CSV formats are in
  `src/logging.cpp`. The Type B has no TF slot on the default pins; confirm the pins first.
- Arduino core 3.x / ESP-IDF 5 for RGB bounce buffers (display), only with the display in front of you.
- Authenticated remote link; MeshCore or Wi-Fi/MQTT path (channel must match ESP-NOW channel 1).
- Custom two-board carrier (relay remap is a `RELAY_BIT_*` change in `config.h`), K9 heater disconnect.
- Split `statemachine.cpp` (recharge, auto dryer, thermal) once the beta behavior is verified, never in the same
  commit as a behavior change. The host simulation makes that refactor safe to check.

## 6. Critical "Do not touch / gotchas" warnings

### Hardware quirks
- **K1 (bit 0) on the bench relay board does not latch. Never energize it.** K7 and K8 are 120 V AC outputs. Treat them as mains switching. No cycle drives K1, K7 or K8.
- **XL9535 protocol.** Two data bytes follow the register: port 0, then port 1. Single-byte PCF8574-style writes do not address an XL9535 output register and must not be used. The Google Drive samples that came with the relay board describe a different PCF8574 board. Do not use them.
- **GPIO numbers mean different things per board.** On the Waveshare, 3, 47 and 48 are LCD pins. On the Heltec sensor node they are valve shunt, HX711 SCK and HX711 DOUT. Do not move a pin define between the two `config.h` files.
- **HX711 on 3.3 V only.** At 5 V its DOUT pull-up would put about 5 V on a 3.3 V pin.
- **DS3231M on 3.3 V only**, never the 5 V relay rail (it would reach GPIO 8/9 through the pull-ups).
- **Heltec V4.3 silkscreen shows header pin numbers, not GPIO names.** The confirmed map is in `docs/PROJECT_NOTES.md` (carrier board section).
- **Waveshare touch reset order matters.** GT911 INT (GPIO 4) is held low through the reset so the chip comes up at 0x5D. The relay driver is re-initialized after the display starts because the touch reset can disturb the I2C bus. Keep that order in `setup()`.
- **USB CDC console.** The COM port vanishes and returns on every reset. A running `capture_serial.py` holds the port and blocks flashing. Always pass `--upload-port COM4`.
- **USS-PSV00033 valves are the wrong part** (1/8 inch pneumatic pilot valves, not diverters). Do not buy them again. Vent pressure steps recorded with them (+5,500 and +19,253 counts) were choked-orifice pressure, not a divert. Do not tune `VENT_PRESSURE_MIN_STEP` from them. (The 2026-11-20 and later recharge logs show steps of 72,000-133,000 once the plumbing diverted properly.)
- **The 266 F fuse is hardware.** Firmware must not pretend to own it.

### Timing and logic sensitivities
- **Temperatures are Celsius in `SensorReading`.** Comparing `sensors.chamberTemp` to 120 directly ended every cool down at once (120 C is 248 F). Always call `celsiusToFahrenheit()`.
- **Recharge must ignore the 40% saturation stop.** To-Engine reads near 100% in the regen exhaust by design. Re-adding saturation to recharge caused a false stop about 1 h in.
- **Cool down keeps the valves open.** The pump pulses 10 s every 10 min and exhaust goes outside. Closing the valves during cool down would let hot wet air toward the engine. The cool down ends at 120 F on purpose (a hangar can be that hot in summer, so it must not wait for a lower temperature).
- **Vent peak must be tracked every loop** while the pump is on. Judging only the last packet before the phase timer is wrong. `_ventPulsePeak` is reset after each judgment so each pulse stands alone.
- **The vent reference uses pump-OFF samples only** (median of the last 6 s before each pulse, `freezeVentReference()`). Never take it while the pump runs; a pulse with no steady reference does not run (S21 after 2 min).
- **Readiness means a valid sample within 3 s** (`sampleFresh()` in `sensors.cpp`) for To-Engine, From-Engine and pressure. Requiring the latest packet's flag made one bad read look like a lost sensor (the host simulation showed a stalled recharge). The chamber keeps the stricter rule because it gates the heater.
- **Never write NVS from a periodic path.** A flash write blocks the PSRAM bus and can glitch the RGB display. `run_state` writes only when a cycle starts or ends.
- **Every pump pulse in a recharge needs a fresh vent reading** and is judged; a pulse with no pressure sample is not judged and does not count (`judgeVentPulse()`).
- **Do not use a per-sensor 3 s freshness test as a stop condition.** That did exactly that in dryer mode on 2026-11-20 and stopped cycles whenever two radio frames in a row were lost (a 3.17 s gap). The 30 s grace replaced it. Do not bring back `SENSOR_LOST_HOLDOUT`, `HOLDOUT_RETRY_MS` or `chamberHoldoutPending()`. The old grace also had a hold condition that never ended.
- **A fresh chamber reading is required to turn the heater on.** Keep that rule. The only exception is the existing hold of the previous command for up to 6 s (`CHAMBER_RETRY_MS`). The thermal check cannot run on a stale reading.
- **Auto dryer sampling.** Reading From-Engine right after the pump starts returns old chamber air (13-15.7%). Waiting 30 s with the pump off lets engine air mix with dry chamber air and reads falsely low. The chosen pattern is 10 s pump, 10 s settle, read. The SHT40 is rated accurate at about 6 s.
- **Long pump pulses cool the beads.** 30 s and 15 s pulses dropped To-Engine exit air from about 150 F to about 130 F. `RECHARGE_LONG_PULSE_ON_MS` is 10 s for now. Raising it was tried and rejected for now.
- **Stay on ESP-NOW channel 1 and do not add Wi-Fi station or AP joins** to the controller or either Heltec without a plan. ESP-NOW and Wi-Fi share one radio, and the channel must match the access point. The notes say not to join the Heltec to Wi-Fi for now.
- **Do not flash the stock MeshCore ESP-NOW bridge** onto the radio board. The notes record that decision.
- **Sensor packet and link structs are shared.** Change `common/*.h` only with the Waveshare, the sensor node and the radio bridge rebuilt and flashed together. The controller drops any packet whose version is not 3.
- **`displayWritePixels()` was restored byte-for-byte to the `bench-2026-11-18` behavior.** Small updates wait for the frame-done flag. Large windows are not gated, because gating them made the flicker worse. Do not "fix" the flicker by editing this without the display in front of you.

### Past dead ends (already tried and rejected)
| Idea | What happened |
|---|---|
| SD card logging on the Waveshare Type B | `SD.begin()` fails with `f_mount (3)`: no TF slot on the default pins. The open/print/close stalled the LVGL flush thread. Logger is disabled (`src/logging.cpp`). Do not call `SD.begin()` again without confirming pins. |
| Humidity-trend valve check (From-Engine climbing) | Unreliable in humid air and with a humid engine. Replaced by the vent pressure check. Code kept, compiled out. |
| Single coil-current check | Built, disabled. The shunt is not wired. The `valveMv` reading sat near 250-280 mV with relays off. Ignore it until the shunt exists. |
| 30 s then 15 s long recharge pulses | Cooled the beads (see above). Back to 10 s. |
| A single "lowest" vent baseline | Replaced by the running average of pump-off samples. |
| A "garbled" rewrite on 2026-11-20 | A commit mixed contradictory notes and logic (valves both open and closed in cool down, no early exit). It was reverted to the bench-validated state and a backup kept in `%TEMP%\aerosentry-garbled-backup` (per the notes; contents not inspected). Re-check any claim from that period against the code. |
| Recharge at 240 F, 3 h and 4 h limits | Superseded: 245 F target, 6 h counted from first reaching target. A 3 h run still at 100% humidity hard-stopped with no cool down. |
| XIAO ESP32-C3 sensor node | Retired for the Heltec V4.3. |
| `ESP32_Display_Panel` library | Left in `.pio/libdeps` but unused. The display is driven directly with `esp_lcd`. |
| Charts on `web/history.html` | Sample drawings. No data source exists (`/hangar.csv` is not served). |
| Flux (schematic tool) | Credits ran out in about two hours with no schematic. The written spec in the notes is the deliverable. Use KiCad or EasyEDA if the carrier board is built. |

### Rules the user stated (keep following them)
- Capture logs, but do not poll or monitor them.
- State plainly what is unverified. Do not report a guess as fact.
- Do not flash, and do not start or stop a test, without being told.
- Mechanical safety stays in hardware (266 F fuse). Do not move it into firmware.
- Heater diagnostic pulses stay disabled on the future custom board until a test-energy budget is written down.

### Environment gotchas
- When editing with shell heredocs, `\\n` can collapse to `\n` before the script sees it. Write edit scripts to a file instead.
- Windows PowerShell paths. `pio.exe` lives under `%USERPROFILE%\.platformio\penv\Scripts`.
- `docs/PROJECT_NOTES.md` is about 80 KB. Read it by section (`Select-String -Pattern '^## '`), not whole.
- `src/logo_img.c` is 4.7 MB of generated data. Do not open it in an editor tool or search it.
- PowerShell can show UTF-8 text as garbled characters. Check the bytes before concluding a file is corrupt. Only `PROJECT_NOTES.md` has a real damaged sequence left.
- The real Wi-Fi channel, hangar temperature and wiring are not visible from the repo. Ask the user.
