"""Local stand-in for the future home computer.

Serves the remote page and talks to the Heltec over USB. The Heltec forwards
commands to the Waveshare on ESP-NOW. Bind is localhost only.
"""
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

import serial
from serial.tools import list_ports

ROOT = Path(__file__).resolve().parents[1] / "web"
PORT = 8080

status = {
    "connected": False,
    "radio": False,
    "mode": "Waiting for radio",
    "running": False,
    "relays": False,
    "toValid": False,
    "fromValid": False,
    "chamberValid": False,
    "toRh": None,
    "fromRh": None,
    "chamberF": None,
    "chipC": None,
    "pump": False,
    "heat": False,
    "fan": False,
    "lastReply": "No command has been sent.",
    "accepted": None,
}
status_lock = threading.Lock()
port_lock = threading.Lock()
link = None
next_seq = 1


def parse_number(text):
    if text is None or text == "-" or text == "":
        return None
    try:
        return float(text)
    except ValueError:
        return None


def apply_status(fields):
    with status_lock:
        status["connected"] = True
        status["radio"] = True
        status["mode"] = fields.get("mode", "Unknown")
        status["running"] = fields.get("running") == "1"
        status["relays"] = fields.get("relays") == "1"
        status["toValid"] = fields.get("toValid") == "1"
        status["fromValid"] = fields.get("fromValid") == "1"
        status["chamberValid"] = fields.get("chamberValid") == "1"
        status["toRh"] = parse_number(fields.get("toRh", "-"))
        status["fromRh"] = parse_number(fields.get("fromRh", "-"))
        status["chamberF"] = parse_number(fields.get("chamberF", "-"))
        status["chipC"] = parse_number(fields.get("chipC", "-"))
        status["pump"] = fields.get("pump") == "1"
        status["heat"] = fields.get("heat") == "1"
        status["fan"] = fields.get("fan") == "1"


def apply_ack(fields):
    with status_lock:
        status["connected"] = True
        status["accepted"] = fields.get("accepted") == "1"
        status["ackSeq"] = int(fields.get("seq", "0"))
        status["lastReply"] = fields.get("text", "No reply text.")


def parse_line(line):
    if line.startswith("STATUS "):
        fields = {}
        for part in line[7:].split():
            if "=" in part:
                key, value = part.split("=", 1)
                fields[key] = value
        if "text" not in fields:
            apply_status(fields)
    elif line.startswith("ACK "):
        fields = {}
        text_at = line.find(" text=")
        head = line[4:text_at] if text_at >= 0 else line[4:]
        for part in head.split():
            if "=" in part:
                key, value = part.split("=", 1)
                fields[key] = value
        fields["text"] = line[text_at + 6:] if text_at >= 0 else ""
        apply_ack(fields)


def reader():
    while True:
        with port_lock:
            current = link
        if current is None:
            time.sleep(0.2)
            continue
        try:
            raw = current.readline()
        except serial.SerialException:
            time.sleep(0.5)
            continue
        if not raw:
            continue
        try:
            parse_line(raw.decode("utf-8", errors="replace").strip())
        except (ValueError, KeyError):
            continue


def find_heltec():
    for item in list_ports.comports():
        try:
            probe = serial.Serial(item.device, 115200, timeout=0.2)
        except serial.SerialException:
            continue
        deadline = time.monotonic() + 2.5
        seen = ""
        while time.monotonic() < deadline:
            chunk = probe.read(probe.in_waiting or 1)
            if chunk:
                seen += chunk.decode("utf-8", errors="replace")
                if "sent beacon" in seen or "Heltec V4.3" in seen:
                    probe.timeout = 0.3
                    print(f"Heltec link on {item.device}")
                    return probe
        probe.close()
    return None


def send_command(name, on_min, off_hr, target):
    global next_seq
    with port_lock:
        if link is None:
            return {"ok": False, "accepted": False, "text": "Heltec serial port is not open."}
        seq = next_seq
        next_seq += 1
        link.write(f"CMD {seq} {name} {on_min} {off_hr} {target}\n".encode("ascii"))
    deadline = time.monotonic() + 3.0
    while time.monotonic() < deadline:
        with status_lock:
            if status.get("ackSeq") == seq:
                return {"ok": True, "accepted": status["accepted"], "text": status["lastReply"]}
        time.sleep(0.05)
    return {"ok": False, "accepted": False, "text": "No acknowledgement from the Waveshare."}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        return

    def send_json(self, payload, code=200):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urlparse(self.path).path
        if path == "/api/status":
            with status_lock:
                self.send_json(dict(status))
            return
        if path == "/":
            path = "/index.html"
        file_path = (ROOT / path.lstrip("/")).resolve()
        if not str(file_path).startswith(str(ROOT)) or not file_path.is_file():
            self.send_error(404)
            return
        kind = "text/html" if file_path.suffix == ".html" else "text/plain"
        body = file_path.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        if urlparse(self.path).path != "/api/command":
            self.send_error(404)
            return
        length = int(self.headers.get("Content-Length", "0"))
        try:
            payload = json.loads(self.rfile.read(length) or b"{}")
        except json.JSONDecodeError:
            self.send_json({"ok": False, "text": "Bad request."}, 400)
            return
        names = {
            "purge": "PURGE",
            "manual": "MANUAL",
            "auto": "AUTO",
            "recharge": "RECHARGE",
            "stop": "STOP",
        }
        name = names.get(payload.get("command"))
        if name is None:
            self.send_json({"ok": False, "accepted": False, "text": "Unknown command."}, 400)
            return
        result = send_command(
            name,
            int(payload.get("dryerOnMinutes", 3)),
            int(payload.get("dryerOffHours", 1)),
            int(payload.get("dryerTargetRH", 20)),
        )
        self.send_json(result)


def tailscale_ip():
    exe = Path(r"C:\Program Files\Tailscale\tailscale.exe")
    if not exe.is_file():
        return None
    import subprocess
    try:
        result = subprocess.run([str(exe), "ip", "-4"], capture_output=True, text=True, timeout=5)
    except (OSError, subprocess.TimeoutExpired):
        return None
    address = result.stdout.strip().splitlines()
    if result.returncode != 0 or not address or not address[0].startswith("100."):
        return None
    return address[0]


def serve(host):
    try:
        server = ThreadingHTTPServer((host, PORT), Handler)
    except OSError as error:
        print(f"Could not listen on {host}:{PORT}: {error}")
        return
    print(f"Open http://{host}:{PORT}/")
    server.serve_forever()


def main():
    global link
    print("Looking for the Heltec beacon...")
    link = find_heltec()
    if link is None:
        print("Heltec not found. The page will still open, but commands cannot be sent.")
    threading.Thread(target=reader, daemon=True).start()
    private = tailscale_ip()
    if private:
        threading.Thread(target=serve, args=("127.0.0.1",), daemon=True).start()
        print("Phone URL uses the Tailscale address only. Do not forward this port on the router.")
        serve(private)
    else:
        print("Tailscale is not logged in. Listening on this computer only.")
        serve("127.0.0.1")


if __name__ == "__main__":
    main()

