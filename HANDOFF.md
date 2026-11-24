# AeroSentry Controller: Handoff to GitHub Copilot (Agent Mode)

Written 2026-11-23, about 23:10 local, by the previous agent (Cline) at the end of a bench-tuning session.
Repo: `https://github.com/Shawnson333/aerosentry-controller`, branch `main`, HEAD `accffcb`.

**How far to trust this file.** Every pin, constant, path and behavior below was read from the files on disk on 2026-11-23. Anything that could not be checked is marked **Unverified**. Where an older document disagrees with the code, the code wins, and the stale document is listed in section 4.

## READ THIS FIRST

1. **The Waveshare controller may have dropped off USB during the second auto dryer run.** The serial capture `C:\Users\sjohn\AppData\Local\Temp\aerosentry-auto-dryer-run2-grace30.log` ends at 22:46:15 with `[serial disconnected; waiting for reconnect]`, about 10.3 h into the run. At 23:08 Windows listed only COM6 (the Heltec radio). COM4 (the Waveshare) was gone. The log has no `Stop:` line and no second boot banner. The cause is **not known** (cable, power loss, or a crash that never re-enumerated are all possible; none was checked). The pump state at that moment is unknown. Ask the user to look at the hardware before anything else.
2. **Do not flash the Waveshare until the user says the dryer test is over.** The build in `.pio` includes the auto dryer sampling fix (section 5). It is NOT on the board. The board runs the previous build (30 s sensor-loss grace, no sample fix).
3. **Almost everything since `accffcb` is uncommitted.** `git status` before these two new documents were added: 16 modified files, 2 untracked (`tools/peek_port.py`, `tools/reset_and_peek.py`). The last tagged known-good state is `bench-2026-11-18` (`35cd9ad`), which predates most of the current safety logic.
4. **User working style:** capture logs, do not poll or monitor; say plainly what is unverified; never flash without being told; always pin the upload port; do not guess.

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
| Recharge | See the sequence below. Both valves energized the whole time. | Normal completion, or any stop reason in the table in section 3. | To-Engine and chamber thermistor |

Every mode also ends on `THERMAL` and, after the 30 s grace, `SENSOR_LOST`. Saturation stop applies to Purge, Manual and Auto only. If "Allow Auto Recharge" is on, saturation during Manual or Auto starts a recharge and resumes that dryer cycle after the safety check passes. **That auto-recharge path has not been exercised in any log I have.**

**Auto dryer phases** (the `AutoPhase` enum value in brackets is what the serial log prints as `Auto phase N`):
- `SAMPLE_RUN` [4]: pump 10 s (`AUTO_SAMPLE_RUN_MS`). Then `SAMPLE_SETTLE` [5]: pump off 10 s in source (`AUTO_SAMPLE_SETTLE_MS`; 30 s in the flashed build). Then read From-Engine. At or below target (default 20%, range 15-30) go to `WAITING`; above target go to `DRYING`.
- `DRYING` [0]: pump on until From-Engine is at or below target (readings ignored for the first 10 s) or 10 min (`AUTO_MAX_RUN_MS`), then `COOLDOWN` [1] for 10 min, then `QUICK_SAMPLE` [2] (pump 10 s, settle, read). Up to 3 quick cycles (`AUTO_MAX_QUICK_CYCLES`), then `WAITING`.
- `WAITING` [3]: pump off 1 h (`AUTO_SAMPLE_INTERVAL_MS`), then `SAMPLE_RUN`.
- **Source vs board:** in the source tree the cycle STARTS in `SAMPLE_RUN`. The flashed build starts in `DRYING` and reads From-Engine within about 1 s of pump start. That read was old chamber air (13-15.7%), which is the bug the sample fix addresses.

**Recharge sequence** (`RechargePhase`, printed as `Recharge phase N`):
1. `HEATING` [0]: both valves energized at mode entry, heater on. Heater ramps 30 s on / 15 s off until the chamber first reaches 245 F. Pump off. Backstop: if 245 F is not reached within 1 h, stop with `HEATUP_TIMEOUT`.
2. `PULSE_ON` [1]: pump on 10 s. Peak vent pressure is tracked every loop. At the end it needs a fresh From-Engine sample (else pump off and wait up to 30 s, then failure cool down), then the vent pressure check (a miss stops the cycle), then it counts To-Engine samples at or below 15% (three in a row go to cool down).
3. `PULSE_WAIT` [2]: pump off 60 s, then `PULSE_ON`. Heater holds 245 F: off at 245, on at 240.
4. Max run: 6 h counted from the moment the chamber first reaches target. Latched once, then `COOLDOWN`.
5. `COOLDOWN` [3]: heater off, case fan on, valves still open, pump 10 s every 10 min, ends at chamber 120 F or below, or after 2 h.
6. `SAFETY_PULSE` [4] pump 10 s, `SAFETY_WAIT` [5] 60 s, then To-Engine must read 15% or below. Twice in a row completes the cycle (`RECHARGE_COMPLETE`, or `RECHARGE_MAX_TIME` if the 6 h limit started the cool down). Any sample above 15% stops with `SAFETY_FAILED`.

