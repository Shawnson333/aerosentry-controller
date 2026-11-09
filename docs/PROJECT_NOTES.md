# AeroSentry Project Notes

## Cycle rules

These setpoints are best guesses and will be tuned after testing. All sensors are logged during every cycle. Only the sensor a mode uses for control can stop that mode.

- **Purge:** pump on until the user stops it. Watch To-Engine only. Above 40% for 30 continuous seconds while pumping stops the cycle: "Desiccant saturated. Recharge cycle recommended."
- **Manual dryer:** pump on for the selected minutes, off for the selected hours, then repeat. Same 40% To-Engine stop. From-Engine is logged but not required.
- **Auto dryer:** the only mode that controls from both sensors. Pump runs until From-Engine is at or below the user target. Then once an hour the pump runs 10 seconds, pauses 30 seconds, and samples. Over target starts drying again; under target waits another hour. The 40% To-Engine stop still applies.
- **Recharge:** 40% stop does not apply. Heaters run until the XIAO chamber thermistor reaches 240F, then the pump pulses 10 seconds on and 1 minute off. Three consecutive To-Engine humidity readings at or below 15% complete the cycle. Otherwise it stops at 3 hours or when the user stops it.
- A missing required sensor blocks Start and, if it drops mid-cycle, stops the cycle with a warning instead of silently returning to Idle.

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

K1's location is not confirmed. The working theory is that K2-K8 are on port 0 while K1 is on port 1 or otherwise mapped differently. Test that only after the all-off write is confirmed on the board. No relay test runs at boot.

The XL9535 itself needs no firmware. The USB-C port's purpose is unconfirmed until it is connected or the exact board documentation is found. P1 and J3 may be parallel I2C connections, but that is not confirmed for this board.
- The saturation hard-stop applies outside Recharge only.
- Missing sensor data must produce a warning, not a brief mode entry followed by an unexplained return to Idle.

## XIAO wiring

No extra I2C pull-ups. The SHT31 modules already have them. Power both modules from the XIAO 3V3 pin, not 5V, so their pull-ups cannot put 5V on the XIAO pins.

- D8 SDA, D9 SCL, shared by both SHT31 boards.
- To-Engine ADDR tied to GND (address 0x44).
- From-Engine ADDR tied to 3V3 (address 0x45). Do not leave ADDR floating.
- Chamber thermistor: one lead to 3V3, the other lead to D1. A separate 10k resistor goes from D1 to GND. A 100nF capacitor from D1 to GND is optional but helps on the 1m cable.
- The thermistor needs that external resistor. The XIAO's internal pull-up is not accurate enough and must stay off.

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