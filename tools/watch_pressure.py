"""Log the Waveshare USB console to a file without resetting the board.

Opens the port with DTR/RTS held low so the ESP32-S3 is not reset, writes
every line with a timestamp, and reconnects when the native-USB port drops
and comes back (it re-enumerates on every reset). Runs until Ctrl+C.
A running copy holds the port: stop it before flashing.

Usage:
    python tools/watch_pressure.py --port COM4
    python tools/watch_pressure.py --port COM4 --log C:/temp/aerosentry-watch.log
"""
import argparse
import os
import tempfile
import time

import serial


def open_port(name):
    port = serial.Serial()
    port.port = name
    port.baudrate = 115200
    port.timeout = 0.3
    port.dtr = False
    port.rts = False
    port.open()
    port.dtr = False
    port.rts = False
    return port


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", required=True, help="serial port of the Waveshare, e.g. COM4")
    parser.add_argument("--log", default=os.path.join(tempfile.gettempdir(), "aerosentry-watch.log"),
                        help="file to append to (default: %(default)s)")
    args = parser.parse_args()
    print(f"Logging {args.port} to {args.log}. Ctrl+C to stop.")

    port = None
    with open(args.log, "a", encoding="utf-8", buffering=1) as out:
        out.write("\n--- listen %s on %s ---\n" % (time.strftime("%Y-%m-%d %H:%M:%S"), args.port))
        try:
            while True:
                if port is None:
                    try:
                        port = open_port(args.port)
                        out.write("%s [serial connected]\n" % time.strftime("%H:%M:%S"))
                    except (serial.SerialException, OSError):
                        time.sleep(0.5)
                        continue
                try:
                    raw = port.readline()
                except (serial.SerialException, OSError):
                    out.write("%s [serial disconnected; waiting for reconnect]\n" % time.strftime("%H:%M:%S"))
                    try:
                        port.close()
                    except (serial.SerialException, OSError):
                        pass
                    port = None
                    continue
                text = raw.decode("utf-8", "replace").rstrip("\r\n")
                if text:
                    out.write("%s %s\n" % (time.strftime("%H:%M:%S"), text))
        except KeyboardInterrupt:
            pass
        finally:
            if port is not None:
                port.close()


if __name__ == "__main__":
    main()