### Non-negotiable operational logic and safety interlocks
| Interlock | Where | Value |
|---|---|---|
| Software thermal trip, chamber | `THERMAL_CHAMBER_F` = `RECHARGE_SOFT_OVERTEMP_F` in `include/config.h` | 255 F, stops everything, latched until it falls, blocks new starts |
| Software thermal trip, humidity probes | `THERMAL_SENSOR_F` | 250 F on either SHT40 |
| Mechanical thermal fuse | Hardware only. Nothing in firmware reads it. `RECHARGE_HARD_OVERTEMP_F` is documentation. | 266 F |
| Vent pressure check | `evaluateVentPressure()` | Each pump pulse must reach ambient average + 20,000 counts at any point, or stop with `VALVE_SUSPECT` immediately |
| Sensor loss | `StateMachine::update()` | 30 s grace (`SENSOR_LOSS_GRACE_MS`), then stop. In recharge after the chamber reached target, a loss goes to the venting failure cool down instead. |
| Heater without a fresh chamber reading | `update()` and `regulateHeater()` | Never newly enabled without a fresh (3 s) chamber sample. `regulateHeater()` may hold the PREVIOUS command for up to 6 s (`CHAMBER_RETRY_MS`) after the last valid sample. During the 30 s dropout hold the heater is forced off as soon as the sample is stale. |
| Heat-up backstop | `RECHARGE_HEATUP_MAX_MS` | 1 h (previous agent's pick, not the user's) |
| Recharge max run | `RECHARGE_MAX_MS` | 6 h from first reaching target |
| Cooldown before the safety check | `RECHARGE_COOLDOWN_MS`, `RECHARGE_FAILED_FAN_TEMP_F` | Ends at 120 F or after 2 h |
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
- **Unverified:** relay behavior if the firmware hangs. There is no watchdog in the code (section 5, P0).

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
    PROJECT_NOTES.md          Running engineering log, about 680 lines, newest sections at the end
  firmware/
    xiao-sensor-known-good.bin  Binary of the retired XIAO sensor firmware
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
  lib/README, test/README     PlatformIO boilerplate. No unit tests exist.
  src/
    main.cpp                  setup() and loop()
    statemachine.cpp          About 1,240 lines. All cycle logic, thermal checks, stop messages, status text
    sensors.cpp               ESP-NOW receive, packet parsing, readiness checks
    radio_link.cpp            Command queue from the radio bridge, status and ack replies
    relay.cpp                 XL9535 driver
    system_clock.cpp          DS3231M read/write, software fallback in NVS (Preferences)
    display.cpp               CH422G reset, GT911 touch, ESP-IDF RGB panel, flush
    lvgl_v8_port.cpp          LVGL init, two 800x20 DMA draw buffers, touch input
    logging.cpp               Disabled SD logger (all no-ops)
    ui.cpp                    uiBegin, uiLoop, uiHandleStartResult
    ui_main.cpp               Main screen, purge/dryer/recharge prompts, 10-tap Admin entry
    ui_dryer_setup.cpp        Manual dryer on/off time
    ui_auto_cycle.cpp         Auto dryer target RH and auto-recharge checkbox
    ui_clock.cpp              Clock editor
    ui_warning.cpp            Warning and choice dialogs, uiWarningActive()
    ui_logo.cpp               Boot splash and 3-minute idle logo
    ui_admin.cpp              Diagnostics: sensors, relays, vent pressure panel
    logo_img.c                4.7 MB generated image data. Do not hand-edit.
  nodes/
    heltec-sensor/            ACTIVE sensor node (env heltec-v4-sensor)
    heltec-radio/             ACTIVE radio bridge (env heltec-v4-beacon)
    heltec-sensor-test/       Bench test, serial only
    xiao-sensor/              RETIRED XIAO ESP32-C3 sensor node
  tools/
    capture_serial.py         Timed serial capture that survives native-USB resets (--port, --seconds, --reset)
    peek_port.py              Read a port without pulsing DTR/RTS (untracked)
    reset_and_peek.py         Pulse DTR/RTS then print the boot log (untracked)
    watch_pressure.py         Endless COM4 log writer, hard-coded to COM4 and a Temp path
    remote_bridge.py          HTTP server on port 8080 plus USB serial to the radio bridge
    make_logo.py              Regenerates src/logo_img.c. Hard-coded input path under Downloads.
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
3. If not idle and the required sensors are not OK: start or continue the dropout timer, print `Sensor dropout - holding`, and hold for 30 s (no decisions; recharge regulates the heater only with a fresh chamber reading). After 30 s: stop with `SENSOR_LOST`, or the failure cool down in recharge once the chamber had reached target.
4. Otherwise print `Sensors back after N ms` if recovering, end the 2 s start grace, run the per-mode update, then `checkSaturation()` and `maybeLog()`.
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
`StopReason` in `include/statemachine.h`; text in `StateMachine::stopWith()`.

