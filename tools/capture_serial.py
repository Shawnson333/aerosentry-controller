"""Capture serial output across native-USB resets, then release the port."""
import argparse
import time
import serial

parser = argparse.ArgumentParser()
parser.add_argument("--port", default="COM4")
parser.add_argument("--seconds", type=float, default=12.0)
parser.add_argument("--reset", action="store_true")
args = parser.parse_args()

print(f"Watching {args.port} at 115200 baud for {args.seconds:.0f} seconds...")
deadline = time.monotonic() + args.seconds
port = None
reset_pending = args.reset
try:
    while time.monotonic() < deadline:
        if port is None:
            try:
                port = serial.Serial(args.port, 115200, timeout=0.2)
                print("\n[serial connected]")
                if reset_pending:
                    reset_pending = False
                    port.dtr = False
                    port.rts = True
                    time.sleep(0.1)
                    port.rts = False
                    print("[reset pulsed]")
            except (serial.SerialException, OSError):
                time.sleep(0.2)
                continue
        try:
            data = port.read(port.in_waiting or 1)
            if data:
                print(data.decode("utf-8", errors="replace"), end="", flush=True)
        except (serial.SerialException, OSError):
            print("\n[serial disconnected; waiting for reconnect]")
            port.close()
            port = None
            time.sleep(0.2)
finally:
    if port is not None:
        port.close()
print("\nSerial port released.")