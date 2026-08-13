#!/usr/bin/env python3
"""
UT for local_jpeg_stream: drive ESP over serial and monitor browser via Puppeteer.

Example:
  python3 ut/test.py -wifi_ssid MyWifi -wifi_psw secret \\
      -duration 30 -send_camera 1 -real_camera 1 -port /dev/ttyUSB0 start

Controls:
  start  Connect WiFi (if needed), launch browser harness, ring after signaling, judge pass/fail
  stop   Send stop to ESP over serial
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import threading
import time
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("pyserial required: pip install pyserial", file=sys.stderr)
    sys.exit(2)

UT_DIR = Path(__file__).resolve().parent
HARNESS = UT_DIR / "harness.mjs"

IP_RE = re.compile(
    r"(?:https://(\d+\.\d+\.\d+\.\d+)/webrtc/test|"
    r"https://(\d+\.\d+\.\d+\.\d+)/|"
    r"got ip:(\d+\.\d+\.\d+\.\d+)|"
    r"sta ip:\s*(\d+\.\d+\.\d+\.\d+))"
)


class SerialMonitor:
    def __init__(self, port: str, baud: int):
        self.port = port
        self.baud = baud
        self.ser = None
        self.lines: list[str] = []
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._thread = None
        self.device_ip = None
        self.peer_connected = False
        self.jpeg_ready = False
        self.dc_opened = False
        self.send_frames = 0

    def open(self):
        # Avoid CP2102 DTR/RTS auto-reset on open (would reboot ESP mid-test)
        self.ser = serial.Serial(
            port=self.port,
            baudrate=self.baud,
            timeout=0.2,
            dsrdtr=False,
            rtscts=False,
        )
        try:
            self.ser.setDTR(False)
            self.ser.setRTS(False)
        except Exception:
            pass
        time.sleep(0.3)
        # Wake console REPL
        self.ser.write(b"\r\n")
        self.ser.flush()
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._thread.start()

    def close(self):
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2)
        if self.ser and self.ser.is_open:
            self.ser.close()

    def _reader(self):
        buf = b""
        while not self._stop.is_set():
            try:
                chunk = self.ser.read(512)
            except Exception:
                break
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode("utf-8", errors="replace").rstrip("\r")
                self._handle_line(text)

    def _set_ip(self, ip: str):
        if ip and ip != "0.0.0.0":
            self.device_ip = ip

    def _handle_line(self, text: str):
        with self._lock:
            self.lines.append(text)
        print(f"[ESP] {text}", flush=True)

        m = IP_RE.search(text)
        if m:
            self._set_ip(next(g for g in m.groups() if g))

        if "JPEG stream ready" in text:
            self.jpeg_ready = True
        if "Peer connected" in text:
            self.peer_connected = True
        if "Video data channel opened" in text or "Created video_data DC" in text:
            self.dc_opened = True
        m = re.search(r"vid_send[^\d]*(\d+)", text, re.I)
        if m:
            self.send_frames = max(self.send_frames, int(m.group(1)))

    def write_line(self, cmd: str):
        # ESP console REPL expects CR/LF
        data = (cmd.rstrip() + "\r\n").encode("utf-8")
        self.ser.write(data)
        self.ser.flush()
        print(f"[HOST->ESP] {cmd}", flush=True)

    def wait_for(self, predicate, timeout: float):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if predicate():
                return True
            time.sleep(0.2)
        return False

    def dump_tail(self, n: int = 40):
        with self._lock:
            return self.lines[-n:]


def find_default_port() -> str | None:
    # Primary UART console on this board is /dev/ttyUSB0 (not USB-JTAG ACM)
    if Path("/dev/ttyUSB0").exists():
        return "/dev/ttyUSB0"
    ports = list(list_ports.comports())
    for p in ports:
        if "ttyUSB" in (p.device or ""):
            return p.device
    for p in ports:
        if "ttyACM" in (p.device or "") or "USB" in (p.device or ""):
            return p.device
    return ports[0].device if ports else None


def run_browser_harness(mon: SerialMonitor, url: str, width: int, height: int,
                        duration_s: float, wait_ms: int | None = None,
                        send_camera: bool = True, real_camera: bool = False,
                        browser_fps: int = 10, jpeg_quality: float = 0.4) -> dict:
    env = os.environ.copy()
    duration_ms = int(max(duration_s, 1) * 1000)
    # Overall browser budget: connect + accept + stream duration
    env["JPEG_WAIT_MS"] = str(wait_ms if wait_ms is not None else duration_ms + 30000)
    env["JPEG_DURATION_MS"] = str(duration_ms)
    env["JPEG_MIN_FRAMES"] = env.get("JPEG_MIN_FRAMES", "3")
    env["JPEG_SEND_VIDEO"] = "1" if send_camera else "0"
    env["JPEG_REAL_CAMERA"] = "1" if real_camera else "0"
    # Prefer real user cache; Cursor sandbox cache often has no Chrome binary
    env.setdefault("PUPPETEER_CACHE_DIR", str(Path.home() / ".cache" / "puppeteer"))
    url_q = url
    if "?" not in url_q:
        url_q += (
            f"?autoconnect=1&w={width}&h={height}&res={width}x{height}"
            f"&fps={browser_fps}&quality={jpeg_quality:g}"
        )
    else:
        if "autoconnect" not in url_q:
            url_q += "&autoconnect=1"
        if "w=" not in url_q:
            url_q += f"&w={width}&h={height}"
        if "res=" not in url_q:
            url_q += f"&res={width}x{height}"
        if "fps=" not in url_q:
            url_q += f"&fps={browser_fps}"
        if "quality=" not in url_q:
            url_q += f"&quality={jpeg_quality:g}"

    cmd = ["node", str(HARNESS), "--url", url_q]
    print(
        f"[HOST] launch browser harness: {' '.join(cmd)} "
        f"(duration={duration_s}s send_camera={int(send_camera)} real_camera={int(real_camera)} "
        f"fps={browser_fps} quality={jpeg_quality:g})",
        flush=True,
    )
    proc = subprocess.Popen(
        cmd,
        cwd=str(UT_DIR),
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )
    result = {"ok": False, "error": "no result"}
    assert proc.stdout is not None
    for line in proc.stdout:
        line = line.rstrip()
        print(f"[BROWSER] {line}", flush=True)
        try:
            obj = json.loads(line)
        except json.JSONDecodeError:
            continue
        # Ring only after browser signaling is up, otherwise RING is lost
        if obj.get("type") == "signaling_ready":
            mon.write_line("cmd ring")
        if obj.get("type") == "result":
            result = obj
    # Allow a little longer than stream duration for teardown
    proc.wait(timeout=max(30, int(duration_s) + 60))
    result["exit_code"] = proc.returncode
    return result


def cmd_stop(mon: SerialMonitor):
    mon.write_line("stop")
    time.sleep(1)
    return 0


def cmd_start(mon: SerialMonitor, args) -> int:
    if args.wifi_ssid:
        psw = args.wifi_psw or ""
        mon.write_line(f"wifi {args.wifi_ssid} {psw}")
    else:
        mon.write_line("start")

    # Must wait for IP itself: "JPEG stream ready" is printed before the URL line
    ok = mon.wait_for(lambda: mon.device_ip is not None, timeout=90)
    if not ok:
        print("[HOST] ERROR: could not parse device IP from serial log", file=sys.stderr)
        print("[HOST] last lines:", file=sys.stderr)
        for l in mon.dump_tail():
            print(f"  {l}", file=sys.stderr)
        return 1

    if not mon.jpeg_ready:
        mon.wait_for(lambda: mon.jpeg_ready, timeout=20)

    page = f"https://{mon.device_ip}/webrtc/test"
    browser = run_browser_harness(
        mon,
        page,
        args.video_width,
        args.video_height,
        args.duration,
        args.wait_ms,
        send_camera=bool(args.send_camera),
        real_camera=bool(args.real_camera),
        browser_fps=args.browser_fps,
        jpeg_quality=args.jpeg_quality,
    )

    mon.wait_for(lambda: mon.peer_connected or mon.dc_opened, timeout=5)

    browser_ok = bool(browser.get("ok"))
    peer_ok = mon.peer_connected or mon.dc_opened or mon.send_frames > 0
    passed = browser_ok and (peer_ok or browser.get("frames", 0) >= 3)

    print("========== RESULT ==========")
    print(f"device_ip     : {mon.device_ip}")
    print(f"duration_s    : {args.duration}")
    print(f"send_camera   : {args.send_camera}")
    print(f"real_camera   : {args.real_camera}")
    print(f"browser_fps  : {args.browser_fps}")
    print(f"jpeg_quality : {args.jpeg_quality:g}")
    print(f"browser_ok    : {browser_ok} frames={browser.get('frames')} ice={browser.get('ice')} dc={browser.get('dc')}")
    send_stats = browser.get("sendStats") or {}
    if send_stats:
        print(
            f"browser_send  : frames={send_stats.get('sentFrames')} bytes={send_stats.get('sentBytes')} "
            f"jpeg={send_stats.get('sizeMin')}-{send_stats.get('sizeMax')}B "
            f"canvasAvg={send_stats.get('canvasAvg')} likelyLive={send_stats.get('likelyLive')}"
        )
    print(f"esp_peer      : connected={mon.peer_connected} dc={mon.dc_opened} send_hint={mon.send_frames}")
    print(f"video_wh      : {args.video_width}x{args.video_height}")
    print(f"PASS/FAIL     : {'PASS' if passed else 'FAIL'}")
    print("============================")
    return 0 if passed else 1


def main():
    parser = argparse.ArgumentParser(description="local_jpeg_stream UT")
    parser.add_argument("-wifi_ssid", default=None)
    parser.add_argument("-wifi_psw", default=None)
    parser.add_argument("-video_width", type=int, default=320)
    parser.add_argument("-video_height", type=int, default=240)
    parser.add_argument("-port", default=None, help="Serial port (default: /dev/ttyUSB0)")
    parser.add_argument("-baud", type=int, default=115200)
    parser.add_argument(
        "-duration",
        type=float,
        default=60,
        help="Stream run duration in seconds after Accept (default: 60)",
    )
    parser.add_argument(
        "-send_camera",
        type=int,
        default=1,
        choices=[0, 1],
        help="Browser send camera JPEG over data channel (1=yes, 0=no; default: 1)",
    )
    parser.add_argument(
        "-real_camera",
        type=int,
        default=0,
        choices=[0, 1],
        help="Use real webcam instead of Chromium fake device (1=yes; still auto, no manual click; default: 0)",
    )
    parser.add_argument(
        "-browser_fps",
        type=int,
        default=10,
        choices=[5, 10, 15, 20, 25, 30],
        help="Browser JPEG send rate (default: 10)",
    )
    parser.add_argument(
        "-jpeg_quality",
        type=float,
        default=0.4,
        choices=[0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9],
        help="Browser JPEG encoder quality, 0.3-0.9 (default: 0.4)",
    )
    parser.add_argument(
        "-wait_ms",
        type=int,
        default=None,
        help="Optional overall browser timeout ms (default: duration+30s)",
    )
    parser.add_argument("action", nargs="?", default="start", choices=["start", "stop"])
    args = parser.parse_args()

    port = args.port or find_default_port()
    if not port:
        print("No serial port found; pass -port /dev/ttyUSB0", file=sys.stderr)
        return 2
    print(f"[HOST] serial port: {port}", flush=True)

    if not HARNESS.exists():
        print(f"Missing {HARNESS}", file=sys.stderr)
        return 2

    if not (UT_DIR / "node_modules" / "puppeteer").exists():
        print("[HOST] npm install in ut/", flush=True)
        subprocess.check_call(["npm", "install"], cwd=str(UT_DIR))

    mon = SerialMonitor(port, args.baud)
    mon.open()
    try:
        time.sleep(1)
        if args.action == "stop":
            return cmd_stop(mon)
        return cmd_start(mon, args)
    finally:
        mon.close()


if __name__ == "__main__":
    sys.exit(main())
