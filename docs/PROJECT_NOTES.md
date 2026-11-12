# AeroSentry Project Notes

## Where we are — 2026-11-11, end of session

Saved locally on `main` at `86a51b5` ("Serve the remote page on the Tailscale address."). Not pushed. `main` is ahead of `origin/main`. Working tree was clean before this note.

Phone path that works today, with the relay board still at home:

- Phone browser, Tailscale connected, opens `http://100.110.107.102:8080/`. History sample is `/history.html`. Do not open the HTML files directly.
- This computer runs `tools/remote_bridge.py`. It must stay awake. It talks USB to the Heltec, which forwards on ESP-NOW channel 1 to the Waveshare.
- Stop was acknowledged: "Already idle. Outputs off." Purge was refused: relay board not connected. Starts stay refused until the XL9535 is back.
- Live humidity can appear when the XIAO is in range. Charts on the history page are sample drawings, not hangar logs.
- Tailscale address can change. If the phone URL fails, run `tailscale ip -4` on this computer. Do not forward port 8080.

Next session, in this order. Do not redo the radio discovery.

1. Reconnect the XL9535 and DS3231M at home, then confirm starts are no longer refused for a missing relay board.
2. MeshCore on the hangar Heltec and a second V4 at home. That replaces the USB cable. Do not flash stock MeshCore ESP-NOW bridge. Do not join the Heltec to Wi-Fi. Stay on channel 1.
3. Move `tools/remote_bridge.py` to a small always-on computer later. A Chromebox is fine. The phone still uses Tailscale, not a public port.

To resume without rereading the whole chat: read this section only, then do the next numbered step. Do not rebuild, reflash, or rescan ports unless that step needs it.

Bench-confirmed and in the saved firmware:

- Waveshare ESP32-S3 4.3-inch Type B, 800x480. Touch, main controls, clock screen, and the navy AeroSentry splash work. The upload port used so far is COM4.
- XL9535 at 0x21, active-high, two data bytes (port 0 then port 1). K1 is unused. K2 pump, K3 heater, and K4 case fan are the confirmed outputs. K5 AUX stays off. K6-K8 stay reserved.
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

## Future unattended recharge (not V1)

A later version may add two solenoid valves. Closing/rerouting the valves would change the dryer from the engine's closed loop to an open circuit vented to atmosphere, allowing Recharge to run without the user disconnecting lines.

Design provision: keep relay mapping expandable and mode-specific hardware actions inside the state machine. Do not assign relay channels 4-8 permanently until the valve sequence and fail-safe positions are defined. A future automatic recharge must include valve-position confirmation, timeout handling, and a safe power-loss state before it is enabled.

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