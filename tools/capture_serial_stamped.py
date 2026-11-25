"""Like capture_serial.py, but stamps every line with wall-clock time (HH:MM:SS.mmm) and writes a file.

Use it to line a serial log up against a phone video of the screen.
Usage: python tools/capture_serial_stamped.py --port COM4 --seconds 120 --reset --out log.txt [--delay 10]
"""
import argparse
import time
from datetime import datetime
import serial

parser = argparse.ArgumentParser()
parser.add_argument("--port", default="COM4")
parser.add_argument("--seconds", type=float, default=60.0)
parser.add_argument("--reset", action="store_true")
parser.add_argument("--delay", type=float, default=0.0, help="wait this long first (start your recording)")
parser.add_argument("--out", required=True)
args = parser.parse_args()


def stamp():
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]


out = open(args.out, "w", encoding="utf-8")


def emit(text):
    out.write(text + "\n")
    out.flush()


if args.delay > 0:
    time.sleep(args.delay)
emit(f"{stamp()} [capture start]")
deadline = time.monotonic() + args.seconds
port = None
reset_pending = args.reset
buffer = b""
while time.monotonic() < deadline:
    if port is None:
        try:
            # Open with DTR and RTS low so opening the port can never reset the board.
            port = serial.Serial()
            port.port = args.port
            port.baudrate = 115200
            port.timeout = 0.2
            port.dtr = False
            port.rts = False
            port.open()
            emit(f"{stamp()} [serial connected]")
            if reset_pending:
                reset_pending = False
                port.dtr = False
                port.rts = True
                time.sleep(0.1)
                port.rts = False
                emit(f"{stamp()} [reset pulsed]")
        except (serial.SerialException, OSError):
            time.sleep(0.2)
            continue
    try:
        data = port.read(port.in_waiting or 1)
    except (serial.SerialException, OSError):
        emit(f"{stamp()} [serial disconnected; waiting for reconnect]")
        port.close()
        port = None
        time.sleep(0.2)
        continue
    if not data:
        continue
    buffer += data
    while b"\n" in buffer:
        line, buffer = buffer.split(b"\n", 1)
        emit(f"{stamp()} {line.decode('utf-8', errors='replace').rstrip()}")
if port is not None:
    port.close()
emit(f"{stamp()} [capture end]")
out.close()
