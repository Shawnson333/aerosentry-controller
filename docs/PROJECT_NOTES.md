# AeroSentry Project Notes

## Cycle rules

These setpoints are best guesses and will be tuned after testing. All sensors are logged during every cycle. Only the sensor a mode uses for control can stop that mode.

- **Purge:** pump on until the user stops it. Watch To-Engine only. Above 40% for 30 continuous seconds while pumping stops the cycle: "Desiccant saturated. Recharge cycle recommended."
- **Manual dryer:** pump on for the selected minutes, off for the selected hours, then repeat. Same 40% To-Engine stop. From-Engine is logged but not required.
- **Auto dryer:** the only mode that controls from both sensors. Pump runs until From-Engine is at or below the user target. Then once an hour the pump runs 10 seconds, pauses 30 seconds, and samples. Over target starts drying again; under target waits another hour. The 40% To-Engine stop still applies.
- **Recharge:** 40% stop does not apply. Heaters run until the XIAO chamber thermistor reaches 240F, then the pump pulses 10 seconds on and 1 minute off. Three consecutive To-Engine humidity readings at or below 15% complete the cycle. Otherwise it stops at 3 hours or when the user stops it.
- A missing required sensor blocks Start and, if it drops mid-cycle, stops the cycle with a warning instead of silently returning to Idle.
- Starting Dryer or Auto without a qualified purge in the previous hour asks the user to purge first. A purge counts only after the pump has run for 60 seconds. Purge opens the normal purge prompt. Continue to Dryer opens the normal dryer prompt. The qualified time is saved with the software clock. Time spent powered off is not counted until a battery-backed clock is added.
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

K1 stays unused. K2 is the pump, K3 the heater, K4 the case fan, and K5 AUX stays off. K6-K8 stay reserved. The driver rejects K1 and K6-K8. K2 was confirmed on P0.1. K3-K5 follow that order and still need a click test.

K4 runs while the pump is on, for the entire recharge cycle, and also when the Waveshare chip temperature stays at or above 65C. It turns off at 55C if the pump is off and recharge is not running. That chip reading is the ESP32-S3 die, not the air in the case, so the thresholds are a starting guess.

During recharge the heater holds 240F. It turns off at 240F and back on at 235F so the relay does not chatter. The pump still waits for the first time the chamber reaches 240F, then pulses.

The XL9535 itself needs no firmware. The USB-C port's purpose is unconfirmed until it is connected or the exact board documentation is found. P1 and J3 may be parallel I2C connections, but that is not confirmed for this board.
- The saturation hard-stop applies outside Recharge only.
- Missing sensor data must produce a warning, not a brief mode entry followed by an unexplained return to Idle.

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
- Until the 10k is installed, the XIAO still broadcasts both humidity sensors but never marks the chamber reading valid. Recharge stays blocked. Purge, manual, and auto can be tested from the humidity sensors.

## V1 boundaries

- Local touch-screen operation is the priority.
- One wired XIAO ESP32-C3 will supply both To-Engine and From-Engine readings by ESP-NOW.
- SD logging and clock persistence are deferred until their hardware/time-source decisions are made.
- MeshCore remote control is not required for V1.

## Future unattended recharge (not V1)

A later version may add two solenoid valves. Closing/rerouting the valves would change the dryer from the engine's closed loop to an open circuit vented to atmosphere, allowing Recharge to run without the user disconnecting lines.

Design provision: keep relay mapping expandable and mode-specific hardware actions inside the state machine. Do not assign relay channels 4-8 permanently until the valve sequence and fail-safe positions are defined. A future automatic recharge must include valve-position confirmation, timeout handling, and a safe power-loss state before it is enabled.

## Clock

The current software clock starts from firmware build time and advances with `millis()`. It is editable on screen but resets after power loss. RTC or network time can replace `system_clock.cpp` without changing the UI.