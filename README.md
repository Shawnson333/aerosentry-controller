# AeroSentry Controller

Hangar-based aircraft-engine dehumidifier controller.

## Current hardware

- **Display/controller:** Waveshare ESP32-S3 4.3-inch Capacitive Touch LCD Development Board **Type B with case**, 800x480, ST7262 RGB LCD and GT911 touch.
- **I2C:** GPIO 8 (SDA), GPIO 9 (SCL). GT911 is detected at 0x5D.
- **Sensor node (planned):** one wired XIAO ESP32-C3, two humidity sensors, ESP-NOW.
- **Relay board (later):** 8-channel XL9535 board. Driver is disabled until its DIP address is confirmed because the Waveshare CH422G also responds in the 0x20-0x27 range.

## Build and upload

From this directory (Windows):

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run --target upload --upload-port COM4
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools/capture_serial.py --port COM4 --seconds 20 --reset
```

## Implementation status

- Direct ESP-IDF RGB driver and LVGL 8.3.11 work on the Type B display.
- Main, warning, Manual Dryer Setup, Auto Cycle, and editable clock screens are implemented.
- Mode start commands require the sensor that mode actually uses. Until the XIAO is connected, Start displays a sensor-unavailable warning.
- Purge and manual dryer watch To-Engine for the 40% hard-stop. Auto watches both humidity sensors. Recharge heats to 240F on the XIAO chamber thermistor, then ends on three consecutive To-Engine readings at or below 15%, or at 3 hours.
- Relay writes and ESP-NOW remain disabled. SD logging is still a stub, but every running cycle already calls the logger.
- The software clock starts at firmware build time and is not retained across power loss.

See `docs/PROJECT_NOTES.md` for behavior notes and future expansion.