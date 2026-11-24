# Archive

Retired hardware, kept for reference. Nothing here is built or flashed in normal work.

- `xiao-sensor/`: the Seeed XIAO ESP32-C3 sensor node, replaced by the Heltec V4.3
  sensor node (`nodes/heltec-sensor`). It does not fill the vent pressure fields of
  packet version 3, so the current controller would refuse a recharge with it.
  `xiao-sensor-known-good.bin` is the last confirmed image (MAC `AC:27:6E:FC:EF:A8`).
  The PlatformIO project still builds from here (`pio run -d archive/xiao-sensor`).
