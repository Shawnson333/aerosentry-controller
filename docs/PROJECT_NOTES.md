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
- Relay writes go only to XL9535 address 0x21. Do not scan or write the rest of 0x20-0x27, because the Waveshare CH422G shares that range. K1 is the pump, K2 is the heater, and the coils are active-low.
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