| Reason | Trigger | On-screen text (first line) |
|---|---|---|
| `USER` | Stop button or remote STOP | none |
| `SATURATED` | To-Engine above 40% for 30 s, pump on, not recharge | "Desiccant saturated." |
| `SENSOR_LOST` | Required sensors missing for 30 s | "Sensor node not responding." or names the sensor |
| `RECHARGE_COMPLETE` | Three dry samples then two safety samples pass | "Recharge complete." |
| `RECHARGE_MAX_TIME` | 6 h at temperature, then cool down and safety check pass | "Recharge stopped. 6 hours at temperature..." |
| `HEATUP_TIMEOUT` | 245 F not reached in 1 h | "Chamber did not reach target temperature." |
| `THERMAL` | Chamber over 255 F or a probe over 250 F | "Thermal limit reached." |
| `VALVE_SUSPECT` | A pulse missed the +20,000 vent step | "Valve position suspect." |
| `VALVE_NO_CURRENT` | Coil current check (disabled) | "Valve coil fault." |
| `SAFETY_FAILED` | Safety sample above 15% | "Safety check failed." |

Messages stay on screen until OK. `uiShowWarning()` sets `warningActive`, and `uiLogoTick()` will not start the idle logo while it is set. **This has been compiled and flashed but never seen working on the display.**

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
| `VENT_PRESSURE_CHECK_ENABLED` | 1 | The divert safety check |
| `VALVE_CURRENT_CHECK_ENABLED` | 0 | Needs the shunt wired on the sensor node |
| `VALVE_TREND_CHECK_ENABLED` | 0 | Retired humidity ladder. **Defined twice** (lines 117 and 132). |
| `RECHARGE_TEST_MODE` | 1 | Both branches now hold the same values (245 F, 5 F), so it does nothing |

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
| `docs/PROJECT_NOTES.md` "Cycle rules" | Says recharge target 240 F, stop at 3 h, thermal 260 F, 6 s chamber retry, auto sample pause 30 s. Code: 245 F, 6 h from target, 255 F, 30 s grace, 10 s settle. |
| `docs/PROJECT_NOTES.md` "Audit restoration 2026-11-20" | Says there is no 120 F cooldown exit and the loose 10 s / 10 min pulses were removed. Both are back in the code. |
| `docs/PROJECT_NOTES.md` "V1 boundaries" and "Next: Heltec V4" | Plan around the XIAO and MeshCore. The XIAO is retired. |
| `docs/PROJECT_NOTES.md` valve sections | Say K7/K8 are "masked off". The code enables all eight channels. |
| `README.md` lines 28 and 32 | Refer to the XIAO packet and an unwritten Heltec firmware. |
| `src/sensors.cpp` line 3, `include/sensors.h` line 8 | Header comments say the packet comes from the XIAO. |
| `src/statemachine.cpp` line 557 | Comment says the long pulse is 15 s. It is 10 s. |
| `src/statemachine.cpp` status text | `"Chamber retry, 6s max"` is hard-coded and no longer matches the 30 s grace. |
| `docs/PROJECT_NOTES.md` "First system operating test" | Still ends "Result: pending". The 2026-11-23 evening section has the current state. |

