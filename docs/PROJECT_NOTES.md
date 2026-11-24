# AeroSentry Project Notes

Newest sections are at the end. The latest is "2026-11-24 v1 beta build". Several older sections are stale;
`HANDOFF.md` section 4 lists them, and where a note and the code disagree, the code wins.

## Audit restoration — 2026-11-20 (logger disabled + display restored)

Restored the garbled commit to the bench-tested state (`bench-2026-11-18` / `35cd9ad`), then kept only the final confirmed rules. The previous notes claimed both "pulsing the pump with ambient air" and "valves closed so no ambient air" at the same time; both cannot be true. The restored cooldown keeps the valves OPEN (ambient air) and the pump ON, exactly as in the validated HEAD (`35cd9ad`/`accffcb`) behavior.

- **SD logger DISABLED** (user request): `src/logging.cpp` now returns false without calling `SD.begin()`, and `main.cpp` prints "SD logger disabled". The Waveshare Type B has no TF slot on the default pins — `f_mount (3)` is the real error — and the open/print/close stalled the LVGL flush thread. Build no longer links `<SD.h>` / `<SPI.h>`.
- **Recharge cooldown (restored)**: pump ON, case fan ON, valves OPEN; exits early when `chamberTemp <= 120 F`? — NO: the validated bench image has NO early 120 F exit. Recharge cooldown is `(phaseElapsed() >= RECHARGE_COOLDOWN_MS)` only, then the 2 safety pulses. The loose 10 s / 10 min pulse defines are NOT present in the bench tree — they were added only in the garbled diff and are now removed. Do not re-enable them without bench validation.
- **Failed recharge fan-hold**: the validated `rechargeFailureCooldown()` does NOT have the 120 F / 2 h fan-hold. That logic was part of the garbled rewrite and is now reverted. Failure keeps venting outside and cools for the full `RECHARGE_COOLDOWN_MS` before closing valves.
- **Display flicker**: `UI_BG_COLOR` (0x0A1628) unchanged. Restored `displayWritePixels()` byte-for-byte to bench-2026-11-18 (small-update vsync guard `<400 && <80`, large windows ungated). The magenta/yellow flashes were the partial `0x00` RAW bytes path in that good build — not a regression. If flicker persists, it is the panel, not the vsync flag.
- **Admin pressure tap (2026-11-20)**: `uiUpdateAdmin()` now shows `Vent press` (raw), `Vent avg` (running ambient average, pulse peak, step, OK/NO FLOW), and `Pulses good/bad`. If the step never reaches 20,000, the safety path is provably armed. Serial `capture_serial.py` on COM4 will print the same `Vent ambient sample` lines per 30 s and `Vent pressure raw ... peak ... step` per pulse.
- **Blocked test note (2026-11-20)**: the previous test reported the safety catch never tripped with the air line disconnected. That is consistent with `VENT_PRESSURE_SAMPLES=6` and `_ventGoodPulses==0` gating: a bad count of 1–5 without a fault will not stop the cycle. The fix is to judge the *first* pump pulse — see `evaluateVentPressure()`.
- **2026-11-20 safety fix**: `evaluateVentPressure()` now treats a single 10 s pulse with no `+20,000` over the ambient average as a safety shutdown (`stopWith(VALVE_SUSPECT)`). Any good pulse does NOT hide a later bad pulse — each pulse is judged independently. Watch `Vent pressure raw ... peak ... step ... NO FLOW` then `Vent pressure fault - pulse did not rise 20,000 over ambient` on COM4. Admin page now shows the same `Vent avg` / `Pulses good/bad`. Note: `_ventPulsePeak` is reset after judging each pulse, so the next 10 s window starts clean.



- **Reserved for full bench test**: validate the loose cooldown pulses against the 120 F heat-soak curve; do not run the 3-round 120 V test on the garbled binary.

## Where we are — 2026-11-18, bench test

Mostly successful all-up run. Sensors, radio, display, touch, clock, relays, heater cap, and the recharge state machine all worked. The valves did not. They are the wrong part. Replacement requirements are in the recharge valve section below. Do not tune `VENT_PRESSURE_MIN_STEP` from tonight's counts.

**Known-good firmware is git `35cd9ad`, "Add vent pressure check, two valves, and bench test mode."** Tag `bench-2026-11-18` points at that commit. Both boards are running that tree:

- Waveshare was already on `35cd9ad`. It was not reflashed. Boot line is `XL9535 outputs OFF (K1 unused)`. COM4 during the test.
- Sensor node was still on the older image at the start, so the controller ignored its packets. Flashed `nodes/heltec-sensor` to COM10 from `35cd9ad`. MAC `F8:5B:1B:BF:3F:EC`. Boot line is `Vent pressure DOUT 48 SCK 47 ready`.
- Later commits may enable the unused aux relay channels and update these notes. That does not change pump, heater, fan, or valve bits. Do not reflash the Waveshare just to pick that up. The image on the board is the tested one.

What passed, with AC on:

- Packet v3, flags `0x0F`. To-Engine about 27% RH at idle. From-Engine about 14% at idle, higher once recharge moved air. Chamber about 59 F at rest after the 10k was moved to ground. It had been on 3.3 V, which held GPIO 4 at 3119 mV and the firmware rejected it.
- HX711 on 3.3 V. Module `OUT` is GPIO 48, `SCK` is GPIO 47. Idle raw about 1,199,000. Quiet wander about 560 counts.
- XL9535 and DS3231M ACK. Display and touch came up. GT911 at 0x5D.
- Bench recharge cap is 120 F (`RECHARGE_TEST_MODE` 1). The heater reached it and cycled off. Pump stayed off until the pulse, then ran with both valve bits set (`0x3A` = pump + fan + K5 + K6).
- First recharge learned baseline 1,199,286. End-of-pulse steps were +5,901 and +5,434. A later pulse, after the valve plumbing was reworked, was +19,253. The firmware called them OK against the placeholder gate of 8. That gate is inside the noise. The steps themselves are choked-orifice pressure, not a divert.
- Valve-current check and the humidity ladder stayed compiled out. The `valveMv` print sat near 280 mV all night, including with the relays off. Ignore it.

What failed:

- USS-PSV00033 clicks and the relay LED lights, and purge (valves at rest) sends the air the right way. Energized, most of the flow still goes to the engine and the dump only gets a wisp. It is a 1/8 inch pneumatic pilot valve, not a diverter. See the replacement spec below.
- Early purge, manual, and auto starts were stopped immediately. The first relay wiring was wrong and was corrected before the recharge runs. Do not treat those short starts as a program failure.

Next: replacement valves, then one recharge with a real divert, then set the pressure step from that run. The pressure log from tonight is `%TEMP%\aerosentry-pressure-watch.log` on this PC. It is not in the repo.

## Where we are — 2026-11-11, end of session

Saved locally on `main` at `86a51b5` ("Serve the remote page on the Tailscale address."). Not pushed. `main` is ahead of `origin/main`. Working tree was clean before this note.

Phone path that works today, with the relay board still at home:

- Phone browser, Tailscale connected, opens `http://100.110.107.102:8080/`. History sample is `/history.html`. Do not open the HTML files directly.
- This computer runs `tools/remote_bridge.py`. It must stay awake. It talks USB to the Heltec, which forwards on ESP-NOW channel 1 to the Waveshare.
- Stop was acknowledged: "Already idle. Outputs off." Purge was refused: relay board not connected. Starts stay refused until the XL9535 is back.
- Live humidity can appear when the XIAO is in range. Charts on the history page are sample drawings, not hangar logs.
- Tailscale address can change. If the phone URL fails, run `tailscale ip -4` on this computer. Do not forward port 8080.

Next session, in this order. Do not redo the radio discovery.

1. Done 2026-11-13. The XL9535 and DS3231M are connected. A purge start was accepted, then Stop was accepted. The pump may have run for a moment. The missing-relay refusal is gone.
2. Next: the second V4 is already flashed with MeshCore companion. Test that radio hop before replacing the USB stand-in. Do not flash the stock MeshCore ESP-NOW bridge. Do not join the Heltec to Wi-Fi. Stay on channel 1.
3. Move `tools/remote_bridge.py` to a small always-on computer later. A Chromebox is fine. The phone still uses Tailscale, not a public port.

To resume without rereading the whole chat: read this section and the wiring list below. Do not rebuild or reflash until the new sensors are on the bench.

## Sensor move — bench confirmed

The no-display Heltec WiFi LoRa 32 V4.3 has replaced the XIAO on the bench. Firmware is `nodes/heltec-sensor`. It reads both SHT40 probes and the chamber thermistor, then sends the existing sensor packet on ESP-NOW channel 1. The Waveshare received the packets with all three validity flags set. The XIAO was disconnected. The OLED V4 boards stay the repeater and home node. The Waveshare still owns the relays.

New node MAC: `F8:5B:1B:BF:3F:EC`. Both SHT40 probes answer at `0x44` on their separate buses. Bench readings were about 47% / 45% RH and 21C chamber. Thermistor spread was about 3-16 mV. The 150 mV gate is a starting value, not a final heater-mat proof.

The stainless SHT40 probes use high-precision command `0xFD`. Their addresses cannot be changed, so the two buses stay separate. Runs are about 12 inches inside the same enclosure.

## Wiring — no-display Heltec V4.3

Power every sensor from a side-header 3V3 pin and a side-header GND pin. Do not use 5V. Do not use the Ve pins. Do not add pull-up resistors.

Probe wire key used for the working bench wiring: **red = 3V3, black = GND, yellow = SCL, green = SDA**. The supplied probe diagram labels green as SCL as well as yellow. That is a documentation typo. Green is the data line.

With the board component-side up and USB-C at the bottom, the four positions on the small top connector are, left to right: **GPIO 15, GPIO 16, GPIO 17, GPIO 18**. That connector has no power or ground pins.

To-Engine SHT40, top connector:

| Location | Heltec pin | Probe wire |
| --- | --- | --- |
| Top connector, third from left | GPIO 17 | Green, SDA |
| Top connector, fourth from left | GPIO 18 | Yellow, SCL |
| Side header J3, pin 2 or 3 | 3V3 | Red, VCC |
| Side header J3, pin 1 | GND | Black |

From-Engine SHT40, same top connector:

| Location | Heltec pin | Probe wire |
| --- | --- | --- |
| Top connector, first from left | GPIO 15 | Green, SDA |
| Top connector, second from left | GPIO 16 | Yellow, SCL |
| Side header J3, pin 2 or 3 | 3V3 | Red, VCC |
| Side header J3, pin 1 | GND | Black |

GPIO 26 and 33 are not used.

Chamber thermistor, side header J3:

| Location | Connection |
| --- | --- |
| J3 pin 2 or 3, 3V3 | One thermistor lead |
| J3 pin 15, GPIO 4 | Other thermistor lead, and one side of the 10k resistor |
| J3 pin 1, GND | Other side of the 10k resistor |

