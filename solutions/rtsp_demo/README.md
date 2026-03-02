# RTSP Server Pusher Example

This example starts an RTSP server or pusher on the ESP device after Wi-Fi connects.
The most important setup is in `main/settings.h`.

## What `main.c` does

- Initializes board + media pipeline (`init_board()`, `media_sys_buildup()`).
- Starts console REPL (`esp>` prompt).
- Connects to Wi-Fi via `network_init(WIFI_SSID, WIFI_PASSWORD, ...)`.
- Auto-starts RTSP server when network is up:
  - Mode: `RTSP_SERVER` (`0`), `RTSP_PUSHER` (`1`)
  - URI path: `rtsp://127.0.0.1:8554/live` (path is used; server listens on device IP)
  - Port: `8554` (set in `main/rtsp.c`)

So on a PC in the same LAN, the stream URL is typically:

`rtsp://<ESP_DEVICE_IP>:8554/live`

---

## 1) Required configuration

### A. Set Wi-Fi credentials

Edit `main/settings.h`:

- `WIFI_SSID`
- `WIFI_PASSWORD`

Without this, `start` command and auto-start will wait for network and do nothing.

### B. Confirm board and media settings

Also in `main/settings.h`:

- `TEST_BOARD_NAME`
  - `ESP32_P4_DEV_V14` for `esp32p4`
  - `S3_Korvo_V2` otherwise
- Video:
  - ESP32-P4 default: `1920x1080 @ 25fps`, `H264`
  - Others default: `320x240 @ 10fps`, `MJPEG`
- Audio:
  - `AAC`, `16kHz`, mono

If your camera/board differs, update these values first.

### C. (ESP32-P4) camera-related defaults

`sdkconfig.defaults.esp32p4` enables SC2336 camera and related options.
If your hardware uses a different sensor, update camera options with `menuconfig`.

### D. Console transport (optional but useful)

The app uses ESP-IDF console REPL. If console input is not working, verify one of:

- `CONFIG_ESP_CONSOLE_UART`
- `CONFIG_ESP_CONSOLE_USB_CDC`
- `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`

---

## 2) Build and flash

From `solutions/rtsp_server`:

```bash
idf.py set-target esp32p4
idf.py build
idf.py -p <PORT> flash monitor
```

For non-P4 targets, set the correct target (for example `esp32s3`).

---

## 3) Run and verify

After boot:

1. Wait for Wi-Fi connection log.
2. RTSP server starts automatically on connect.
3. Get device IP from logs/router.
4. Open stream on host:

```bash
ffplay rtsp://<ESP_DEVICE_IP>:8554/live
```

---

## 4) Console commands

At `esp>` prompt:

- `start <mode> [uri]`
  - Modes:
    - `0` = RTSP server
    - `1` = RTSP push client
  - Example (server mode):
    - `start 0 rtsp://127.0.0.1:8554/live`
- `stop`
  Stop RTSP service.
- `i`
  Show system loading info.

Note: for normal server usage, auto-start on Wi-Fi connect is already enabled in `main.c`.

---