### Build, flash and capture commands (Windows, PowerShell, from the repo root)
These come from `README.md` and the session record. Only `pio run` for the controller was actually run during this handoff, and it succeeded.
```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
$py  = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
& $pio run                                                   # build the controller
& $pio run -d nodes/heltec-sensor                            # build the sensor node (not run in this step)
& $pio run -d nodes/heltec-radio                             # build the radio bridge (not run in this step)
& $pio run --target upload --upload-port COM4                # flash the Waveshare (ONLY when the user says so)
& $py tools/capture_serial.py --port COM4 --seconds 20 --reset
```
- The Waveshare is the board on COM4 (MAC `94:a9:90:df:bb:54`). The radio bridge is COM6. Always pin `--upload-port`. Without it PlatformIO can pick the wrong board.
- A running `capture_serial.py` holds the port. Stop it before flashing. At 23:08 two such processes (PIDs 9992 and 24636) were still waiting to reconnect to COM4.
- The previous agent read the image back with `esptool.py verify_flash` after each flash. The exact arguments are not recorded in the repo. The app image sits at 0x10000 per the partition table, and `verify_flash` exists in the installed esptool. I did not run it.
- `pio pkg list` crashed once with a `UnicodeEncodeError` when its output was piped. `pio run` was unaffected.

---

## 5. Current status: working vs. incomplete

### 5.1 Verified and working (do not break these)
"Evidence" says what I actually saw. A feature with no hardware evidence is in 5.3, not here.

| Feature | Evidence | Caveat |
|---|---|---|
| Controller builds | `pio run` on 2026-11-23: SUCCESS, RAM 34.1% (111,580 B), flash 55.5% (1,855,553 B used; `firmware.bin` is 1,855,920 B) | Incremental build, so warnings from cached objects would not re-appear. The build printed no warning lines. A clean build was not done. |
| XL9535 relay writes | 1,180 output-register writes in the run7 recharge log, 48 in dryer run 2, all with I2C result 0 | Says nothing about the relay board's behavior in reset (section 2) |
| Heater regulation at 245 F | Run7 (8 h): chamber 237-250 F, mean 243.6 F, 37% heater duty | The heat-up ramp itself was seen in earlier runs (about 12.6 min to target in one log) |
| Recharge pulses and vent check | Run7 and the stopped run: `Vent pressure raw ... OK`, steps about 79,000-133,000 against the 20,000 gate, 55 of 55 passed in one run | Needs the real To-Engine plumbing. With a disconnected line it stops on pulse 1 (bench, 2026-11-20). |
| Recharge max-run latch at 6 h | Run7 reached the latch and entered cool down | The cool down it entered was the buggy one (Celsius compared with 120) |
| No false saturation stop in recharge | Run7 ran 8 h with 10 s pulses and no stop | The earlier stop came from 30 s pulses; those are no longer used |
| Auto dryer hourly cycle | Dryer run 2: 10 consecutive `Auto phase 4` samples about 1.01 h apart, no `Stop:` line, in 10.3 h | The board is the build WITHOUT the sample fix |
| Sensor dropout grace | Dryer run 2: 3 `Sensor dropout - holding` events, recovered after 105, 171 and 167 ms | The 30 s limit has never been reached on hardware |
| Radio sensor link | Flags 0x0F on every packet in the logs I read; loss about 0.4% in the dryer log | Cause of the loss is unknown |
| Touch, display, clock, UI navigation | Observed working on the 2026-11-18 bench test and in later sessions | Flicker and message issues below |
| Remote page and bridge | Worked on 2026-11-11 (Stop acknowledged, Purge refused with relays absent) | Relays are connected now, so the refusal path changed. Remote Recharge from the page was never recorded. The `/api/status` mode is stale (5.2). |

