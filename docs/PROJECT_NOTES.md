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
- The relay board is a PCF8574 at 0x21, not an XL9535. A normal single-byte write is the vendor protocol: 1 = coil off, 0 = coil on. Their per-channel sample maps K1 to bit 7 with `channel = 8 - channel`, then sends the inverted byte.

## Relay bench notes, 2026-11-08

Do not repeat the boot pulse. The board is being left with a single-byte `0xFF` so every coil stays off.

The only pattern that actually energized relays was the first XL9535-style driver, before the vendor files were read:

1. Probe `0x21` with an empty transmission. It ACKed.
2. `writeReg(0x06, 0x00)` sent two bytes, `0x06` then `0x00`.
3. `applyState()` with active-low inversion sent two bytes, `0x02` then `0xFF`.

K2 through K8 clicked and their red LEDs stayed on. K1 did not, confirmed with a meter. A later single-byte `0xFF` turned those seven off and the user confirmed every LED was out. After that, single-byte `0x00` and a bit walk (`0xFE`, `0xFD`, ... `0x7F`) produced no clicks and no LEDs, even though serial showed the writes ACKed and `0x00` read back as `0x00`. A `0xFF` write read back as `0x26`, which is normal for a PCF8574 weak pull-up loaded by the relay inputs. It does not mean those coils were on.

Next session: reproduce the two-byte `0x06, 0x00` then `0x02, 0xFF` sequence once, on purpose, and watch which LEDs change before trying another theory. Do not leave that sequence in boot code.

## Relay reproduction, 2026-11-09

The original two-byte sequence reproduced exactly: K2-K8 stayed solid on and K1 stayed off. A following single-byte `0xFF` was ACKed but did not clear them this time. Its readback was again `0x26`. That disproves the idea that one `0xFF` always releases the board after this sequence. The boot pulse has been removed. `begin()` now sends `0xFF` twice, 50ms apart, and nothing else turns a coil on. Next diagnostic is an I2C-bus timing/electrical check and the vendor's own exact one-byte sequence, not more XL9535 register writes.

## Relay schematic finding

The supplied schematic shows a PCF8574 driving AO3401 P-channel MOSFETs. A low gate energizes the relay. To release it, the gate must rise close to the relay's 5V supply. With dual-power jumpers and 3.3V logic, the PCF8574 cannot pull the gate high enough. The 5V relay source minus a 3.3V gate still leaves about 1.7V across the MOSFET, so K2-K8 can remain on despite a correct `0xFF` write. This is a hardware level problem, not something another I2C byte pattern can fix.

Keep the relay 5V supply removed until this is corrected. Do not connect 5V I2C pull-ups directly to the Waveshare. The safe options are a bidirectional I2C level shifter with the PCF8574 powered at 5V and common grounds, or replacing the board with a relay driver explicitly designed for 3.3V logic. After that is fixed, retest K1 separately.
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