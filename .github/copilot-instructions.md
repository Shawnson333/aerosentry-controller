# AeroSentry Controller: instructions for Copilot

Embedded C++ (Arduino framework on PlatformIO) for a hangar engine dehumidifier. It switches a mains heater, a pump and two valves. A wrong change can overheat the heater or push hot wet air into an engine. Read `HANDOFF.md` before any non-trivial edit. Where a doc and the code disagree, the code wins.

## Stop and ask first
- Never flash a board, start or stop a bench test, or run an upload command unless the user says so. The Waveshare may currently be off USB (see `HANDOFF.md`, READ THIS FIRST).
- Do not change a safety value, thermal limit, relay mapping, pin or packet layout without the user's decision. Say plainly what you could not verify. Do not present a guess as fact.
- User preference: capture logs, do not poll or monitor them.

## Boards
- **Controller (root project):** Waveshare ESP32-S3 4.3-inch Type B, 800x480, env `waveshare-esp32-s3-touch-lcd-43`. On COM4 (MAC 94:a9:90:df:bb:54).
- **Sensor node:** Heltec V4.3, `nodes/heltec-sensor`, env `heltec-v4-sensor`.
- **Radio bridge:** Heltec V4.3 on COM6, `nodes/heltec-radio`, env `heltec-v4-beacon`.
- `nodes/xiao-sensor` is retired. COM numbers can change, so confirm the board before flashing.

## Pins (verified against the code)
- Controller I2C: SDA 8, SCL 9 (100 kHz). Devices: XL9535 relays 0x21, DS3231M RTC 0x68, GT911 touch 0x5D (or 0x14), CH422G on 0x24 and 0x38.
- Controller GPIO 4 = GT911 INT, held low through touch reset. The LCD uses 0-3, 5, 7, 10, 14, 17, 18, 21, 38-42, 45-48. GPIO 19/20 are native USB. Never reuse them.
- Relays, XL9535 port 0, active-high: bit1 K2 pump, bit2 K3 heater, bit3 K4 case fan, bit4 K5 To-Engine valve (dump to atmosphere), bit5 K6 From-Engine valve (fresh intake). **K1 (bit 0) is faulty on the bench board: never energize it.** K7 and K8 are 120 V AC, unused. Both valves are switched together.
- Sensor node: To-Engine SHT40 on 17/18, From-Engine SHT40 on 15/16, chamber NTC on 4, valve shunt on 3, HX711 pressure DOUT 48 and SCK 47. GPIO 3, 47 and 48 mean different things on the two boards.
- HX711 and the RTC run on 3.3 V only.

## Build commands (Windows PowerShell, repo root)
```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
& $pio run                                      # controller build (safe to run)
& $pio run -d nodes/heltec-sensor               # sensor node build
& $pio run -d nodes/heltec-radio                # radio bridge build
& $pio run --target upload --upload-port COM4   # flash. ONLY when told. Always pin the port.
```
- Stop any `capture_serial.py` process before flashing: it holds the port.
- Build flags are in `platformio.ini`: `-std=gnu++17`, USB CDC on boot, PSRAM, `-Iinclude -Icommon`. There are no unit tests.

## Safety rules that must not weaken
- Software thermal trip: chamber 255 F (`THERMAL_CHAMBER_F`), humidity probes 250 F. The 266 F mechanical fuse is hardware only. Do not move it into firmware.
- The heater is never newly turned on without a fresh (3 s) chamber reading. During a sensor dropout it is off unless the reading is fresh.
- Each recharge pump pulse must reach vent baseline + 20,000 counts or the cycle stops (`VALVE_SUSPECT`). The baseline is an average of pump-OFF samples only. Track the peak every loop while the pump runs.
- Sensor loss: 30 s grace (`SENSOR_LOSS_GRACE_MS`), then stop. Do not bring back a per-sensor 3 s stop or the old holdout code.
- Recharge: 245 F target (off at 245, on at 240), 6 h counted from first reaching target, 1 h heat-up backstop, cool down with valves open until 120 F or 2 h, then two safety samples at or below 15%.
- Recharge ignores the 40% saturation stop. Purge, manual and auto keep it.
- STOP must always work. Every stop path ends in `relay.allOff()`. Only `applyOutputs()` and the stop paths write relays. Mode code sets intent flags.

## Conventions
- All pins, addresses, thresholds and timings are `#define`s in `include/config.h` (node pins in `nodes/*/include/config.h`) with unit suffixes (`_MS`, `_F`, `_C`, `_MV`). Do not hard-code new numbers in `src/statemachine.cpp`.
- `SensorReading` temperatures are Celsius. Names ending `_F` are Fahrenheit. Always convert with `celsiusToFahrenheit()`. (Comparing Celsius to 120 once ended every cool down instantly.)
- Naming: classes PascalCase, functions camelCase, members `_underscore`, macros UPPER_SNAKE, UI functions `ui*`, LVGL callbacks `cb*`.
- Single-threaded loop. No new FreeRTOS tasks, no blocking waits. Use `millis()` deltas. Only the ESP-NOW callback runs on another thread, and it copies data under a critical section.
- UI logic never touches LVGL, and the UI reads `stateMachine` getters. One screen per `src/ui_*.cpp`, declared in `include/ui.h`.
- Serial log lines are parsed by analysis scripts (`Auto phase`, `Recharge phase`, `Vent pressure raw`, `Sensor dropout - holding`, `Sensors back after`, `Stop:`). Do not reword them casually.
- Keep CRLF line endings. `docs/PROJECT_NOTES.md` is UTF-8 without BOM. After a behavior change, add a dated section to it that says what changed and what was not verified.
- `common/*.h` packet structs are shared by the controller, the sensor node and the radio bridge. Change them only with all three rebuilt together. `AEROSENTRY_PACKET_VERSION` is 3.

## Do not touch, tried and rejected
- `displayWritePixels()` in `src/display.cpp`: restored byte-for-byte to `bench-2026-11-18`. Do not edit it to chase the flicker.
- Do not call `SD.begin()`. The SD logger is disabled on purpose (no TF slot on those pins, and it stalled LVGL).
- The humidity-trend valve check and `VALVE_CURRENT_CHECK_ENABLED` stay compiled out (shunt not wired).
- Recharge pulses longer than 10 s cooled the beads (To-Engine air fell from about 150 F to about 130 F). `RECHARGE_LONG_PULSE_ON_MS` stays 10 s unless the user decides otherwise.
- Auto dryer sampling is 10 s pump, 10 s settle, then read. Reading at pump start returns old chamber air. Waiting 30 s with the pump off reads falsely low.
- Stay on ESP-NOW channel 1. Do not add Wi-Fi joins. Do not flash the stock MeshCore ESP-NOW bridge. Do not buy the USS-PSV00033 valve again.
- `src/logo_img.c` is generated (4.7 MB). Do not open or hand-edit it. Use `tools/make_logo.py`.
- Do not refactor and change behavior in the same commit. The cleanup list is in `HANDOFF.md` section 5.4.