### 5.2 Known bugs and quirks (unresolved)
1. **Possible USB drop on the Waveshare** (top of this file). Cause unknown.
2. **Sensor loss in recharge never advances the cool down.** In `update()`, once the 30 s grace is spent and the chamber had reached target, `rechargeFailureCooldown()` runs again on every pass while the sensors stay missing. It calls `setRechargePhase(COOLDOWN)`, which restarts the phase timer, so the cool down cannot finish. The heater is off. The pump keeps whatever state it had. Valves stay open. This was noticed, not fixed. It resolves only when the sensors return.
3. **Vent pressure check fails open.** `evaluateVentPressure()` returns true (no check) when `sensors.pressureValid` is false or the baseline was never learned. A dead or unplugged HX711 therefore removes the divert verification without any message. Whether that is acceptable is the user's decision (P0 below). I did not change it.
4. **The radio command path is unauthenticated.** ESP-NOW is unencrypted. `radioLinkAccept()` accepts any frame of the right length whose first byte is 0xA7. It can start the pump or a recharge from any device on channel 1. A remote Recharge also skips the on-screen "Disconnect both engine lines. Heaters will be HOT." prompt. The HTTP page has no login; it relies on Tailscale and the localhost/Tailscale-only bind. A refused remote start still overwrites `config.allowAutoRecharge`.
5. **No watchdog code.** There is no `esp_task_wdt` or similar anywhere in `src/`. What the Arduino core does by default on a hang was not checked. After a reboot the controller is idle with every output off. It does not resume a cycle.
6. **Warning message disappearing after the screen saver** was seen again in run7. The fix (`uiWarningActive()`, `uiLogoHide()`) is compiled and flashed but has never been seen working on the display.
7. **Magenta/yellow flicker** on about 1 in 10 page changes. Unresolved. The background is 0x0A1628 in every build.
8. **Radio frame loss** of about 0.4% in the dryer log against about 0.01% in the run7 log. Cause unknown. Two lost frames in a row is a 3.17 s gap.
9. **Bridge `/api/status` mode is stale.** It repeatedly reported "Recharge" when the controller was not in recharge. Never use it as controller state. Cause not investigated.
10. **Serial log cosmetic bug.** The `Vent pressure raw ...` line ends in a literal backslash-n (`\\n` in the format string, `statemachine.cpp` line 908), so the next log line is glued to it. Saved analysis scripts may depend on that, so check before fixing.
11. **Stale or wrong text** listed in section 4: "Chamber retry, 6s max" status text, the 15 s comment, XIAO mentions.
12. **The 1 h heat-up backstop is not the user's number.** The user has not decided whether to keep it.
13. **Auto-recharge from a saturated dryer** (checkbox on the Auto Cycle screen and the resume path) has never been run.
14. **Valve coil current check** is disabled in firmware because the shunt is not wired. Packets still carry the shunt millivolts (about 250-280 mV was seen with relays off, which is not a real current).

### 5.3 Built or changed but not verified on hardware
- **Auto dryer sample fix** (source and `.pio` image, not flashed): cycle starts in `SAMPLE_RUN`, 10 s pump, 10 s settle, then read. `DRYING` ignores readings for its first 10 s. `QUICK_SAMPLE` pumps first. Check the first cycle after flashing: `Auto phase 4`, about 10 s of `Pump ON`, then a settle, then a decision.
- **Recharge cool down to 120 F** with the Celsius fix and the 2 h cap: has never run on the heater.
- **Heat-up backstop (`HEATUP_TIMEOUT`)**: never triggered.
- **Safety check after a real cool down:** never run on a cooled bed. Run7 failed it at 43% To-Engine, but the sample was taken hot, so that failure may not be real.
- **Thermal trip at 255 F and the probe trip at 250 F:** I found no record of either firing on hardware.
- **Message stays until OK:** compiled and flashed, never seen.
- **Whether the beads dry to 15% at all.** About 6 h at temperature reached 43% with about 4 lb. The user has discussed halving the desiccant to about 2 lb; not done.

### 5.4 v1 cleanup and refactoring roadmap (prioritized)
Items marked **[user]** need a decision from the user first. Items marked **[hardware]** need the bench.

**P0: safety and trust**
- [ ] **[user, hardware]** Look at the Waveshare and find out why COM4 disappeared. Do not flash until the user says the dryer test is over.
- [ ] **[user]** Decide whether the vent pressure check should fail closed when pressure data is missing (5.2 item 3).
- [ ] **[user]** Decide how remote commands should be protected: pairing or a shared key on the ESP-NOW link, and a confirm step for remote Recharge (5.2 item 4).
- [ ] Fix the sensor-loss cool down loop (5.2 item 2): enter the failure cool down once, not on every pass, and decide what the pump does while sensors are missing.
- [ ] **[user]** Decide whether to add a hardware or software watchdog (5.2 item 5).
- [ ] **[hardware]** Run one full recharge to completion on the final build. Watch the first real cool down, the 120 F exit, the safety samples, and whether the stop message stays on screen.

