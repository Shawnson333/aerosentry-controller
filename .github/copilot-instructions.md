# AeroSentry Controller: instructions for Copilot

Embedded C++ (Arduino framework on PlatformIO) for a hangar engine dehumidifier. It switches a mains heater, a pump and two valves. A wrong change can overheat the heater or push hot wet air into an engine. Read `HANDOFF.md` before any non-trivial edit. Where a doc and the code disagree, the code wins.

## Stop and ask first
- Never flash a board, start or stop a bench test, or run an upload command unless the user says so. The v1 beta build has not run on the hardware yet; `docs/BENCH_TEST_CHECKLIST.md` is the order to prove it.
- Do not change a safety value, thermal limit, relay mapping, pin or packet layout without the user's decision. Say plainly what you could not verify. Do not present a guess as fact.
- User preference: capture logs, do not poll or monitor them.

## Boards
- **Controller (root project):** Waveshare ESP32-S3 4.3-inch Type B, 800x480, env `waveshare-esp32-s3-touch-lcd-43`. On COM4 (MAC 94:a9:90:df:bb:54).
- **Sensor node:** Heltec V4.3, `nodes/heltec-sensor`, env `heltec-v4-sensor`. Sends each packet twice.
- **Radio bridge:** Heltec V4.3 on COM6, `nodes/heltec-radio`, env `heltec-v4-beacon`, MAC 10:BD:A3:5A:7C:94 (`REMOTE_BRIDGE_MAC`).
- `archive/xiao-sensor` is retired. COM numbers can change, so confirm the board before flashing.

## Pins (verified against the code)
- Controller I2C: SDA 8, SCL 9 (100 kHz). Devices: XL9535 relays 0x21, DS3231M RTC 0x68, GT911 touch 0x5D (or 0x14), CH422G on 0x24 and 0x38.
- Controller GPIO 4 = GT911 INT, held low through touch reset. The LCD uses 0-3, 5, 7, 10, 14, 17, 18, 21, 38-42, 45-48. GPIO 19/20 are native USB. Never reuse them.
- Relays, XL9535 port 0, active-high: bit1 K2 pump, bit2 K3 heater, bit3 K4 case fan, bit4 K5 To-Engine valve (dump to atmosphere), bit5 K6 From-Engine valve (fresh intake). **K1 (bit 0) is reported dead on three boards, cause unknown: no cycle drives it; Admin > Relay test is the bench check.** K7 and K8 are 120 V AC, unused. Both valves are switched together.
- Sensor node: To-Engine SHT40 on 17/18, From-Engine SHT40 on 15/16, chamber NTC on 4, valve shunt on 3, HX711 pressure DOUT 48 and SCK 47. GPIO 3, 47 and 48 mean different things on the two boards.
- HX711 and the RTC run on 3.3 V only.

## Build and test commands (Windows PowerShell, repo root)
```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
$py  = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
& $pio run                                      # controller build (safe to run)
& $pio run -d nodes/heltec-sensor               # sensor node build
& $pio run -d nodes/heltec-radio                # radio bridge build
& $py tools/host_test.py                        # host simulation of the controller logic (safe to run)
& $pio run --target upload --upload-port COM4   # flash. ONLY when told. Always pin the port.
```
- Run `tools/host_test.py` after any change to `statemachine.cpp`, `sensors.cpp`, `relay.cpp` or `radio_link.cpp`, and add a scenario in `test/host/test_controller.cpp` for new behavior. It checks the safety invariants at every simulated step.
- The platform is pinned to `espressif32@6.11.0` (Arduino-ESP32 2.0.17, ESP-IDF 4.4).
- Stop any `capture_serial.py` / `watch_pressure.py` process before flashing: it holds the port.

