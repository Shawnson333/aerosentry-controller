"""Read a few seconds from a USB serial port without pulsing DTR/RTS."""
import sys
import time

import serial

port = sys.argv[1]
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 5
ser = serial.Serial()
ser.port = port
ser.baudrate = 115200
ser.timeout = 0.3
ser.dtr = False
ser.rts = False
ser.open()
ser.dtr = False
ser.rts = False
end = time.time() + seconds
while time.time() < end:
    raw = ser.readline()
    if raw:
        sys.stdout.write(raw.decode("utf-8", "replace"))
        sys.stdout.flush()
ser.close()