The thermistor and resistor are not polarized.

Do not use GPIO 2, 5, or 7. Those are the power amplifier. Do not use GPIO 8 through 14. Those are LoRa. Do not use GPIO 19 or 20. Those are USB. Do not use GPIO 36. That pin switches peripheral power. Do not use GPIO 1. That pin reads the battery.

Bench-confirmed and in the saved firmware:

- Waveshare ESP32-S3 4.3-inch Type B, 800x480. Touch, main controls, clock screen, and the navy AeroSentry splash work. The upload port used so far is COM4.
- XL9535 at 0x21, active-high, two data bytes (port 0 then port 1). K2 pump, K3 heater, and K4 case fan are the confirmed outputs. K5 is the To-Engine valve and K6 the From-Engine valve, both energized together during recharge. K1, K7 and K8 are aux outputs and are enabled in the driver mask, but no cycle drives them. The bench relay board has a faulty K1 channel, so leave bit 0 alone until the custom carrier board replaces it.
- XIAO ESP32-C3 on ESP-NOW channel 1, MAC `AC:27:6E:FC:EF:A8`. Known-good image: `firmware/xiao-sensor-known-good.bin`. To-Engine is D4/D5, From-Engine is D8/D9, both at `0x44`. The chamber 10k divider is installed and `CHAMBER_DIVIDER_INSTALLED` is 1. Spread reject is 150 mV. A dual-port capture showed packets about once a second with no missing sequence numbers.
- Cycle rules below are implemented: 40% To-Engine stop, 60-second purge qualification, purge reminder, 260F / 250F thermal stops, and admin (ten title taps). The recharge chamber retry window is in the same firmware, but it is not yet a finished heater-mat proof. See the next list.

Coded in that same commit, not yet written up as a finished bench proof:

- DS3231M driver at 0x68. It is read at startup, refreshed once a minute, and written from Set Clock and from Done. First installation still needs Set Clock, then Done, with the coin cell installed. Power-loss hold of the purge age has not been recorded as confirmed.
- Chamber retry: after a valid chamber sample, a brief invalid reading holds the previous heater command for up to 6 seconds and cannot newly enable heat. The real heater mat is not in this test yet. When it arrives, watch K3 switching EMI on the thermistor lead and whether a 6-second heat hold near 240F overshoots toward 260F.

Still deferred, on purpose:

- SD card logging. `src/logging.cpp` is a stub. The intended file is `/hangar.csv`.
- History web page. About 30 days, both humidities about once a minute. Charts stay off the operator screen.
- Unattended recharge valves. Do not assign K6-K8 until that sequence exists.
- Heltec WiFi LoRa 32 V4.3, GPS unplugged, no wires to the Waveshare. MAC `10:BD:A3:5A:7C:94`. Waveshare MAC `94:A9:90:DF:BB:54`. Firmware in `nodes/heltec-radio` forwards page commands and returns status. It cannot drive relays.

Do not reopen these while adding the radio: the XL9535 is not a PCF8574, K1 does not drive, the XIAO stays on ESP-NOW channel 1, and a remote command must not become a second state machine.

## Next: Heltec V4

The board in hand is a Heltec WiFi LoRa 32 **V4.3** with the 0.96-inch OLED. GPS has been unplugged. Listing facts that still apply: ESP32-S3R2, 2MB PSRAM, 16MB flash, SX1262, about 27 dBm. It is not wired and not in `platformio.ini`. Flash option for this revision is `USE_KCT8103L_PA`, not the V4.2 GC1109 option. MeshCore's OLED target is `heltec_v4_oled` in [meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore) `variants/heltec_v4/platformio.ini`. Do not flash Meshtastic. LoRa SPI there is SCLK 9, MOSI 10, MISO 11, NSS 8, RESET 12, BUSY 13, DIO1 14. On V4.3 the PA controls are GPIO 2 (CSD), GPIO 5 (CTX), and GPIO 7 (VFEM power). Leave those alone.

Role, already decided: the Heltec is the LoRa coprocessor. MeshCore carries start, stop, and mode actions, plus warnings, status, and periodic log packets, so the phone page can stay current away from the unit. It does not drive relays. Remote start and stop must call the existing `StateMachine::command()` path so the sensor, saturation, and thermal checks still apply. If the local link is down, ignore remote starts. The touch screen still stops the cycle.

Link, revised 2026-11-11: no extra module, and no wires from the Heltec to the Waveshare. Use ESP-NOW channel 1.

- An RS485 adapter is not required. It was only needed to translate the Waveshare RS485 socket. That socket stays unused. GPIO 43 and 44 already go to the onboard RS485 chip, so they are not a plain serial port either. There is no free serial header to land a 3-wire cable on.
- I2C needs no adapter, only SDA, SCL, and GND, and it is still the wrong bus. Those wires already run the touch controller and the relay chip. If the Heltec locks them, the screen and the relays stop together, including the button that turns the heater off. Do not put the Heltec there to avoid a small board.
- USB-C to USB-C still does not work. Both ports are programming ports.
- ESP-NOW does work. The earlier "do not copy the XIAO" note was too strong. The Heltec can be a second peer on channel 1. Its packet must not look like the XIAO sensor packet. A missed humidity reading is harmless. A missed stop is not, so a command is acknowledged, a stop is repeated until acknowledged, and a quiet link cannot start a cycle. The touch screen still stops the unit. The Heltec stays on channel 1 and does not join Wi-Fi. The phone path is the SX1262 LoRa radio, which is a separate radio and can run at the same time. Do not flash MeshCore's stock ESP-NOW bridge.

No Heltec pins are wired for this link. Power it, leave USB-C for flashing, and leave the GNSS connector empty.

The remote path is web page, then MeshCore over LoRa, then the Heltec, then ESP-NOW, then `StateMachine::command()` on the Waveshare. The page does not talk to the controller directly, and the Heltec does not energize a relay. A command is only a request. The controller still applies the sensor, saturation, and thermal checks. If the ESP-NOW link is quiet, remote starts are ignored. The touch screen can still stop the cycle.

The basic operator page is `web/index.html`, and the sample history drawing is `web/history.html`. Do not open those files directly. Run `tools/remote_bridge.py` and open `http://127.0.0.1:8080/`. That server is this computer standing in for the future home computer. It talks USB to the Heltec, and the Heltec talks ESP-NOW to the Waveshare. It is bound to this machine only. It is not on the internet.

A phone away from home opens that same page through Tailscale, not through an open public port. This computer's Tailscale address is `100.110.107.102`. The phone URL is `http://100.110.107.102:8080/`. Do not forward port 8080 on the router. The home computer will have its own V4 on USB. LoRa repeaters carry the mesh between that home V4 and the hangar V4. MeshCore firmware is not on either radio yet, so this bench uses the USB cable in place of that mesh hop.

On 2026-11-11 the page sent Stop and the controller answered "Already idle. Outputs off." Purge was refused because the relay board is not connected. Starts stay refused until that board is back. Humidity can still appear if the XIAO is in radio range.

OLED stays. It is the radio's own status display: link, last command accepted or rejected. It is not a second operator screen.

GPS is unplugged. Leave the GNSS pins unused: RX 38, TX 39, EN 34, RESET 42. A map pin later can be a typed-in hangar coordinate. Time already comes from the DS3231M.

## No Heltec wiring for the controller link

Nothing on the Heltec header connects to the Waveshare. The hear test is in `nodes/heltec-radio`. It broadcasts `LinkBeacon` once a second. The controller prints it and does not treat it as a sensor reading or a command.

Ports last seen: Waveshare COM4, Heltec COM6. Both are native USB, so those numbers can swap. Confirm by the boot line. The controller says `AeroSentry Controller Starting`. The Heltec says `AeroSentry Heltec V4.3 link`.

On that same USB-only session the XL9535 and DS3231M did not ACK. That was already true before the beacon firmware was flashed. The display, touch, and ESP-NOW listener still started, and no relay writes were made.

Leave these alone:

- Do not move the XIAO off channel 1.
- Do not host the remote page on the Heltec. Its Wi-Fi radio must stay on channel 1. The 30-day history page and SD logger are still deferred. The operator page shell is `web/index.html` and is not connected.

## Cycle rules

*Superseded. Current values: 245 F target, 255 F trip, 6 h from first reaching target, 30 s sensor-loss grace,
10 s sample settle, vent check fails closed. See `README.md` and `docs/ERROR_CODES.md`.*

These setpoints are best guesses and will be tuned after testing. All sensors are logged during every cycle. Only the sensor a mode uses for control can stop that mode.

- **Purge:** pump on until the user stops it. Watch To-Engine only. Above 40% for 30 continuous seconds while pumping stops the cycle: "Desiccant saturated. Recharge cycle recommended."
- **Manual dryer:** pump on for the selected minutes, off for the selected hours, then repeat. Same 40% To-Engine stop. From-Engine is logged but not required.
- **Auto dryer:** the only mode that controls from both sensors. Pump runs until From-Engine is at or below the user target. Then once an hour the pump runs 10 seconds, pauses 30 seconds, and samples. Over target starts drying again; under target waits another hour. The 40% To-Engine stop still applies.
- **Recharge:** 40% stop does not apply. Heaters run until the XIAO chamber thermistor reaches 240F, then the pump pulses 10 seconds on and 1 minute off. Three consecutive To-Engine humidity readings at or below 15% complete the cycle. Otherwise it stops at 3 hours or when the user stops it.
- A missing required sensor blocks Start and, if it drops mid-cycle, stops the cycle with a warning instead of silently returning to Idle. Recharge has one deliberate exception: after the chamber has been valid, a brief invalid sample starts a 6-second retry window measured from the last valid chamber sample. The heater holds its previous command during this window but cannot be newly enabled; normal control only allows it to be on below the target temperature. Recovery resumes regulation; exceeding the window stops Recharge and all outputs. The XIAO revalidates isolated one- or two-sample faults on the next good sample, while three consecutive faults require three good samples. A currently valid chamber reading is always required to start.
- Starting Dryer or Auto without a qualified purge in the previous hour asks the user to purge first. A purge counts only after the pump has run for 60 seconds. Purge opens the normal purge prompt. Continue to Dryer opens the normal dryer prompt. The qualified time is saved with the system clock. The DS3231M preserves that age across power loss; the software fallback does not count time spent powered off.
- Recharge and any other cycle stop if the chamber thermistor exceeds 260F, or if either SHT31 temperature exceeds 250F. Those SHT31 limits are a redundant catch because they should stay well below chamber temperature. A start is also blocked while a limit is exceeded.
- Tap the AeroSentry title ten times within a few seconds to open Admin. It shows both humidities, all three temperatures, the chip temperature, and the live state of K1-K8. The main-screen relay line can be hidden there. It stays visible by default for bench work.

## Confirmed during first Type B hardware test