## Safety rules that must not weaken
- Software thermal trip: chamber 255 F (`THERMAL_CHAMBER_F`), humidity probes 250 F. The 266 F mechanical fuse is hardware only. Do not move it into firmware.
- The heater is never newly turned on without a fresh (3 s) chamber reading. It is off in every cool down and safety phase.
- Vent pressure check fails closed: no recharge start and no recharge pump pulse (recharge, cool down or safety) without a fresh pressure reading. Each pulse must reach baseline + 20,000 counts or the cycle stops (S09). The baseline averages pump-OFF samples only. Track the peak on every new pressure packet while the pump runs.
- Sensor loss: 30 s grace (`SENSOR_LOSS_GRACE_MS`). Dryers stop; recharge stops the pump at once and goes to the failure cool down once, which advances on time even with no sensors. Readiness = a valid sample within 3 s. Do not bring back a per-sensor 3 s stop or the old holdout code.
- Recharge: 245 F target (off at 245, on at 240), 6 h counted from first reaching target, 1 h heat-up backstop, cool down with valves open until 120 F or 2 h, then two safety samples at or below 15%.
- Recharge ignores the 40% saturation stop. Purge, manual and auto keep it.
- Remote: no remote Recharge, starts only from the bridge MAC, remote dryer starts never arm Auto Recharge, remote commands never change settings unless a start is accepted. STOP from any sender.
- Loop watchdog stays on (`LOOP_WATCHDOG_ENABLED`). `setup()` turns the relays off before anything else.
- STOP must always work. Every stop path ends in `relay.allOff()`. Only `applyOutputs()` and the stop paths write relays (and the idle-only Admin relay test). Mode code sets intent flags.
- Every stop has a unique code (`StopReason`, S01-S18, `docs/ERROR_CODES.md`). Never renumber; a new cause takes the next number and gets a message.

## Conventions
- All pins, addresses, thresholds and timings are `#define`s in `include/config.h` (node pins in `nodes/*/include/config.h`) with unit suffixes (`_MS`, `_F`, `_C`, `_MV`). Do not hard-code new numbers in `src/statemachine.cpp`, including in message text (format them from the defines).
- `SensorReading` temperatures are Celsius. Names ending `_F` are Fahrenheit. Always convert with `celsiusToFahrenheit()`. (Comparing Celsius to 120 once ended every cool down instantly.)
- Naming: classes PascalCase, functions camelCase, members `_underscore`, macros UPPER_SNAKE, UI functions `ui*`, LVGL callbacks `cb*`. Colors use `UI_*_COLOR`.
- Single-threaded loop. No new FreeRTOS tasks, no blocking waits. Use `millis()` deltas. Only the ESP-NOW callback runs on another thread, and it copies data under a critical section.
- Never write NVS from a periodic path: a flash write blocks the PSRAM bus and can glitch the display.
- UI logic never touches LVGL, and the UI reads `stateMachine` getters. One screen per `src/ui_*.cpp`, declared in `include/ui.h`.
- Serial log lines are parsed by analysis scripts (`Auto phase`, `Recharge phase`, `Vent pressure raw`, `Sensor dropout - holding`, `Sensors back after`, `Stop:`, `ESP-NOW seq`). Do not reword them casually.
- Keep CRLF line endings. `docs/PROJECT_NOTES.md` is UTF-8 without BOM. After a behavior change, add a dated section to it that says what changed and what was not verified.
- `common/*.h` packet structs are shared by the controller, the sensor node and the radio bridge. Change them only with all three rebuilt together. `AEROSENTRY_PACKET_VERSION` is 3.

## Do not touch, tried and rejected
- `displayWritePixels()` in `src/display.cpp`: restored byte-for-byte to `bench-2026-11-18`. The flashes are PSRAM starvation (byte-swapped frames), not this function. The lever is `DISPLAY_PCLK_HZ`.
- Do not call `SD.begin()`. The SD logger is disabled on purpose (no TF slot on those pins, and it stalled LVGL).
- `VALVE_CURRENT_CHECK_ENABLED` stays 0 until the shunt is wired. The humidity-trend valve check was removed; do not bring it back.
- Recharge pulses longer than 10 s cooled the beads (To-Engine air fell from about 150 F to about 130 F). `RECHARGE_LONG_PULSE_ON_MS` stays 10 s unless the user decides otherwise.
- Auto dryer sampling is 10 s pump, 10 s settle, then read. Reading at pump start returns old chamber air. Waiting 30 s with the pump off reads falsely low.
- Stay on ESP-NOW channel 1. Do not add Wi-Fi joins. Do not flash the stock MeshCore ESP-NOW bridge. Do not buy the USS-PSV00033 valve again.
- `src/logo_img.c` is generated (4.7 MB). Do not open or hand-edit it. Use `tools/make_logo.py --input <image>`.
- Do not refactor and change behavior in the same commit. The roadmap is in `HANDOFF.md` section 5.4.