**P1: finish v1 behavior**
- [ ] When the user says dryer run 2 is over: stop the capture processes, flash the sample-fix build to COM4 (pinned port), read it back, start a new capture before the dryer restarts, then check the first cycle (5.3).
- [ ] **Single stop reason with log-once and on-screen button hints.** The user asked for this earlier. It is not done. Today several paths can print the same stop repeatedly.
- [ ] Sensor node: send each reading twice or faster to cut radio dropouts (the node sends one unacknowledged broadcast per second).
- [ ] Review the auto-recharge resume path before anyone enables the checkbox.
- [ ] **Commit the work.** Everything since `accffcb` is uncommitted. Suggest a tag after the next verified run (the existing tag is `bench-2026-11-18`).
- [ ] Record the first system test result in `docs/PROJECT_NOTES.md` once a run actually completes.
- [ ] **[user]** Keep or remove the 1 h heat-up backstop. Consider halving the desiccant (4 lb to about 2 lb).
- [ ] Confirm what firmware belongs on COM6 (the radio bridge). I flashed `nodes/heltec-radio` there. Whether it previously ran MeshCore is unconfirmed.

**P2: cleanup (no behavior change, verify with a diff of the serial log)**
- [ ] Delete the duplicate `#define VALVE_TREND_CHECK_ENABLED 0` (`config.h` lines 117 and 132).
- [ ] Remove the retired humidity-trend block and its state (`evaluateValveVerification()`, `_valveBaselineRH`, `_valvePulseCount`, `_valveHighCount`). The function now always returns true.
- [ ] Remove unreachable code: the `_ventBadPulses >= VENT_PRESSURE_SAMPLES` branch (a single bad pulse stops the cycle first) and the unused local `step` in `evaluateVentPressure()`.
- [ ] Remove defines nothing reads: `VENT_PRESSURE_IDLE_BASELINE`, `RECHARGE_FAILED_FAN_MAX_MS`, `LOG_FILENAME` (a grep of `src/` and `include/` finds only the define itself; the disabled logger does not use it). `RECHARGE_HARD_OVERTEMP_F` is documentation only; keep it or move it to a comment. Remove `RECHARGE_TEST_MODE`, whose two branches are identical.
- [ ] Remove members and methods with no readers: `_lastPurgeMs` (written, never read), `timeRemainingMs()`, `RelayDriver::printDiagnostics()` and `setTestOutputs()` (unless bench diagnostics are wanted).
- [ ] Fix stale comments and text listed in section 4 (status text "6s max", the 15 s comment, XIAO mentions in `sensors.cpp`, `sensors.h`, `README.md`).
- [ ] Replace hard-coded colors with `UI_BG_COLOR` and friends (`ui_logo.cpp` uses `0x0A1628`, `ui_dryer_setup.cpp` uses `0x1A2E4A`).
- [ ] Pin `platform = espressif32@6.11.0` in all envs so a fresh checkout builds the same way. Add a `requirements.txt` for `pyserial`.
- [ ] Make `tools/watch_pressure.py` and `tools/make_logo.py` take their ports and paths as arguments. Decide whether `peek_port.py` and `reset_and_peek.py` get committed.
- [ ] Decide the fate of `nodes/xiao-sensor`, `nodes/heltec-sensor-test` and `firmware/xiao-sensor-known-good.bin` (retired or bench-only).
- [ ] Merge the dated notes sections into current-state documentation and mark the old ones superseded, starting with the stale table in section 4.
- [ ] `statemachine.cpp` is about 1,240 lines. Splitting recharge, auto dryer and thermal logic into separate files is an option, but only after the behavior above is verified. Do not refactor and change behavior in the same commit.

**P3: later**
- Display flicker (5.2 item 7), the radio drop cause (item 8), the bridge stale mode (item 9).
- Custom two-board carrier, K9 heater disconnect and the Board A UART link (design text only in the notes; nothing built).
- SD or other history logging. The web history page expects `/hangar.csv`, which nothing serves today.
- Direct Wi-Fi or MQTT control. The notes warn that ESP-NOW and Wi-Fi share one radio, so the channel must match.

---

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
- **The vent baseline uses pump-OFF samples only**, averaged and re-learned each recharge. Do not take the baseline while the pump runs.
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
- Windows PowerShell paths. `pio.exe` lives under `%USERPROFILE%\.platformio\penv\Scripts`.
- `docs/PROJECT_NOTES.md` is about 80 KB. Read it by section (`Select-String -Pattern '^## '`), not whole.
- `src/logo_img.c` is 4.7 MB of generated data. Do not open it in an editor tool or search it.
- PowerShell can show UTF-8 text as garbled characters. Check the bytes before concluding a file is corrupt. Only `PROJECT_NOTES.md` has a real damaged sequence left.
- The real Wi-Fi channel, hangar temperature and wiring are not visible from the repo. Ask the user.