- The display is **800x480**, not 480x272 as stated in the original handover.
- RGB initialization, PSRAM, GT911 touch, main-screen rendering, mode navigation, and Stop Cycle were observed working.
- The relay board is marked **XL9535** and answers at 0x21. The Google Drive samples supplied with it describe a different PCF8574 board with no USB-C or dual-power jumper. Do not use those samples or that schematic for this hardware.
- The user's board has dual power: 3.3V logic and a separate 5V relay supply. The matching XL9535 register protocol is two data bytes after the register: port 0 then port 1.

## Relay bench notes

The first XL9535-style sequence (`0x06, 0x00`, then `0x02, 0xFF`) energized K2-K8 and left K1 off. That was a real output-register write, not evidence for the PCF8574 protocol. Single-byte PCF8574 commands do not address an XL9535 output register and must not be used to clear it.

The current initialization follows the public XL9535 example at https://github.com/mcauser/micropython-xl9535-kxv5-relay:

1. Register 0x04 <= 0x00, 0x00: no polarity inversion on either port.
2. Register 0x02 <= 0x00, 0x00: both output ports off. This board is active-high.
3. Register 0x06 <= 0x00, 0x00: both ports are outputs.

K1 stays unused. K2 is the pump, K3 the heater, K4 the case fan, and K5 AUX stays off. K6-K8 stay reserved. The driver rejects K1 and K6-K8. K2 was confirmed on P0.1. Later bench work confirmed K3 heater and K4 case fan on the following bits. K5 AUX remains unused and was not given a separate click test.

K4 runs while the pump is on, for the entire recharge cycle, and also when the Waveshare chip temperature stays at or above 65C. It turns off at 55C if the pump is off and recharge is not running. That chip reading is the ESP32-S3 die, not the air in the case, so the thresholds are a starting guess.

During recharge the heater holds 240F. It turns off at 240F and back on at 235F so the relay does not chatter. The pump still waits for the first time the chamber reaches 240F, then pulses.

The XL9535 itself needs no firmware. The USB-C port's purpose is unconfirmed until it is connected or the exact board documentation is found. P1 and J3 may be parallel I2C connections, but that is not confirmed for this board.
- The saturation hard-stop applies outside Recharge only.
- Missing sensor data must produce a warning, not a brief mode entry followed by an unexplained return to Idle.

## Known-good controller

Recorded after the bench confirmed touch, live humidity, the 40% stop, and the cycle screens. The splash mark was enlarged and its near-black background was replaced with the UI navy `0x0A1628` so it does not look pasted on.

What is working at this point:

- Waveshare touch, main controls, clock, and the AeroSentry splash.
- XIAO on channel 1. To-Engine D4/D5 and From-Engine D8/D9, both at factory address `0x44`. Saved image: `firmware/xiao-sensor-known-good.bin`, MAC `AC:27:6E:FC:EF:A8`.
- K2 pump, K3 heater, K4 case fan. K1 unused. K5 AUX off. K6-K8 reserved.
- 40% To-Engine stop, purge reminder, 60-second purge qualification, and the 260F / 250F thermal stops.
- Admin is ten taps on the title. The relay line stays visible unless hidden there.

## Known-good XIAO firmware

Saved as `firmware/xiao-sensor-known-good.bin`. Flashed to the XIAO ESP32-C3 at MAC `AC:27:6E:FC:EF:A8` on 2026-04-09 and confirmed on the bench.

Why the wiring is split:

- The ESP32-C3 has one I2C controller, so both SHT31 boards cannot share a bus if they use the same address.
- To-Engine is D4 SDA / D5 SCL. From-Engine is D8 SDA / D9 SCL. Firmware switches the controller between those pin pairs.
- Both boards were left at the factory ADDR pin and both answered at `0x44`. Do not jumper ADDR unless a board stops answering. The firmware also accepts `0x45`.
- Readings at the bench were about 50% on both sensors, which is a plausible room value. Packets went out on ESP-NOW channel 1 with flags `0x03`.
- The chamber flag stays clear until the 10k from D1 to GND is installed. A missing pulldown can look like a real temperature, so Recharge stays blocked on purpose.

## Waveshare touch and screen

GT911 reset is EXIO1 on the onboard CH422G, not a direct ESP32 pin. INT is GPIO 4. Holding INT low through that reset selects address `0x5D`. If that reset is skipped, the touch controller does not ACK and every button is dead while the picture still updates.

The clock has no seconds, so its text changes once a minute. That small RGB update can tear. Those small copies wait for the end of a frame before they are written.

The AeroSentry mark is shown for a few seconds at boot and again after three idle minutes when no cycle is running. Tap it to return to the controls. A fuller card-style layout can wait until the controls are proven. Keep the cyan-on-navy palette. Do not copy the gold home-automation sample.

## XIAO wiring

No extra I2C pull-ups. The SHT31 modules already have them. Power both modules from the XIAO 3V3 pin, not 5V, so their pull-ups cannot put 5V on the XIAO pins.

- D4 SDA and D5 SCL: To-Engine SHT31 only.
- D8 SDA and D9 SCL: From-Engine SHT31 only.
- Leave both ADDR pins as they came. On the bench that was address 0x44 on both buses. A jumper is not required.
- Chamber thermistor: one lead to 3V3, the other lead to D1. A separate 10k resistor goes from D1 to GND. A 100nF capacitor from D1 to GND is optional but helps on the 1m cable.
- The thermistor needs that external resistor. The XIAO's internal pull-up is not accurate enough and must stay off.
- The 10k divider is installed and `CHAMBER_DIVIDER_INSTALLED` is enabled in the XIAO firmware. A dual-port capture showed the radio itself was reliable: packets arrived about once a second with no missing sequence numbers. The chamber flag was being cleared by a 12 mV ADC spread gate. Quiet spread was 1-12 mV, and finger contact reached 36 mV while the averaged voltage still represented a valid temperature. The XIAO now uses an 8-sample trimmed mean (dropping the highest and lowest) and rejects only a spread above 150 mV. Initial qualification requires three consecutive in-range samples.
- **Noise limit vs real heater ramp rate:** The 150 mV spread check measures ADC jitter across a 16 ms sample window (8 samples with 2 ms delays), NOT rate-of-change across seconds or minutes. Even with a powerful heating mat, thermal mass of the thermistor bead prevents a 150 mV (~10°C) jump within 16 ms. The noise limit will therefore remain valid during fast temperature rises.
- **Real heater mat test items when hardware arrives:**
  1. *Relay switching EMI:* High-current switching of the heater relay could induce transient electrical spikes onto the 1m thermistor lead. Watch the `spread` diagnostic when K3 switches. If spikes exceed 150 mV, install the optional 100 nF capacitor between D1 and GND.
  2. *Thermal overshoot & retry hold:* During any brief sensor dropouts, the controller holds the prior heater state for up to 6 seconds before aborting. With the real mat's wattage known, verify whether a 6-second heat hold near 240°F causes overshoot toward the 260°F safety cutoff. If so, tighten the hold or drop power near target.

## V1 boundaries

- Local touch-screen operation is the priority.
- One wired XIAO ESP32-C3 supplies both humidity readings by ESP-NOW.
- The history viewer is a web page, not a native phone app, so iPhone and Android use the same page. Target is about 30 days, with both humidity readings about once a minute. Charts stay off the operator screen.
- MeshCore on the Heltec WiFi LoRa 32 V4.3 is the remote-control path, not only a monitor. The board has the OLED. GPS is unplugged. The local link is ESP-NOW channel 1, not I2C and not an RS485 adapter. See "Next: Heltec V4" above.
- SD logging is still deferred. The clock module is the DS3231 described below.

## Future feature — direct Wi-Fi control, bypassing the mesh

Not implemented. To be built later as a user setting on the app or on the control screen.

Both the Waveshare and the Heltec can join a Wi-Fi network, and the phone could then talk to the unit directly over the internet instead of through the LoRa mesh. The blocker is not the radio, it is channel alignment: ESP-NOW and Wi-Fi share the same 2.4 GHz radio, so the Wi-Fi access point and the ESP-NOW link must sit on the same channel. Our link is on channel 1 (`AEROSENTRY_ESPNOW_CHANNEL`), so the hangar AP must also be on channel 1, or the channel define is changed and every ESP-NOW peer is reflashed. With Starlink or a phone hotspot the channel often cannot be forced, so the practical lever is "match ESP-NOW to the AP channel".

The Heltec is the right board for the internet path, not the Waveshare. The Waveshare is the control brain with LVGL, the I2C bus, and the state machine on it; adding network duty there is a reliability risk. Two workable patterns:

- **MQTT to a cloud broker.** The hangar Heltec joins Wi-Fi, publishes status and subscribes to commands; the phone app talks to the same broker. No home computer, no port forwarding, and it works behind Starlink's carrier-grade NAT. The SX1262 is a separate radio, so LoRa can stay as a fallback alongside it.
- **Tailscale at the hangar.** Run the existing bridge on a small always-on box at the hangar, or put Tailscale directly on a device there. The phone reaches it over Tailscale exactly as it does from home today.

Both need authentication. A Wi-Fi path that can energize the pump and heater must never be an open port on the public internet. "Bypass the mesh" also means re-pointing the web app's data source from the home bridge to the broker or tunnel feed, which is a real change to the app, not a flag.

## Recharge valve logic — revised 2026-11-15, two valves

**K5 (P0.4) = To-Engine valve**, energized dumps the regen exhaust to atmosphere (the vent line the pressure sensor watches). **K6 (P0.5) = From-Engine valve**, energized pulls fresh ambient air into the chamber for recharge, so no engine air is drawn in. Both are energized together for recharge and both are de-energized otherwise. K7 and K8 are reserved aux (future engine heater, future user accessory) and are masked off. K1 is unused.

**USS-PSV00033 is the wrong valve. Do not buy it again.** Bench test 2026-11-18: the coils clicked and the relay LEDs lit, and purge (valves at rest) sent the air the right way. Energized, the To-Engine valve only passed a wisp to the dump and most of the pump flow still went to the engine. The part is a 1/8 inch 3-way pneumatic pilot valve. Two of its threaded ports are the same outlet, and the real exhaust is a top diffuser, not a third plumbed port. The vent sensor rose anyway (+5,500 counts, then +19,253) because it sits upstream of that pinhole. Those steps are choked-orifice pressure, not a diversion. Do not tune `VENT_PRESSURE_MIN_STEP` from them.

Replacement, two of the same valve:

- 3-way, 2-position, spring-return normally closed. Power loss must put the engine loop back. No latching valve, no motorized ball that stays where it was.
- Direct acting, zero minimum pressure. A pilot valve that needs 20 PSI will not shift on this pump, and it will not seal on the suction side at all.
- Three real ports of one passage: common, engine port, divert port. Reject any valve whose "third port" is a muffler, or whose two side ports are the same gallery.
- Port and orifice at least the air-hose inside diameter. If the hose is 1/4 inch, that means 1/4 inch NPT and an orifice of 4 mm or larger, stated on the datasheet before purchase. Another 1/8 inch body repeats tonight.
- 12 V DC coil, under 1 A, with the flyback diode across the coil, stripe to +12 V. The bench relay contacts are only rated 3 A inductive.

