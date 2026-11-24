# AeroSentry Controller

Hangar-based aircraft-engine dehumidifier controller. Firmware version `1.0.0-beta.1` (`FIRMWARE_VERSION` in
`include/config.h`).

Start here: `HANDOFF.md` (full context), `docs/BENCH_TEST_CHECKLIST.md` (what still has to be proven on the
hardware), `docs/ERROR_CODES.md` (stop codes S01-S18), `docs/PROJECT_NOTES.md` (dated engineering log).

## Hardware

- **Controller:** Waveshare ESP32-S3 4.3-inch Touch LCD **Type B**, 800x480, ST7262 RGB panel, GT911 touch,
  LVGL 8.3.11. The root PlatformIO project.
- **I2C (GPIO 8 SDA / 9 SCL, 100 kHz):** XL9535 relays 0x21, DS3231M RTC 0x68 (3.3 V only), GT911 0x5D,
  CH422G 0x24/0x38. Boot prints every address that answers.
- **Sensor node:** no-display Heltec WiFi LoRa 32 V4.3 (`nodes/heltec-sensor`). To-Engine SHT40 on 17/18,
  From-Engine SHT40 on 15/16, chamber 100K NTC on 4, valve shunt on 3, HX711 vent pressure DOUT 48 / SCK 47
  (3.3 V only). Sends packet version 3 on ESP-NOW channel 1 about once a second, each packet twice.
- **Radio bridge:** a second Heltec V4.3 (`nodes/heltec-radio`) on USB to the PC running
  `tools/remote_bridge.py`.
- **Relays (XL9535 port 0, active-high):** K2 pump, K3 heater mat (120 V AC), K4 case fan, K5 To-Engine valve
  (energized = dump to atmosphere), K6 From-Engine valve (energized = fresh-air intake). K1 (12 V aux) and
  K7/K8 (120 V AC aux) are not driven. **K1 has been reported dead on three boards; see Admin > Relay test.**
- Mechanical thermal fuse 266 F on the heater: hardware only.

## Cycles

- **Purge / Manual dryer:** pump, To-Engine 40% saturation stop.
- **Auto dryer:** samples (10 s pump, 10 s settle, read From-Engine), dries to the target RH, samples hourly.
  With Auto Recharge on (set at the screen, with a warning) a saturated dryer runs a recharge by itself and
  then resumes.
- **Recharge:** both valves vent outside; heater ramps to 245 F and holds 240-245 F; 10 s pump pulses every
  minute, each confirmed by the vent pressure step; three To-Engine samples at or below 15% end it; 6 h limit
  from first reaching 245 F; then cool down (fan, valves open, confirmed pulses) to 120 F or 2 h, and two safety
  samples at or below 15%.

## Safety summary

- Software thermal trip: chamber 255 F, humidity probes 250 F. Starts are refused above them.
- Heater is never newly enabled without a fresh chamber reading.
- Vent pressure check **fails closed**: no recharge and no pump pulse without a fresh pressure reading; one
  pulse without the 20,000-count step stops the cycle (S09).
- Sensor loss: 30 s grace, then stop (dryers) or a failure cool down that advances on time (recharge).
- Loop watchdog (5 s). Every relay is turned off first thing at boot, and a restart mid-cycle is reported (S18).
- Remote: Recharge cannot be started remotely, remote dryer starts never arm Auto Recharge, starts are accepted
  only from the bridge MAC, Stop is accepted from anyone.
- Every stop has a code, a cause, an action, and one serial line: `Stop: S09 VALVE_SUSPECT mode=Recharge | ...`.

## Build, test, flash (Windows PowerShell, repo root)

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
$py  = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
& $pio run                                   # controller
& $pio run -d nodes/heltec-sensor            # sensor node
& $pio run -d nodes/heltec-radio             # radio bridge
& $py tools/host_test.py                     # host simulation of the controller logic (no board needed)
& $pio run --target upload --upload-port COM4    # flash the Waveshare: only when told; always pin the port
& $py tools/capture_serial.py --port COM4 --seconds 20 --reset
```

The platform is pinned to `espressif32@6.11.0` (Arduino-ESP32 2.0.17) in every environment. The host test
needs PlatformIO's MinGW compiler once: `pio pkg install -g --tool "platformio/toolchain-gccmingw32"`.
Python tools need `pip install -r requirements.txt`.

## Layout

- `src/`, `include/`: controller. All pins, thresholds and timings are in `include/config.h`.
- `common/`: packet layouts shared by the controller and both nodes (change only with all three rebuilt).
- `nodes/heltec-sensor`, `nodes/heltec-radio`, `nodes/heltec-sensor-test` (serial-only bench test).
- `test/host/`: stubs, fakes and scenarios for `tools/host_test.py`.
- `tools/`: serial capture, remote bridge, logo generator, host test runner.
- `web/`: phone control page and the history page mockup.
- `archive/`: the retired XIAO ESP32-C3 sensor node.
