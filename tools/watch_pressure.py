"""Read the Waveshare USB log without pulsing DTR/RTS, so the ESP32-S3 does not reset."""
import sys
import time

import serial

PORT = "COM4"
PATH = r"C:\Users\sjohn\AppData\Local\Temp\aerosentry-pressure-watch.log"

ser = serial.Serial()
ser.port = PORT
ser.baudrate = 115200
ser.timeout = 0.3
ser.dtr = False
ser.rts = False
ser.open()
ser.dtr = False
ser.rts = False

with open(PATH, "a", encoding="utf-8", buffering=1) as out:
    out.write("\n--- listen %s dtr=%s rts=%s ---\n" % (
        time.strftime("%H:%M:%S"), ser.dtr, ser.rts))
    while True:
        raw = ser.readline()
        if not raw:
            continue
        text = raw.decode("utf-8", "replace").rstrip("\r\n")
        if not text:
            continue
        out.write("%s %s\n" % (time.strftime("%H:%M:%S"), text))
        out.flush()