Because recharge pulls fresh ambient air rather than engine air, the From-Engine probe is no longer the valve sentinel and the humidity-based position check was dropped as too sensitive to ambient conditions.

Recharge flow: heat to target, pulse until three good To-Engine samples, then **cool down** — up to 2 hours (ends early at 120 F), heater off, pump running, still venting — then a **safety check**: two rounds of 10-second pulse plus 1-minute sample. Both samples at or below 15% closes the valves and completes. A sample above 15% fails safe: error, idle.

**Vent pressure check (new).** A one-port gauge pressure module sits on the dump line upstream of the vent, reached through a 2.5 mm tube off a 1/4 in tee with a 1/4-to-2.5 mm barb reducer. It is not an I2C device: it is an HX711 load-cell ADC behind a pressure die, so it needs two plain GPIOs — **GPIO 48 (DOUT / the module's OUT pin) and GPIO 47 (SCK)**, physically adjacent on header J2. An older sentence in this paragraph had those two swapped; the firmware in `nodes/heltec-sensor/include/config.h` is the assignment. The library is `bogde/HX711`. The sensor node reads it and reports `ventPressureRaw` in the packet (version 3). The Waveshare learns an idle baseline **while the pump is off** (lowest quiet reading over 30 s), then each end-of-pulse sample is compared against it. A step of at least `VENT_PRESSURE_MIN_STEP` counts as flow. Six bad pulses with no good pulse at all is a valve fault: pump off, error, idle. This is a positive check on the mechanical valve position and does not depend on humidity at all. The raw value is logged every packet and shown on the admin screen.

**Coil current sense (built, disabled).** A 1-ohm 1 W shunt in the valve 12 V return: valve(-) — GPIO 3 tap — 1 ohm — 12 V(-). The 4 W coil draws about 0.33 A, so GPIO 3 reads about 330 mV energized, ~0 mV off; fault threshold 150 mV. A 1N4007 flyback diode goes across the valve coil, stripe to the + side. The 12 V supply negative must be common with the logic ground. GPIO 3 is an ESP32-S3 strapping pin, but at boot the relay is still open so the node sits at 0 V — the default strap state. `VALVE_CURRENT_CHECK_ENABLED` is **0** for the first all-up test because the shunt is not wired yet. Set it to 1 once the shunt and common ground are in place.

**Retired humidity trend check.** `VALVE_TREND_CHECK_ENABLED` is **0**. The code is kept in `statemachine.cpp` with its own local constants so it still compiles if re-enabled, but the vent pressure check replaced it.

**Failure policy.** Valve-related failures (vent pressure fault, coil fault) stop the pump immediately and idle — continued pumping is what pushes moisture at the engine. **Sensor loss with a trusted valve** keeps the venting cool down: 1 hour with the heater off, then valves closed, then idle.

**Bench test mode.** `RECHARGE_TEST_MODE` is **1**, which caps the chamber at 120F instead of 240F with 10F hysteresis (heat to 120, off at 110, back on at 120). Everything else about recharge is unchanged. Set it to 0 for production; the 240F / 5F values are preserved in the `#else` branch.

If saturation stops a manual or auto cycle and "Allow Auto Recharge Cycles" is checked, recharge starts automatically and the previous cycle resumes after the safety checks pass. Start and end times are logged. A manual recharge never resumes a cycle.

The old unattended-recharge provision is now implemented: the valves reroute to an open circuit vented to atmosphere, position confirmation is the vent pressure step, the fail-safe paths are as above, and power loss returns the normally-closed valves to the engine loop.

## Clock

The module on hand is a DS3231M with an AT24C32 EEPROM. The clock chip is I2C address `0x68`. It can share the Waveshare bus. No new ESP32 pin is required.

The relay board's second I2C header is in parallel with the first, so the module can connect there if the four names match. Do not assume the connector pin order matches the module pin order. Connect label to label:

- SCL to SCL
- SDA to SDA
- GND to GND
- VCC only to the 3.3V logic rail, the same rail as the Waveshare I2C pull-ups

Do not power the module from the 5V coil supply. The board has pull-up resistors from SDA and SCL to its VCC pin. A 5V VCC would put 5V on GPIO 8 and 9 and can damage the ESP32-S3. Leave SQW and 32K unconnected. Install the coin cell so the clock runs while the panel is unplugged.

The AT24C32 is not needed for timekeeping. If the bus becomes unreliable after the module is added, tie A0, A1, and A2 to ground so the EEPROM has a fixed address and stays unused.

The DS3231M driver is active. The firmware reads it at startup and refreshes the software cache once a minute. Clock-screen adjustments are written to the RTC, and Done commits the displayed time even when no field was changed; both paths clear the oscillator-stop flag and enable the backup oscillator. On first installation, open Set Clock, correct any fields that need it, and tap Done. If the RTC is absent, unreadable, or has invalid/stopped time, the firmware keeps the saved software-clock fallback and reports the fallback on the serial console. The AT24C32 remains unused.

## Custom carrier board design — 2026-11-18

Two-board custom carrier replacing the loose bench wiring. Two existing modules stay as plug-in units: the Waveshare ESP32-S3 4.3-inch Type B (control) and the no-display Heltec WiFi LoRa 32 V4.3 (sensor node).

**Board A — power/AC.** 120V 15A input with fuse and MOV, unswitched neutral/ground bus bars, 120V->12V and 120V->5V supplies, and all eight relays with the XL9535 at `0x21` and a ULN2803A-type coil driver. **Board B — control/DC.** Heltec V4.3 header, all sensor connectors, thermistor divider, HX711 pressure input, DS3231M RTC, and the Waveshare I2C + 5V JSTs. The boards join with one short keyed low-voltage bridge (5V, GND, SDA, SCL, VALVE_SENSE, AGND). No mains crosses the bridge. The split exists for noise as much as safety: the HX711 ADC and the LoRa radio are the sensitive parts, the switchers and relay coils are the noisy parts.

**Relay map on the new board — grouped by supply domain, not by bench history:**

| Channel | Bit | Function | Supply |
|---|---|---|---|
| K1 | P0.0 | Pump (~18W) | 120V AC |
| K2 | P0.1 | Heater mat (~500W) | 120V AC |
| K3 | P0.2 | AUX AC 1 (future engine heater) | 120V AC |
| K4 | P0.3 | AUX AC 2 (future accessory) | 120V AC |
| K5 | P0.4 | Case fan | 12V DC |
| K6 | P0.5 | To-Engine valve | 12V DC |
| K7 | P0.6 | From-Engine valve | 12V DC |
| K8 | P0.7 | AUX DC (switched output) | 12V DC |

The XL9535 wires straight down in order (P0.0 -> K1 coil ... P0.7 -> K8 coil), so the board reads top to bottom in supply groups. The bench board keeps its historical map (K2 pump, K3 heater, K4 fan, K5/K6 valves, K7/K8 aux) because that is what the current wiring and the working firmware assume. The remap is a constant swap in `include/config.h` when the custom board is built and the bench relay board is retired. The driver calls every relay through a named constant, so only the `RELAY_BIT_*` values change; the helper names in `relay.h` are voltage-scoped (`aux12v`, `auxAc1`, `auxAc2`) rather than channel-scoped, so each keeps pointing at the same function.

**Power budget (sizing the supplies), from verified part data:**

| Rail | Load | Max current |
|---|---|---|
| 12 V | valve coil (K6), 4 W solenoid | 0.42 A |
| 12 V | valve coil (K7), 4 W solenoid | 0.42 A |
| 12 V | case fan (K5) | 0.60 A |
| 12 V | K8 aux DC output (reserved) | 3.00 A |
| 12 V | **total** | **4.44 A** |
| 5 V | 8 relay coils at 71.4 mA (Songle datasheet) | 0.57 A |
| 5 V | Waveshare ESP32-S3 + 4.3" RGB LCD | 1.00 A |
| 5 V | Heltec V4.3 (LoRa TX at 28 dBm) | 0.35 A |
| 5 V | Board B 3.3 V rail + sensors | 0.15 A |
| 5 V | **total** | **2.07 A** |

The original 12 V @ 2 A supply was too small once a several-amp 12 V aux output was added alongside the valves and fan. The 12 V supply is now specified at 12 V @ 5 A, and the K8 aux output gets its own 3 A fuse so a fault in a future accessory cannot brown out the valves and fan. The 5 V supply is specified at 5 V @ 4 A for margin over the 2.07 A worst case.

**Source verification results (do not accept these from a draft):**

- **Songle SRD-05VDC-SL-C, from the manufacturer datasheet (electrobot.gr mirror):** coil 05 = 5 V nominal, 71.4 mA, 70 ohm, 0.36 W, pull-in 75% max, drop-out 10% min, max allowable voltage 110% of nominal. Contacts are Form C (SPDT): **resistive 10 A at 125 VAC, 7 A at 240 VAC, 10 A at 28 VDC; inductive (cos F 0.4) only 3 A at 120 VAC / 3 A at 28 VDC.** Max allowable power for Form C is 800 VA. Max allowable voltage 250 VAC / 110 VDC. The commonly quoted "10 A 250 VAC" is a marketing figure, not the datasheet rating. Our loads are comfortably inside it: heater 500 W = 4.2 A resistive, pump 18 W = 0.15 A inductive.
- **Heltec WiFi LoRa 32 V4.3 schematic (Altium source, meshcorepolska.org mirror):** USB VBUS reaches `VDD_5V` through fuse F1 (JSMD0805-300L) and P-channel MOSFET Q4 (Si2302CDS-T1-GE3) with R8 100K / R10 1K gate network, not a direct connection. Heltec's own documentation says to power from USB or the 5 V pin, never both. **GPIO 8 through 14 are not broken out to either header** — they carry the SX1262 LoRa SPI bus and the SPI flash. The board exposes 28 GPIOs total, matching the header silkscreen.
- **I2C topology: the Waveshare is the only master on the shared bus.** The Heltec cannot join it, because GPIO 8-14 are unavailable on the V4.3. The Heltec's firmware uses two private hardware I2C peripherals: `TwoWire(0)` on GPIO 17 (SDA) / 18 (SCL) for the To-Engine SHT40, and `TwoWire(1)` on GPIO 15 (SDA) / 16 (SCL) for the From-Engine SHT40, both at 100 kHz. Heltec-to-Waveshare communication is ESP-NOW, not I2C. The shared bus (Waveshare GPIO 8/9) carries only the GT911 touch, the CH422G, the XL9535 at `0x21`, and the DS3231M at `0x68`.

**Waveshare ESP32-S3 4.3-inch Type B GPIO map (from our own firmware, `src/display.cpp`):**

LCD RGB bus (all consumed): GPIO 14, 38, 18, 17, 10 (B0-B4); 39, 0, 45, 48, 47, 21 (G0-G5); 1, 2, 42, 41, 40 (R0-R4). Control: hsync 46, vsync 3, de 5, pclk 7.

So the LCD alone eats: 0, 1, 2, 3, 5, 7, 10, 14, 17, 18, 21, 38, 39, 40, 41, 42, 45, 46, 47, 48 - twenty pins.

Other board use: GPIO 8 = SDA, GPIO 9 = SCL (shared I2C bus, confirmed in `include/config.h`). GPIO 4 = GT911 INT. GPIO 43/44 go to the onboard CH422G (RS485 + touch reset), so they are not a plain UART.

**Free GPIOs on the Waveshare, for the Board A UART link:** GPIO 11, 12, 13, 15, 16, 19, 20, 33, 34, 35, 36, 37, 43, 44 (43/44 only via the CH422G, so not directly usable as a plain UART). Practically free and directly available: **11, 12, 13, 15, 16, 19, 20, 33, 34, 35, 36, 37**. Note GPIO 19/20 are the native USB D-/D+ and are used by USB CDC for flashing and serial logs, so they are not a good choice for a permanent link. GPIO 35 drives the onboard white LED. GPIO 36 is the Vext control on the Heltec, not relevant here.

**Chosen UART assignment:** **GPIO 15 = Waveshare TX (to Board A RX), GPIO 16 = Waveshare RX (from Board A TX).** Both are free, both are on the Waveshare's header breakouts, and neither is a strapping pin for the S3 in a way that matters here. This changes the Waveshare connector from the old 4-pin I2C + 2-pin power scheme.

**Revised Waveshare to Board B connection:**

| Connector | Pins | Carries |
|---|---|---|
| JST, 8-pin | 5V, GND, TX (GPIO15), RX (GPIO16), plus 4 spare/ID | power and UART |
| JST, 4-pin | SDA (GPIO8), SCL (GPIO9), 3V3, GND | I2C bus for GT911 + DS3231M |

The I2C connector stays because the GT911 touch and the DS3231M RTC still live on the Waveshare's I2C bus. The XL9535 moves to Board A, so it leaves the Waveshare's bus entirely.

**Note on GPIO 19/20:** they are the native USB pins and the firmware uses USB CDC for flash and serial logging. They must stay on USB.

**Note on the CH422G:** GPIO 43/44 reach the onboard CH422G, which drives the RS485 transceiver and the GT911 reset. They cannot be used as a direct UART to Board A without disrupting the touch reset sequence, which would kill the touch interface. The onboard RS485 connector is therefore not usable for this link.

**RS-485 upgrade path:** the protocol layer is transport-independent (CRC16 on every frame). If the cable ever needs to be longer than a few metres or needs galvanic isolation, swap the UART electrical layer for RS-485 with a transceiver on each end; the message format does not change.


**Corrections found during the design review, worth keeping in mind:**
- Songle SRD-05VDC-SL-C pinout is 1=coil+, 2=coil-, 3=COM, **4=NO, 5=NC**. Every load wires COM -> NO so it is dead when the coil is off. NO and NC swapped is a silent fail-dangerous error.
- The HX711 pressure module must be powered from **3.3V, not 5V**. Its DOUT pull-up follows the module VCC, so 5V would drive ~5V into a 3.3V ESP32-S3 pin, and its SCK high-threshold (0.8 x DVDD) at 5V would reject the 3.3V clock. "Fine with either" refers to the module's power input only.
- The vent pressure pins are **DOUT = GPIO 48, SCK = GPIO 47** (from the code). An earlier note in these docs had them swapped.
- The Heltec V4.3 silkscreen gives header pin numbers only, not GPIO names. Confirmed map: J2 = GND, 5V, Ve, Ve, 44, 43, RST, 0, 36, 35, 34, 33, 47, 48, 26, 21, 20, 19 with top pair 17 (inner) / 18 (outer); J3 = GND, 3V3, 3V3, 37, 46, 45, 42, 41, 40, 39, 38, 1, 2, 3, 4, 5, 6, 7 with top pair 15 (outer) / 16 (inner).
- `VALVE_CURRENT_MIN_MV` is 150 mV. With both valve coils on the shared 1 ohm shunt the reading is about 660 mV, one coil about 330 mV. Both clear the threshold, so no firmware change was needed.

### Heater thermal control (decided 2026-11-18)

**The external thermistor stays the primary control sensor.** It sits in the seam where the mat edges meet, on the outside of the tube near the middle of its length, reading tube-wall temperature. Firmware regulates it to 240 F (120 F in bench test mode). It stays where it is.

Reasoning: the desiccant sits against the inner wall, so inner-wall temperature is what protects it, and the inner wall is *cooler* than the outer wall (heat flows inward to the desiccant and outward to ambient). The seam thermistor reads the outer wall, so holding it at 240 F bounds the desiccant with margin. Waiting for centre-air to reach 240 F would instead require driving the mat harder, and the wall-adjacent desiccant would see that overshoot first. Stagnant air in a desiccant chamber makes the lag worse, not better.

**The seam reading IS representative of the tube wall.** An earlier concern that the seam would under-read the mat-covered hot zones was wrong and is retracted. The tube is aluminium and conducts well along its length, the gap is only 8 mm, the seam is covered with thermal kapton tape, and an aluminium-fiberglass insulation layer wraps the assembly. The axial gradient (seam versus covered) is therefore small. The gradient that actually matters is radial: mat surface, outer wall, inner wall, desiccant. This means:
- The external cutoff belongs in the seam area with the control sensor. Same location is correct, not a compromise.
- If the seam is later found to read unrepresentatively, the mat can be pulled back and the sensor tucked under it. That is a bench-time physical adjustment, and it changes calibration, not validity.

**Silica gel:** the desiccant is silica beads. 240 F sits inside the commonly quoted 200-250 F regeneration range, with some sources extending to 300 F for standard beads. 240 F is a sound operating point; exceeding a bead's maximum regeneration temperature degrades it, which is why the hardware cutoff exists.

**The mat NTC is a LIMIT on the mat itself, not a second control sensor.** Two jobs:
1. It bounds the mat's own maximum service temperature, which the wall sensor cannot see.
2. It catches loss of mat-to-tube contact. If the mat lifts locally, that region runs hotter while the tube reading stays at target, so the controller drives harder and the gap widens. The NTC sees this directly.

It is a redundancy against the wall sensor, not a correction for a large seam offset. It still belongs in the centre of the mat's width at mid-length, the true hot zone, and the two sensors stay in different places on purpose so a single local failure cannot fool both.

**No thermistor inside the tube for now.** A penetration into the desiccant vessel is a mechanical and seal change with real consequences - a leak ruins the desiccant and a slow air leak defeats the drying. Desiccant temperature is well approximated by the inner wall, which is below the wall temperature already measured from outside. The mat NTC covers the mat's own temperature with no penetration. If chamber air temperature is wanted later for cycle timing, the SHT40 probes already report temperature.

**Measurement plan:** the v2 mat NTC and the seam thermistor logged together during heat-up quantify the mat-versus-wall offset, which is the number that sets the NTC limit and validates the cutoff set point. A thermal camera is a good spatial check for hot spots and for spotting the mat losing contact, but note the emissivity asymmetry: silicone has high emissivity and reads accurately, while bare aluminium has very low emissivity and reads badly. Put a small matte high-emissivity tape patch on the tube wherever an IR reading is wanted.

**Mat v2 spec, to request from the manufacturer:**

- 120 VAC, 500 W confirmed. 18 AWG stranded leads to a connector.
- Embedded 100K ohm beta 3950 NTC with its own 2-wire lead to a separate connector, positioned in the centre of the mat's width at mid-length (the hot zone).
- Factory-set, non-user-adjustable high-limit cutoff in the mat's internal wiring, in series with the element, rated 250 VAC and 15 A. Latching self-hold preferred over a one-shot fuse so a trip cannot be silently replaced in the field.
- No buttons and no display on the mat. If a thermostat is included, only its dry contact is exposed.
- Uniform watt density, no hot spots. State watts per square inch, maximum service surface temperature, and element resistance in ohms.
- Insulation class, moisture rating, and UL or CE listing for the assembly.

**Optional firmware idea, no hardware:** near target, shorten the heater on-window instead of full on/off (duty limiting) to reduce overshoot and element stress. Same relay, same fail-safe character.

### Temperature safety ladder

Four rungs, each with a different sensor and a different failure mode:

| Rung | Set point | Sensor | Action |
|---|---|---|---|
| Control | 240 F | seam thermistor (external) | normal heater cycling |
| Firmware abort | 260 F | seam thermistor (external) | cycle stops, all outputs off |
| External cutoff | 270 F | bimetal or fuse at the chamber | whole AC feed drops, latches |
| Mat internal cutoff | per mat service temp | inside the mat | element drops |

Note the gap: the firmware abort at 260 F and the hardware cutoff at 270 F are both driven by the *same* seam sensor. If that sensor fails low, the controller drives the mat continuously and neither of the first two rungs can act - only the external cutoff and the mat's own cutoff remain. That is precisely why the mat NTC has to be an independent path into the Board A MCU rather than another firmware check on the same reading.

**Select the cutoff on hold current, not just trip temperature.** A cutoff or fuse mounted in a hot enclosure must have a hold current rating above the actual line current at the ambient temperature it will see, or it nuisance-trips during a normal cycle. The line carries the heater plus converter input, roughly 5 A total. Confirm Ih at the mounting temperature.

### Independent overtemperature layers (decided 2026-11-18)

Three layers, deliberately independent of each other:

1. **External cutoff at the chamber**, in series with the entire 120 VAC feed so a trip kills heater, pump, valves and converters. Latching, resettable only by opening the enclosure. Set point around 280 F, pending mat surface temperature data. It remains the last thermal layer if current sense fails, if K9 itself welds, or if the mat runs away on current that was commanded. It is not the only protection against a hung controller or a welded heater relay; that electrical class is K9, below.
2. **Mat NTC into the Board A MCU**, independent of the Waveshare. Board A drops the heater on its own if mat temperature exceeds the limit derived from the mat's maximum service temperature.
3. **The mat's own internal cutoff**, manufacturer-fitted.

Board A should also provision one spare analog input for a second wall thermistor near the heater's end, for gradient monitoring later.

**Accepted consequence:** a trip of the external cutoff kills the controller, so there is no on-screen record of that fault. A K9 drop does not kill the controller; Board A should report it.

### K9 heater disconnect and fault-response gate (decided 2026-11-18)

K9 is a ninth relay. It is not an XL9535 channel and must not be added to the K1-K8 map above. It sits in series with the heater only. K2 still does the bang-bang cycling. K9 stays energized while the heater is armed and drops on a fault, so it rarely switches.

It covers two electrical faults the temperature ladder does not: heater current flowing while the command says the heater is off (welded K2 contact or a stuck driver), and the controller going quiet while the heater was left on (hung MCU, lost UART heartbeat).

**K9 is not short-circuit protection.** The proposed AC acquisition takes at least 0.8 s for four channels, plus processing and relay release. That cannot catch a dead short. Hard shorts are the branch fuse's job and open in milliseconds. Do not describe K9, or the AC scan, as fast heater-current protection.

**Response-time requirement, thermal class only:**

- Maximum time from (a) loss of a valid heartbeat while the heater is armed, or (b) detection of heater current while the heater command is de-energised, to full interruption by K9, shall not exceed 5 seconds. Design target 1 second.
- Why that number is enough: 500 W into the aluminium tube and silica bed climbs on the order of tens of degrees F per minute. One second of extra on-time adds well under 1 F. Five seconds is still inside the margin between 240 F control, the 270 F seam cutoff, and desiccant tolerance. The control loop already runs at 1 Hz. The safety path does not need to be faster than the control path. It needs to be independent of it.
- The 0.8 s four-channel AC scan is acceptable for this class. It is diagnostic monitoring, not fast protection. This is a closed safety-review gate. Do not reopen it as if the scan speed were still undecided.

**Constraints that make the requirement real:**

- K9's coil driver is independent of the XL9535 that drives K2. The same expander for both is theater: one fault takes out the normal path and the safety path together.
- K9 is de-energised, heater disconnected, during every power-up and every brownout, until a permission latch is satisfied: rail OK, and MCU heartbeat present, and no fault latch. The watchdog alone is not sufficient. This has to be proven across the rail ramp and the rail collapse, not asserted.
- The permission latch lives on Board A. The Waveshare still decides when the heater should be on. Board A owns the bounds and can drop K9 without the Waveshare.
- Coil voltage and driver part are not chosen yet. Do not fold the K9 coil into the 5 V budget table until the part is real. Whatever the coil voltage, its enable must not come from an XL9535 pin.

### What the earlier paragraphs in this section no longer say

Path B is the architecture. Board A has its own MCU and owns the bounds: current, time, mat temperature, link loss, and the hardware gate that drops K9. The Waveshare still decides when. Where the Board A / Board B paragraph above conflicts, this list wins:

- The bridge is UART plus power, not shared I2C. Waveshare GPIO 15 is TX to Board A RX. Waveshare GPIO 16 is RX from Board A TX. CRC16 stays on every frame so RS-485 can replace the electrical layer later without a message change. Do not use the onboard RS485 connector. GPIO 43/44 are the CH422G.
- The XL9535 moves to Board A's private I2C and leaves the Waveshare bus. GT911 and the DS3231M stay on Waveshare GPIO 8/9.
- Coil drivers are discrete logic-level MOSFETs, not a ULN2803A.
- The shared valve shunt and the bridge VALVE_SENSE / AGND pins are dropped. Per-channel sense on Board A replaces both. K6 and K7 are separate channels, so each valve coil gets its own shunt.
- Relay class is HF115F / G5LE, not the bench Songle, once the custom board exists. The bench board and its firmware map stay as they are until that board is retired.
- Supplies stay 12 V at 5 A and 5 V at 4 A.

### Still open before a schematic is committed

These are engineering gates, not approval to energize mains. Heater diagnostic pulses stay disabled until an allowable test-energy budget is written down. Do not guess footprints, and do not treat a differential connector terminal as raw UART.

| Item | Who | What is still missing |
|---|---|---|
| AC current sensor | board design | A real CT, not a placeholder. Resolution must tell 0 mA from heater-on. Response and recovery are open. |
| Permission latch | board design | Prove K9 is OFF through power-up and brownout, not only while the watchdog is running. |
| Seam cutoff | mat vendor, then us | Hot continuous-current rating at mounting temperature, about 5 A line near 240 F. Trip behavior. Mixed-load interruption: heater resistive, pump inductive, converters on the same feed. |
| Seam cutoff aux contact | board design | Isolate it before it touches a Board A GPIO. |
| Mat NTC | mat vendor | Curve (expect 100K beta 3950), insulation, 2-wire vs 3-wire, maximum service temperature. Spare gradient input stays a separate analog channel. |
| Waveshare GPIO 15/16 | board design | Firmware assignment is settled. Physical raw-pin access on the module is not verified. |
| Heltec headers | board design | Mating geometry, and proof that USB and the 5 V pin are never both sources. Schematic already says never both. |
| Branch fuse | us, then board design | Pump locked-rotor inrush and any aux inrush. Coordination waits on those numbers. |
| Test energy | us | How long the heater may be pulsed before the safety path is proven. Gates the first heater test on the new board, not tonight's bench. |

### Tooling (2026-11-18)

Flux's trial was 1000 credits. Those were used in about two hours, still in discussion, before a schematic existed. The $100/month plan includes 1200 credits. Do not use Flux as the workhorse to the finish line. The written spec in this file is the deliverable and does not belong to any tool. Flux is useful as a checkpoint reviewer. Schematic capture can move to KiCad or EasyEDA, with a one-time human review of the mains layout if that is wanted. Nothing about this board blocks the bench test. Do not reflash the Waveshare before that test.



## 2026-11-20 full-runtime update

**Flash (Waveshare only, Heltec unchanged):**
- 1,855,024 B, hash verified, `[SUCCESS]` on COM4 (auto-detected, build in `include/config.h`).
- Superseded on 2026-11-21 (see the section below): target is now 245 °F, software trip 255 °F, max run 6 hours.

**Vent-pressure rework (per 2026-11-20 discussion):**
- Ambient baseline is now a **running average of pump-OFF samples every 30 s** during heating and between pulses, re-learned on every recharge (not a single 30 s lowest, not carried over from a previous cycle).
- Gate is **20,000 counts above that average**, judged on the **peak** seen at any point during each 10 s pump pulse, not just the last packet before the phase timer.
- Calibration: on the 2026-11-20 bench run, a real divert produced `step 72,274 OK` (4528782 → 4599846). A truly disconnected HX711 ambient produced `step -2238 NO FLOW` and the safety stopped immediately (`Vent pressure fault - pulse did not rise 20,000 over ambient` → `Stop: Valve position suspect.`).
- `_ventPulsePeak` is owned by the PULSE_ON phase and reset at end-of-pulse, so each subsequent pump pulse stands on its own 10 s window — a prior good pulse does not hide a later disconnect.

**Recharge max / cooldown:**
- `RECHARGE_MAX_MS` was 3 hours (10,800,000). A 3-hour run with the chamber still pushing 100% humidity hit that and **hard-stopped without a proper cooldown**.
- Superseded 2026-11-21 by the 6 hour limit below. At the 4 hour limit, the code now prints `Recharge 4h max - cooldown, no resume` and enters `COOLDOWN` (fan ON, valves OPEN, pump pulsed 10 s / 10 min) for the safety pulses before idle. If already in COOLDOWN, it stops cleanly.
- Loose cooldown pulse: **10 s ON / 10 min OFF**, valves OPEN, early exit when chamber ≤ 120 °F (`RECHARGE_FAILED_FAN_TEMP_F`) or after 1 h.

**Sensor-loss holdout (2026-11-20):**
- `toEngineReady()` / `fromEngineReady()` now require a fresh *per-sensor* timestamp within `SAMPLE_MAX_AGE_MS` (3 s), not just any packet with the flag set.
- `SENSOR_LOST_HOLDOUT 1` and `HOLDOUT_RETRY_MS 6000` add a 6 s grace: a brief humidity/chamber dropout holds heat and keeps the last sample instead of ending recharge instantly. **Superseded 2026-11-23:** that grace was written wrong. See the sensor dropout section below.
- Heltec flags (`0x0F` = all valid) dropped to `0x0B` (chamber retry) several times in the live run; those dropouts no longer kill the cycle instantly.

**Admin pressure panel:**
- `uiUpdateAdmin()` now shows raw pressure, running ambient average, pulse peak, step, OK/NO FLOW, and `good/bad` counts. Tap the AeroSentry title 10 times to open it. Serial `capture_serial.py` on COM4 prints `Vent ambient sample ...` every 30 s and `Vent pressure raw ... peak ... step` per pulse.

**Test checkpoint (2026-11-20 PM):**
- Disconnected-from-start run: correctly stopped on the **first** pulse (step -2,238, fault asserted).
- Connected run: `step 72,274 OK` — passed.
- After this flash, run a full 4-hour / 240 °F run and verify `Recharge 4h max - cooldown` appears, then the safety pulses run.

## 2026-11-21 recharge tuning (flashed to the Waveshare, not yet run on the bench; first-hour start and max-run start corrected 2026-11-22)

**Settings, all in `include/config.h`:**
- Max run time: **6 hours at temperature** (`RECHARGE_MAX_MS` 21,600,000). The clock starts when the chamber first reaches target. Heat-up time does not count.
- Chamber target **245 F**, 5 F hysteresis: heater off at 245 F, back on at 240 F.
- Software thermal trip **255 F**. `THERMAL_CHAMBER_F` now takes this value from `RECHARGE_SOFT_OVERTEMP_F`, so there is one number. It was 260 F before, which was above the 255 F you asked for.
- Mechanical thermal fuse **266 F**. Hardware only, nothing in firmware reads it.
- The humidity-sensor trip (`THERMAL_SENSOR_F`, 250 F) was not changed.

**Heat-up ramp:** while the recharge is in its heating phase, the heater runs 30 s on / 15 s off (`RECHARGE_RAMP_ON_MS`, `RECHARGE_RAMP_OFF_MS`), and stays off once the chamber is at or above target. When the target is first reached the normal hold takes over. The ramp runs once per recharge.

**Pump timing:**
- First hour: 10 s on, 1 min off.
- After the first hour the pulse length is `RECHARGE_LONG_PULSE_ON_MS`, now 10 s, the same as the first hour (2026-11-23). It was 30 s, then 15 s. The value stays adjustable in `config.h`.
- The first hour starts when the chamber first reaches target (`_chamberReachedMs`). Heat-up time does not count. Corrected 2026-11-22; the first flash counted from the start of the recharge.
- The 6 hour max run time also counts from the chamber first reaching target (changed 2026-11-22).
- Because that clock only starts at target, there is a heat-up backstop: if the chamber has not reached target within 1 hour of the start (`RECHARGE_HEATUP_MAX_MS`), the recharge stops with `HEATUP_TIMEOUT` and the message "Chamber did not reach target temperature." Without it, a dead heater or a bad thermistor would run with no time limit. The 1 hour value is my choice, not yours. Change it if heat-up can legitimately take longer.
- The 20,000 count vent check still judges every pulse against the ambient average, including the long ones.

**Vent peak tracking was wrong before this build.** The notes above say the pulse is judged on the peak at any point in the 10 s. That was not true. The peak was only updated at the end of the pulse, so the check saw one reading, not the whole pulse. This build tracks the highest reading on every loop while the pump is on, and the end-of-pulse check uses that.

**Max run time behavior:**
- At the limit, if the recharge is pulsing, it prints `Recharge 6h max at temperature - cool down, then safety check, no resume`, runs the cool down and the safety check, then stops with the "6 hours at temperature" message.
- It is latched, so it cannot fire twice. The earlier 4 hour version had no latch.
- If the cycle is already in cool down or the safety check at the limit, it finishes normally and reports complete.

**Housekeeping:** the 2026-11-20 sections of this file had been saved with broken characters (`â€”` for dashes, `Â°` for the degree sign). I repaired them. The file keeps CRLF line endings and now has no byte order mark. The text is otherwise unchanged apart from the two lines marked superseded above.

**Not verified:** none of this has run on the heater. Watch the first ramp: the ramp timing, the chamber overshoot past 245 F, and the 30 s pulse step after the first hour.

## 2026-11-22 recharge stopped about an hour after reaching temperature

**Cause: a false saturation stop, not the pressure sensor.** `checkSaturation()` stops any running cycle when To-Engine stays above 40% for 30 s with the pump on. It had no exclusion for recharge, although `config.h` and the boot line both say recharge ignores 40%. During recharge To-Engine reads near 100% because it sits in the regeneration exhaust. With 10 s pulses the pump was never on for 30 s, so it never fired. After the first hour the pulse became 30 s, the same length as the timer, so it eventually fired.

**Evidence from the serial log of the stopped run:**
- Heat-up took about 12.6 min. The first pump pulse was at 12.6 min, and the stop came 64.2 min after it, about 4 min into the 30 s pulses.
- All 55 vent checks passed (`good 55 bad 0`). The steps I read were roughly 83,000 to 133,000 against the 20,000 gate.
- The last 30 s pulse ended normally (vent check OK, phase advanced to the wait). In the same pass the cycle stopped with `Stop: Desiccant saturated`. There is no `Pump OFF` line before it.
- The first two 30 s pulses ended a few milliseconds before the 30 s timer and got away with it. The third did not.

**Fix:** `checkSaturation()` now returns early in recharge. Purge, manual and auto are unchanged.

**Error messages stay until acknowledged.** The stop message was shown once, then the idle logo replaced it after 3 minutes without a touch, and touching the logo went to the main screen. The message was lost. Now `uiShowWarning()` marks a message as waiting (`uiWarningActive()`) and the idle logo never starts while one is. It is cleared only by pressing OK. A message that arrives while the logo is up replaces the logo, and the logo can return after OK. This was a separate bug from the false stop above. Before the fix no message could be read once the screen saver started.

**Not changed, worth watching:** the ambient average used by the vent check drifted from 4,721,935 at the start to about 4,745,000 an hour later. That is small against the 20,000 gate and the steps are far above it.

## 2026-11-22 long pulse cut from 30 s to 15 s

After the saturation fix the cycle ran past the one-hour mark. With 30 s pulses the chamber cooled too much and noticeably less moisture came out of the line. `RECHARGE_LONG_PULSE_ON_MS` is now 15,000 ms. Everything else is unchanged: the first hour counted from reaching temperature is 10 s on / 1 min off, then 15 s on / 1 min off. Not yet run on the bench. Check whether the chamber holds 240 to 245 F and whether moisture keeps coming out at 15 s.

## 2026-11-23 cool down bug, pulse back to 10 s, and what the run7 recharge showed

**Cool down was skipped on every run (bug).** The cool down exit compared `sensors.chamberTemp` to 120 directly. That value is in Celsius, and 120 C is 248 F. The chamber holds 240 to 245 F, so the test was true on the first pass. In the run7 log the cool down phase and the safety pulse phase print on consecutive lines with no sensor packet between them, and the safety sample was taken about 70 s after the 6 hour latch with the chamber near 248 F. It now converts to Fahrenheit first. The same code runs when a recharge ends normally, so no earlier run had the one hour fan cool down either.

**The run7 "failed" recharge probably was not a real failure.** It ended at the 6 hour limit with To-Engine at 43%, and the safety check failed ("Sample above 15%"). That sample was taken hot, with the beads still releasing moisture. With the fixed cool down the beads reach 120 F first, stop releasing, and can start absorbing, so the safety sample may pass. Cooling to 120 F was chosen on purpose, because a hangar can be that hot in summer and the system should not wait on a temperature it may never reach.

**Pulse length.** To-Engine exit air temperature read about 150 F with 10 s pulses and about 130 F with longer ones (watched on the display, not in the serial log). The goal is to heat the beads, not the chamber wall, and more airflow cools the beads. Pulses are 10 s before and after the first hour for now. The chamber thermistor did not show this: the dip after each pulse was about 5 to 6 F for 10, 15 and 30 s pulses.

**Drying rate in run7 (To-Engine RH at the pulses):** 93% at 71 min after reaching temperature, 75% at 161, 54% at 281, 43% at 360. It was falling about 0.13 to 0.18 points per minute. At that rate 15% needs several more hours with 4 lb of beads.

**Desiccant amount.** About 4 lb now. Halving it was discussed and is not being done yet. 2 lb may last 2 to 4 weeks between recharges, which may be enough.

**Not yet verified:** the cool down fix has not run on the heater. Watch the first cool down. It should take a while to fall from 245 F to 120 F with the fan on, then run the two safety pulses. The cool down is capped at 2 hours (raised from 1 hour on 2026-11-23).

## 2026-11-23 cool down cap raised to 2 hours

`RECHARGE_COOLDOWN_MS` is now 7,200,000 ms (2 hours), up from 1 hour. Falling from about 245 F to 120 F with the fan on and 10 s pulses every 10 minutes may take more than an hour, and at the old cap the safety check would have run with the beads still warm. The cool down still ends early the moment the chamber reads 120 F or lower. The same constant is the cap for the failed-recharge cool down. Not yet run on the heater.

## First system operating test: auto dryer, no auto recharge (started 2026-11-23)

First run of the whole system in its normal operating mode: an **Auto Dryer** cycle with **Allow Auto Recharge Cycles unchecked**. If the desiccant saturates, the cycle stops and shows the saturation message. It does not start a recharge.

**Started:** about 11:10 on 2026-11-23, by the user at the touch screen. The serial capture started a minute or two later, so the first part of the cycle is not in the log.

**Firmware under test:** the build flashed on 2026-11-23 to the Waveshare, COM4, MAC `94:a9:90:df:bb:54`, 1,855,680 bytes, SHA-256 `6A71D55C...A9347`. It includes the cool down Celsius fix, the 2 hour cool down cap, 10 s pulses before and after the first hour, the recharge exclusion from the 40% saturation stop, and error messages that stay up until acknowledged. The Heltec radio (MAC `10:BD:A3:5A:7C:94`) and the sensor node (MAC `F8:5B:1B:BF:3F:EC`) were not touched.

**Desiccant:** about 4 lb. The last recharge (run7) did not finish drying it. It ended hot at the 6 hour limit with To-Engine at 43%, and the safety check failed. The beads were dried further by a closed loop purge after that, where To-Engine held steady at about 10% once the chamber had cooled (user observation, not in a log). Whether the beads are fully dry is what this test starts to answer.

**Starting conditions seen in the log:** chamber about 58 F, vent pressure about 4,685,000 counts, all three sensor flags valid (0x0F).

**Serial log:** `C:/Users/sjohn/AppData/Local/Temp/aerosentry-first-system-test-auto-dryer.log`, 12 hour capture. Not being polled. Review it after the run.

**What to look for in the review:**
- Whether To-Engine and From-Engine stay low, and how the From-Engine target (20%) is being met.
- The quick-sample and sample-wait cycle: pump on/off timing, and whether a quick cycle ever ends early.
- Any stop, and the stop message. A saturation stop (To-Engine above 40% for 30 s with the pump on) would mean the beads are not holding.
- Vent pressure readings, if any pump pulses run with the valve open. Auto dryer is a closed loop, so the valves should stay de-energized.
- Anything that should not happen: sensor-lost stops, a screen message that disappears, or a reboot (the millisecond counter going backwards).

**Result:** run 1 was ended twice by sensor-dropout stops (fixed by the 30 s grace). Run 2 ran 10.3 h with
no stop; the capture ended because the user unplugged the board (2026-11-24). Neither run had the auto dryer
sample fix, so the first reading of each cycle was old chamber air.

## 2026-11-23 sensor dropouts stopped the first auto dryer test (my bug)

**What happened.** The auto dryer cycle stopped twice with "To-Engine sensor lost", about 22 min and about 6 min into two cycles. The To-Engine sensor was not the problem.

**What the serial log shows (`aerosentry-first-system-test-auto-dryer.log`):**
- Every sensor packet has flags `0x0F`. The To-Engine flag was never clear in any packet. The sensor read fine every time.
- Both stops came right after a gap of about 3.17 s with two packets in a row never received (sequence 48430 to 48433, and 49796 to 49799). The ESP-NOW link dropped two frames in a row.
- Gaps of about 2.0 to 2.1 s (one frame lost) happened several more times and did not stop anything. The stop threshold was crossed exactly when two frames in a row were lost.
- The Heltec link beacons from the other radio were received normally during those gaps. The receiver and the channel were fine. Only the sensor node's frames went missing.

**The cause is my 2026-11-20 change.** Before it, the dryer modes only needed a packet within 15 s. I changed the readiness test to require each sensor to have a reading within `SAMPLE_MAX_AGE_MS` (3.0 s), with no grace outside recharge. Two lost frames is 3.17 s, so it stopped the cycle. I only exercised that change in recharge, never in a dryer cycle.

**A second flaw in my grace code.** The grace I added for recharge had `!toEngineReady()` in its hold condition. Whenever To-Engine was not ready in recharge, that was true, so the hold never ended and the cycle never stopped. With the sensor node silent the chamber reading goes stale too, so the thermal check cannot run, and the heater would have stayed on whatever it was last commanded. The mechanical fuse was the only backstop. This was in the firmware flashed for the 11-22 runs and would have been in tonight's recharge.

**Fix (2026-11-23):**
- One grace for every mode. When a required sensor is missing the cycle holds for `SENSOR_LOSS_GRACE_MS` (30 s), then stops as before. The 30 s is my choice. The longest gap in any log I have is 3.24 s, so it is about ten times that. Change it in `config.h`.
- While holding: no new decisions. In recharge the heater is regulated only if the chamber reading is fresh, otherwise it is off. The pump and fan hold their state, so an auto dryer pump that was on stays on for up to 30 s.
- Two serial prints, `Sensor dropout - holding` and `Sensors back after N ms`, make dropouts visible in logs from now on.
- In recharge, a missing From-Engine reading at the end of a pump pulse no longer fails the cycle at once. The pump turns off and waits up to the same grace.
- When the sensor node is silent the stop message now says "Sensor node not responding" instead of naming a sensor.
- Removed `HOLDOUT_RETRY_MS`, `SENSOR_LOST_HOLDOUT` and `chamberHoldoutPending()`.

**Why the radio is dropping frames is not established.** In the dryer test log 12 of 3,107 frames never arrived (about 0.4%) over 0.9 h. In the 8 hour run7 recharge log, 3 of 27,246 (about 0.01%). That is roughly 35 times worse now. Two of the six gaps fell at the start of a cycle, when the pump and fan relays switched, but that is too few to call it a cause. The sensor node sends broadcast frames with no acknowledgement and no retry, so one fix on that side would be to send each reading twice, or faster. Not done.

**Noticed, not changed:** if a sensor is lost mid-recharge after the chamber has reached temperature, `rechargeFailureCooldown()` runs on every pass while the sensor stays missing. It resets the phase each time, so the failure cool down does not advance until the sensor returns. The heater is off but the pump stays in its last state. This existed before today.

**Not yet run on the heater or in a dryer cycle.** Watch for `Sensor dropout - holding` followed by `Sensors back after` in the log.

## 2026-11-23 evening state before the handoff to GitHub Copilot (nothing flashed in this step)

**Firmware on the Waveshare right now (COM4):** the 30 s sensor-loss grace build. `SENSOR_LOSS_GRACE_MS` is 30,000 in every mode and replaces the broken holdout code. It prints `Sensor dropout - holding` and `Sensors back after N ms`. It was flashed to COM4. Whether the esptool verify_flash read-back was run on this particular flash is not recorded here, so treat the image as flashed but not independently verified. It does NOT have the auto dryer sample fix below.

**Built, not flashed: auto dryer sampling fix.** The flash is on hold until the user says dryer run 2 is over.
- The cycle now starts in `SAMPLE_RUN` (printed as `Auto phase 4`). The pump runs 10 s, the air settles for 10 s, then the sensor is read. Before this, the cycle started in `DRYING` and read From-Engine 13 to 15.7% from old chamber air within about 1 s of pump start. That showed in both dryer logs.
- `DRYING` ignores readings for the first 10 s of pump time.
- `QUICK_SAMPLE` now pumps 10 s, settles, then reads. It used to wait with the pump off and read stale chamber air.
- `AUTO_SAMPLE_SETTLE_MS` is 10 s (was 30 s, which let the air mix with dry chamber air and read falsely low). `AUTO_SAMPLE_RUN_MS` is 10 s.
- Build: 1,855,920 bytes, SHA-256 `C5A921598D85F755FBF08E8DFFB9D1AC3D7043A0804FC5177BE5D677D9EBAEFD`, made by `pio run` with no warnings printed (incremental build, not a clean build).
- Not verified on hardware. The first thing to check after the flash is `Auto phase 4`, about 10 s of pump, then a settle, before any decision.

**Auto dryer run 2 (grace-30 build, no sample fix):** serial capture `C:\Users\sjohn\AppData\Local\Temp\aerosentry-auto-dryer-run2-grace30.log`, started 12:26:59 for 12 h. The capture holds COM4, so stop `capture_serial.py` before any flash. The user is away. Not polled. **Checked at 23:08 on 2026-11-23: the capture file ends at 22:46:15 with `[serial disconnected; waiting for reconnect]`, about 37,215 s (10.3 h) into the run, and COM4 is no longer present in Windows (only COM6 is).** The last log lines are ordinary sensor packets (flags 0x0F, chamber 52 F) and Heltec beacons. There is no `Stop:` line and no second `AeroSentry Controller Starting` banner, so the log shows neither a firmware stop nor a reboot. Why the Waveshare left USB is NOT known: unplugged or loose cable, loss of power, or a crash that never re-enumerated are all possible and none was checked. Check the unit physically. What the log does show before it ended: 10 hourly `Auto phase 4` sample cycles about 1.01 h apart (3600 s wait plus the 10 s pump and the old 30 s settle), no stop, three `Sensor dropout - holding` events that recovered in 105, 171 and 167 ms, at 37,152 s To-Engine 27.8% and From-Engine 10.7% with the chamber at 52 F. The 30 s grace has not yet run to its limit. The two capture processes (PIDs 9992 and 24636) were still waiting to reconnect at 23:08.

**First system test result:** there is no completed run. Run 1 was ended twice by sensor-dropout stops (my readiness bug, fixed by the grace above), so it says nothing yet about whether the desiccant is dry. Run 2 is the repeat. The only content finding from run 1 is the stale-air first reading described above.

**Open, unexplained:**
- Radio frame loss: about 0.4% in the dryer log against about 0.01% in the run7 recharge log. Cause unknown.
- The warning message vanishing after the screen saver was seen again in run7. The fix (`uiWarningActive()`, `uiLogoHide()`) has been compiled and flashed but never seen working on the display.
- Magenta/yellow flicker on about 1 in 10 page changes. The background is navy 0x0A1628 in every build.
- The bridge `/api/status` keeps reporting a stale `Recharge` mode. Do not trust it for controller state.

**Handoff:** `HANDOFF.md` and `.github/copilot-instructions.md` were written from an audit of the files on disk. No firmware source, header or `platformio.ini` was changed for them. Nothing from the 2026-11-20 to 2026-11-23 work is committed: 16 files are modified and `tools/peek_port.py` and `tools/reset_and_peek.py` are untracked on `main` at `accffcb`.

## 2026-11-24 v1 beta build (nothing flashed)

Built on branch `v1-beta` from `9a518b9`, one commit per step, every step building with no warnings. Firmware
`1.0.0-beta.1`. **Nothing here has run on the hardware.** What to prove, in order, is in
`docs/BENCH_TEST_CHECKLIST.md`. Stop codes are in `docs/ERROR_CODES.md`.

**Auto dryer run 2 result.** The COM4 disconnect at 22:46:15 on 2026-11-23 was the user unplugging the board.
Run 2 ran 10.3 h with no stop and no crash (grace-30 build, without the sample fix). That closes the
"possible USB drop" item.

**Decisions made by the user on 2026-11-24:**
- Vent pressure check fails closed, with its own message. Every error state gets its own message, and the
  future SD logger uses the same codes.
- During a sensor-loss cool down the pump may run only while the vent pressure confirms the air goes outside;
  without pressure, no pump.
- No remote Recharge. A remote user cannot confirm the unit is somewhere safe.
- Watchdog: add it if it cannot cause stoppages from sensor dropouts (it cannot; see below).
- Recharge prompt reworded (safe location, gets hot, expels water from the discharge port; the "disconnect the
  engine lines" text predates the valves). Starting a dryer with Auto Recharge on shows the same warning.

**What changed (see `git log 9a518b9..v1-beta` for the detail):**
- Stop codes S01-S18: specific cause (which sensor, which thermal limit, remote stop), the reading behind it,
  what to do, and `<mode> ended. Code Sxx`. One serial line per stop: `Stop: S09 VALVE_SUSPECT mode=Recharge | ...`.
  The format after `Stop:` changed; the saved analysis scripts only match the prefix. Admin lists the last 3.
- Loop watchdog: the core's 5 s task watchdog did not watch `loop()` (only the core 0 idle task). It does now.
  `setup()` turns the relays off before the 1 s console wait. A restart mid-cycle, or any watchdog / crash /
  brownout reset, shows S18 with the cause at boot (a flag in NVS, written only at cycle start and stop).
  Admin > Relay test > Watchdog test freezes `loop()` on purpose to prove it on the bench. Sensor dropouts are
  handled inside `loop()`, so they cannot trip it.
- Recharge sensor loss: the old failure cool down was re-entered on every pass, so its timer never ran out, and
  the pump stayed in whatever state it had, which could mean on for good. Now: the pump stops at once, a cut
  pulse is re-run (not judged), the failure cool down is entered once and advances on time even if the sensors
  never return, and a failure during the safety check stops at once. Heat-up timeout cools down before
  stopping.
- Vent pressure: required to start a recharge (also an automatic one), a required reading during the recharge
  (S08 after the grace), the first pulse waits for the baseline, and cool down and safety pulses are judged
  too (they were not checked at all). A pulse with no pressure sample is not judged and does not count.
- Readiness: To-Engine, From-Engine and pressure count as ready when a valid sample arrived within 3 s. One bad
  read used to make a sensor "missing" at once; the host simulation showed one bad pressure read in five
  stalled a recharge forever. The chamber keeps the stricter rule. The From-Engine wait at pulse end is gone
  (it only fed the retired trend check).
- Remote: Recharge refused; starts only from the bridge MAC `10:BD:A3:5A:7C:94` (sender of all 37,165 beacons
  in the run 2 log); Stop from anyone; Auto Recharge armed per cycle only for a dryer started at the screen;
  remote commands no longer overwrite settings. Phone page and bridge updated; the bridge now marks status stale
  after 6 s (the stale "Recharge" report) and parses "Dryer Manual".
- Screens: new Recharge prompt with Back; unattended-recharge warning; the two Auto Recharge checkboxes stay in
  sync (each read the setting only once); valves shown on the main screen; long messages use the 20 px font.
- Display: pixel clock 16 -> 14 MHz (`DISPLAY_PCLK_HZ`). The flash colors are byte-swapped frames: navy 0x08A5
  swapped is gold 0xA508, panel blue 0x1969 swapped is magenta 0x6919. The LCD DMA slipped a byte because
  PSRAM (shared with flash) could not keep up. IDF 4.4 has no bounce buffer, so this lowers the demand. The
  full fix is Arduino core 3.x with bounce buffers.
- K1: no software cause found. The driver matches the reference XL9535 driver byte for byte, and no firmware has
  driven K1 since the first raw sweep. Admin > Relay test pulses any P0/P1 pin and reads the pin back. Boot
  prints an I2C bus scan.
- Sensor node sends every packet twice, 30 ms apart; the controller keeps the first copy (same sequence
  number). Packet layout unchanged.
- Auto dryer start now prints `Auto phase 4`. It did not before, so the post-flash check in `HANDOFF.md` would
  have looked like a failure.
- Cleanup: retired trend check, unused members and defines, `RECHARGE_TEST_MODE`, initializer order, named
  colors, stale comments. Tools take ports and paths as arguments. The XIAO node moved to `archive/`.
- Platform pinned to `espressif32@6.11.0`.
- `tools/host_test.py` builds the real controller logic for the PC with simulated sensors, relays, radio and
  a chamber, and runs 27 scenarios (124 checks) with safety invariants checked at every step.

**Not verified:** everything above on the hardware; the 14 MHz panel; the bridge MAC filter with the real
bridge; the watchdog reset timing and relay drop; the XL9535 holding its outputs through a reset; cool down
and safety pulses showing the step on a cold chamber.
