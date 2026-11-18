# AeroSentry Controller

Hangar-based aircraft-engine dehumidifier controller.

## Current hardware

- **Display/controller:** Waveshare ESP32-S3 4.3-inch Capacitive Touch LCD Development Board **Type B with case**, 800x480, ST7262 RGB LCD and GT911 touch.
- **I2C:** GPIO 8 (SDA), GPIO 9 (SCL). GT911 is detected at 0x5D.
- **Sensor node:** No-display Heltec WiFi LoRa 32 V4.3 on ESP-NOW channel 1. Firmware is `nodes/heltec-sensor`. To-Engine SHT40 uses GPIO 17/18. From-Engine SHT40 uses GPIO 15/16. Both probes answer at 0x44 on separate buses. The 100K NTC 3950 uses GPIO 4 with the external 10k resistor to ground. GPIO 3 reads a 1-ohm shunt in the recharge valve's 12 V return (coil current sense, disabled until the shunt is wired). GPIO 47/48 read the vent pressure module (HX711). Sensor packets are version 3; the Waveshare and sensor node flash together. The XIAO is no longer needed on the bench.
- **Relay board:** 8-channel XL9535 at I2C address 0x21, with 3.3V logic and a separate 5V relay supply. Outputs are active-high. K1 is unused. K2 is the pump, K3 the heater, and K4 the case fan. K5 is the To-Engine valve (dump to atmosphere) and K6 the From-Engine valve (fresh air intake for recharge); both are energized together during recharge. K7-K8 are reserved aux.
- **RTC:** DS3231M at I2C address 0x68, connected to the relay board's second I2C header. It shares GPIO 8/9 and is powered from 3.3V, not the relay coil supply.

## Build and upload

From this directory (Windows):

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run --target upload --upload-port COM4
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools/capture_serial.py --port COM4 --seconds 20 --reset
```

## Implementation status

- Direct ESP-IDF RGB driver and LVGL 8.3.11 work on the Type B display.
- Checkpoint 2026-11-11: phone page works through Tailscale at `http://100.110.107.102:8080/` while `tools/remote_bridge.py` is running. Heltec forwards commands on ESP-NOW channel 1. Starts stay refused until the relay board is reconnected. A no-display V4.3 is proposed to replace the XIAO. Wiring is in `docs/PROJECT_NOTES.md`. That firmware is not written yet.
- Main, warning, Manual Dryer Setup, Auto Cycle, and editable clock screens are implemented.
- Mode start commands require the sensor that mode actually uses. A missing required sensor blocks Start with a sensor-unavailable warning.
- Purge and manual dryer watch To-Engine for the 40% hard-stop. Auto watches both humidity sensors. Recharge holds 240F on the chamber thermistor, then ends on three consecutive To-Engine readings at or below 15%, or at 3 hours. Chamber temperature above 260F, or either humidity-sensor temperature above 250F, stops all outputs. Dryer start warns if no purge has run in the last hour.
- The Waveshare listens for the XIAO packet on ESP-NOW channel 1. Until a packet arrives, the screen stays on Sensors offline. The chamber 10k divider is installed; Recharge stays blocked until that chamber reading is valid.
- The DS3231M now provides battery-backed time at address 0x68. It is read at startup and once a minute. Set Clock adjustments are written immediately, and Done commits the displayed time even if no field changed. A saved software clock remains available as a fallback, but that fallback does not count time spent powered off.

See `docs/PROJECT_NOTES.md` for behavior notes and future expansion.