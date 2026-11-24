"""Pulse DTR/RTS to reset an ESP32-S3, then print the boot log."""
import sys
import time

import serial

port = sys.argv[1]
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 8
ser = serial.Serial()
ser.port = port
ser.baudrate = 115200
ser.timeout = 0.2
ser.dtr = False
ser.rts = False
ser.open()
ser.rts = True
time.sleep(0.05)
ser.dtr = True
time.sleep(0.05)
ser.dtr = False
ser.rts = False
end = time.time() + seconds
while time.time() < end:
    raw = ser.readline()
    if raw:
        sys.stdout.write(raw.decode("utf-8", "replace"))
        sys.stdout.flush()
ser.close()
