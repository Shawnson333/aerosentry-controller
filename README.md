# AeroSentry Controller

Hangar-based aircraft-engine dehumidifier controller.

## Current hardware

- **Display/controller:** Waveshare ESP32-S3 4.3-inch Capacitive Touch LCD Development Board **Type B with case**, 800x480, ST7262 RGB LCD and GT911 touch.
- **I2C:** GPIO 8 (SDA), GPIO 9 (SCL). GT911 is detected at 0x5D.
- **Sensor node:** Seeed XIAO ESP32-C3 on ESP-NOW channel 1. Firmware is in `nodes/xiao-sensor`. Two SHT31-D sensors share D8/D9. The 100K NTC 3950 uses D1 with an external 10k resistor to ground.
- **Relay board:** 8-channel XL9535 at I2C address 0x21, with 3.3V logic and a separate 5V relay supply. Outputs are active-high. Both XL9535 ports are initialized off. K1's port mapping is still being verified.

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
- The Waveshare listens for the XIAO packet. Until a packet arrives, the screen stays on Sensors offline. A live radio with no sensors wired shows "XIAO online, sensors not wired."
- The software clock starts at firmware build time and is not retained across power loss.

See `docs/PROJECT_NOTES.md` for behavior notes and future expansion.