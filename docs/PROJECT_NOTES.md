# AeroSentry Project Notes

## Where we are — 2026-11-11

Local controller work is saved on `main` at `c5023bb` ("Add DS3231M RTC and robust chamber thermistor reading with retry"). That commit was already local. Before this note, `main` was 6 commits ahead of `origin/main` and had not been pushed. The working tree was clean.

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
- Heltec V4 OLED board is in hand and not in the firmware yet. The local link is the Waveshare RS485 port. GPS should be unplugged. Details are in "Next: Heltec V4".

Do not reopen these while adding the radio: the XL9535 is not a PCF8574, K1 does not drive, the XIAO stays on ESP-NOW channel 1, and a remote command must not become a second state machine.

## Next: Heltec V4

The board in hand is the Heltec WiFi LoRa 32 V4 with the 0.96-inch OLED, not the TFT. Listing facts: ESP32-S3R2, 2MB PSRAM, 16MB flash, SX1262, about 27 dBm transmit, USB-C, solar connector, and a GNSS port. It is not wired and not in `platformio.ini`. MeshCore already has an OLED target in [meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore) `variants/heltec_v4/platformio.ini` (`heltec_v4_oled`, SDA 17 / SCL 18). The product page also says Meshtastic. Do not flash Meshtastic. This project stays on MeshCore. Heltec's own flash note still applies: V4.2 selects `USE_GC1109_PA`; V4.3 selects `USE_KCT8103L_PA`. The 27 dBm listing does not say which PA chip is fitted. Read the silkscreen before flashing. LoRa SPI in that MeshCore variant is SCLK 9, MOSI 10, MISO 11, NSS 8, RESET 12, BUSY 13, DIO1 14. Those pins belong to the Heltec, not the Waveshare.

Role, already decided: the Heltec is the LoRa coprocessor. MeshCore carries start, stop, and mode actions, plus warnings, status, and periodic log packets, so the phone page can stay current away from the unit. It does not drive relays. Remote start and stop must call the existing `StateMachine::command()` path so the sensor, saturation, and thermal checks still apply. If the local link is down, ignore remote starts. The touch screen still stops the cycle.

Link, decided 2026-11-11: use the Waveshare RS485 port, not I2C, not USB-C, and not another ESP-NOW radio.

- The Waveshare RS485 demo uses Serial1 on GPIO 43 RX and GPIO 44 TX at 115200. Those pins already go to the onboard RS485 transceiver, so the connector is A/B, not TTL. Put a 3.3V RS485 module on two free Heltec header pins. Connect A to A, B to B, and GND to GND. Do not assume the connector pin order. Do not power that module from the relay coil 5V.
- Do not put the Heltec on the Waveshare I2C bus, and do not buy a splitter or mux to do it. GPIO 8/9 already has the GT911 at 0x5D, the CH422G, the XL9535 at 0x21, and the DS3231M at 0x68. There is no address conflict. The relay board's second I2C header is already a parallel plug. A passive splitter only adds capacitance. A mux is for a later address clash or a flaky extra sensor, not for talking to another computer. A stuck I2C slave can hang touch and the relays. The Heltec is also an I2C master for its own OLED.
- Do not connect the two USB-C ports. The Waveshare USB-C is the native programming port, COM4. On this board, USB D-/D+ can be switched over to CAN, which turns USB off. It is not a host port, and a C-to-C cable between two device ports will not enumerate. Leave both USB-C ports for flashing.
- Do not copy the XIAO pattern. The XIAO has to sit out on the engine lines, so ESP-NOW channel 1 is the right link for sensors. The Heltec sits with the controller. Its useful radio is the SX1262, toward the phone. A second ESP-NOW peer can drop or repeat a start or stop, and it shares the 2.4 GHz radio that the XIAO already uses. Do not enable MeshCore's Heltec ESP-NOW bridge.

OLED stays. It is the radio's own status display: link, last command accepted or rejected. It is not a second operator screen.

GPS comes off. This unit does not move. A map pin can be a typed-in hangar coordinate later. Time already comes from the DS3231M, and a hangar roof can hide the sky. Unplug the GNSS module and leave its pins unused. MeshCore's V4 GPS pins are RX 38, TX 39, EN 34, and RESET 42.

Still open before controller code:

- V4.2 versus V4.3, from the silkscreen.
- Which two Heltec header pins feed the RS485 module. Do not use LoRa, OLED, USB, PA, or the GNSS pins. GPIO 5 is a PA control on V4.3.1.
- Packet contents and how often status and logs go out.

Leave these alone:

- Do not move the XIAO off channel 1.
- Do not start the web page or the SD logger in the same step. Identify the board, add the RS485 module, and prove the wire first.

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
- MeshCore on the Heltec WiFi LoRa 32 V4 is the remote-control path, not only a monitor. The board in hand has the OLED. GPS is not needed and should be unplugged. The local link is the Waveshare RS485 port, not I2C, USB-C, or ESP-NOW. See "Next: Heltec V4" above.
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