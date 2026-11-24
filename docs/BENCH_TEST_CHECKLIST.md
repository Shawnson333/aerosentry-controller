# Bench test checklist (v1.0.0-beta.1)

Everything below is built and passes the host simulation (`python tools/host_test.py`, 27 scenarios,
124 checks), but **none of it has run on the hardware yet**. Work top to bottom. Record each result (date,
pass/fail, log file) in the Result column or in `docs/PROJECT_NOTES.md`.

House rules: capture logs, do not poll them; pin the upload port; nothing is flashed until the user says so.

## 0. Before flashing

| # | Step | Result |
|---|---|---|
| 0.1 | Stop any `capture_serial.py` / `watch_pressure.py` holding COM4. | |
| 0.2 | Exact rollback: read the app image off the board before flashing (command below). The board runs the 30 s-grace build, which was never committed on its own; the closest commit is `9a518b9` (it adds the auto dryer sample fix). | |
| 0.3 | Flash the **controller** and the **sensor node** (both changed). The radio bridge firmware did not change and does not need a flash. | |

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
$py  = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
& $py "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py" --chip esp32s3 --port COM4 read_flash 0x10000 0x330000 backup-grace30-app.bin   # rollback image
& $pio run --target upload --upload-port COM4                       # Waveshare (confirm COM4 first)
& $py "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py" --chip esp32s3 --port COM4 verify_flash 0x10000 .pio\build\waveshare-esp32-s3-touch-lcd-43\firmware.bin
& $pio run -d nodes/heltec-sensor --target upload --upload-port COMx  # sensor node; confirm the port by its boot line
& $py tools/capture_serial.py --port COM4 --seconds 60 --reset
```

## 1. Boot

| # | Check | Pass when | Result |
|---|---|---|---|
| 1.1 | Boot banner | `Firmware 1.0.0-beta.1`, `Reset cause ...`, `Loop watchdog on (5 s)`, `Relay driver OK` | |
| 1.2 | I2C scan line | `I2C devices:` lists 0x21 (relays), 0x5D (touch), 0x68 (RTC) and the CH422G (0x24/0x38, maybe more). **Record the exact list.** Anything else on the bus is news. | |
| 1.3 | Sensor node | Its log shows `sent 2` on every line; controller logs each `ESP-NOW seq` once | |
| 1.4 | Admin page (tap title 10 times) | Shows `Firmware 1.0.0-beta.1`, `Loop max` in ms, `Last stops none since power-up` | |

## 2. Display flashes (pixel clock now 14 MHz)

| # | Check | Pass when | Result |
|---|---|---|---|
| 2.1 | Picture is stable, no visible flicker at the lower refresh (34 Hz) | No shimmer, colors normal | |
| 2.2 | Switch pages 50 times (Main / Dryer / Auto / Clock / Admin / logo) and count magenta or gold flashes | Before: about 1 in 10. Goal: none. **Record the count.** | |
| 2.3 | If flashes remain: set `DISPLAY_PCLK_HZ` to 12000000 in `include/config.h`, rebuild, repeat 2.1-2.2 | | |

Why: the gold and magenta colors are the navy and panel-blue backgrounds byte-swapped, a one-byte slip in the
pixel stream when the LCD DMA is starved of PSRAM bandwidth. See the comment at `DISPLAY_PCLK_HZ`.

## 3. Relay K1 investigation (Admin > Relay test)

Idle only. K3 is the mains heater and K7/K8 are mains outputs: a 2 s pulse is a bench check.

| # | Check | Record | Result |
|---|---|---|---|
| 3.1 | Tap P0.1 (K2 pump) | Relay clicks, LED lights, screen shows output P0 0x02, input P0 0x02 (control case) | |
| 3.2 | Tap **P0.0 (K1)** | Click? LED? Output P0 = 0x01? **Input P0 bit 0 = 1 or 0?** | |
| 3.3 | Tap P1.0 to P1.7 one at a time | Does any of them click K1? | |
| 3.4 | Interpretation | Output 1, input 1, no click: board-side fault or K1 wired to another pin. Output 1, input 0: something on the board holds P0.0 low. A P1 pin clicks K1: K1 is mapped there; update `RELAY_BIT_*`. | |

## 4. Watchdog (Admin > Relay test > Watchdog test)

| # | Check | Pass when | Result |
|---|---|---|---|
| 4.1 | Start a **Purge** (pump on, no heat). Open Admin > Relay test > Watchdog test > Freeze now. | Within about 5 s the board resets; the pump relay drops within about 1-2 s of the reset; boot screen shows **S18** with "watchdog" and "Purge ended". Serial: `Watchdog test: loop() frozen on purpose`, then a panic dump, then the boot banner. | |
| 4.2 | Idle, then after a day of normal use, read `Loop max` on Admin | Well under 5000 ms (expect well under 1000). A `Slow loop pass:` line marks any pass over 250 ms. | |
| 4.3 | Pull the controller's power during a Purge, restore it | Boot screen shows **S18** "Power was lost" and "Purge ended" | |

## 5. Messages and prompts

| # | Check | Pass when | Result |
|---|---|---|---|
| 5.1 | Recharge button | New prompt: safe location / heats to 245 F / water from the discharge port, with **Start Recharge** and **Back** | |
| 5.2 | Tick Allow Auto Recharge on the Manual screen, open the Auto Cycle screen | Both checkboxes show the same state | |
| 5.3 | Start a dryer cycle with Auto Recharge on | Unattended-recharge warning with Start / Back. Serial: `Auto Recharge armed for this cycle` | |
| 5.4 | Any stop message | Ends with `<mode> ended. Code Sxx`; long text uses the smaller font and fits above OK | |
| 5.5 | Leave a stop message for over 3 minutes | The idle logo does not cover it (old fix, never seen working) | |
| 5.6 | Admin `Last stops` | Lists the stops just made, newest first | |

## 6. Remote control

| # | Check | Pass when | Result |
|---|---|---|---|
| 6.1 | Phone page | No Recharge button, no Auto Recharge checkbox; Auto Recharge shown read-only | |
| 6.2 | Stop `remote_bridge.py`, open the bridge's serial port (COM6) and type `CMD 1 RECHARGE 3 1 20 1` | ACK text `Recharge must be started at the unit...`; controller stays idle. (The page and `remote_bridge.py` no longer send Recharge at all.) | |
| 6.3 | Auto Recharge on at the unit, start Manual from the phone | ACK `Start accepted. Auto Recharge stays off for remote starts.` | |
| 6.4 | Stop from the phone during a cycle | Screen shows S02 "Stopped from the remote page" | |
| 6.5 | Unplug the radio bridge for 10 s while the page is open | Page says "No status from the controller for N s"; mode is not shown as current | |

## 7. Recharge safety (sensor faults on purpose)

| # | Check | Pass when | Result |
|---|---|---|---|
| 7.1 | Unplug the HX711 (pressure) before starting a Recharge | Start refused: "Recharge needs the vent pressure sensor" | |
| 7.2 | Unplug the HX711 during heating | After about 33 s: `Recharge failure VENT_SENSOR_LOST`, heater off, pump never runs, valves stay open, fan on; S08 after the cool down | |
| 7.3 | Disconnect the vent tube at the first pulse | `NO FLOW`, immediate **S09**, everything off (repeat of the 2026-11-20 test) | |
| 7.4 | Power off the sensor node during a pulse at temperature | Pump off within about 3 s (`Recharge pulse cut short`), then `Recharge failure SENSOR_NODE_SILENT` **once**, status line "Fault cool down", heater off, valves open, fan on; ends **S04** after 2 h (no chamber reading) | |
| 7.5 | Power off the sensor node for 10 s during a pulse | `Sensors back after ...`, no failure, the cut pulse is re-run | |
| 7.6 | Each recharge pulse, cool down pulse and safety pulse | `Vent pressure raw ... OK ... recharge pulse` / `cool down pulse` / `safety pulse` lines, steps well above 20,000 | |

## 8. Full runs (v1 exit criteria)

| # | Run | Pass when | Result |
|---|---|---|---|
| 8.1 | One complete Recharge, unattended | Ends **S16**. Cool down reaches 120 F (`Cool down done after N min: chamber at or below 120 F`), both safety samples at or below 15%, message stays until OK. This is also the first real run of the 120 F cool down and of judged cool down / safety pulses. | |
| 8.2 | Auto dryer, 12 h or more | First lines after start: `Auto phase 4`, 10 s pump, `Auto phase 5`, settle, then a decision. No stops. | |
| 8.3 | Radio loss with double send | Count missing sequence numbers in 8.2's log; compare with 0.4% in the 2026-11-23 log | |
| 8.4 | Auto Recharge from a saturated dryer | Only once 8.1 passes. Recharge starts by itself, completes, the dryer cycle resumes (`Recharge complete - resuming previous cycle`) | |

## 9. Known gaps (not verifiable yet, or need a plan first)

- Thermal trips (255 F chamber, 250 F probes) have never fired on hardware. Tested in the simulator only. A real
  test needs a written plan (for example, a resistor in place of the thermistor) before anyone tries it.
- Heat-up timeout (1 h) tested in the simulator only. The 1 h value is still a placeholder; user decision.
- Vent baseline is a running average over the whole recharge. In the 2026-11-22 log it drifted about 23,000
  counts in the first hour. Steps are 70,000-130,000, so there is margin, but watch `Vent baseline` against the
  raw idle reading late in 8.1. A drift down would shrink the measured step.
- The XL9535 keeps its outputs through an ESP32 reset (assumed; 4.1 shows how long the relays stay on).
- Valve coil current check stays disabled until the shunt is wired.
- Desiccant amount (4 lb vs about 2 lb) is still open.
