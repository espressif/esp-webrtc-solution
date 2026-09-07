# RTMP Push Example

This example captures audio/video from the ESP device and pushes it to an RTMP server using `esp_media_protocols`.
The most important setup is in `main/settings.h`.

## What `main.c` does

- Initializes board + media pipeline (`init_board()`, `media_sys_buildup()`).
- Starts console REPL (`esp>` prompt).
- Connects to Wi-Fi via `network_init(WIFI_SSID, WIFI_PASSWORD, ...)`.
- Auto-starts RTMP push after Wi-Fi connects when `RTMP_PUSH_URL` is set at build time.

If `RTMP_PUSH_URL` is empty, the app waits for a console command:

```bash
start rtmp://<server>/<app>/<stream>
```

## 1) Required configuration

### A. Set Wi-Fi credentials

Edit `main/settings.h`:

- `WIFI_SSID`
- `WIFI_PASSWORD`

Without this, `start` command and auto-start will wait for network and do nothing.

### B. Set RTMP URL

Use one of these options:

- Build-time environment variable:

```bash
RTMP_PUSH_URL=rtmp://<server>/<app>/<stream> idf.py build
```

- Runtime console command:

```bash
start rtmp://<server>/<app>/<stream>
```

Keeping `RTMP_PUSH_URL` out of `main/settings.h` avoids committing the server URL to source.

### C. Confirm media settings

Also in `main/settings.h`:

- `TEST_BOARD_NAME`
  - `ESP32_P4_DEV_V14` for `esp32p4`
  - `S3_Korvo_V2` otherwise
- Video:
  - ESP32-P4 default: `1920x1080 @ 25fps`, `H264`
  - Others default: `320x240 @ 10fps`, `MJPEG`
- Audio:
  - `AAC`, `8kHz`, mono

RTMP support depends on the server. H264 + AAC is usually the best choice for public RTMP services.

### D. (ESP32-P4) camera-related defaults

`sdkconfig.defaults.esp32p4` enables SC2336 camera and related options.
If your hardware uses a different sensor, update camera options with `menuconfig`.

## 2) Build and flash

From `solutions/rtmp_demo`:

```bash
. ~/esp/idf_v55/esp-idf/export.sh
idf.py set-target esp32p4
RTMP_PUSH_URL=rtmp://<server>/<app>/<stream> idf.py build
idf.py -p <PORT> flash monitor
```

For non-P4 targets, set the correct target, for example `esp32s3`.

## 3) Run and verify

After boot:

1. Wait for Wi-Fi connection log.
2. If `RTMP_PUSH_URL` was set during build, push starts automatically.
3. Otherwise run `start rtmp://<server>/<app>/<stream>` at the `esp>` prompt.
4. Open the stream from your RTMP server or player.

## 4) Console commands

At `esp>` prompt:

- `start [url]`
  Start RTMP push. If `url` is omitted, `RTMP_PUSH_URL` is used.
- `stop`
  Stop RTMP push.
- `wifi <ssid> [password]`
  Reconfigure and reconnect Wi-Fi, matching the other demos.
- `i`
  Show system loading info.

## How to build

### Build

Board configuration uses [`esp_board_manager`](https://components.espressif.com/components/espressif/esp_board_manager). Install or upgrade the helper first:

```bash
pip install --upgrade esp-bmgr-assist
```

List supported boards:

```bash
idf.py gen-bmgr-config -l
```

Select your board (generates board config and sets the chip target):

```bash
idf.py gen-bmgr-config -b YOUR_BOARD_NAME
# Example:
idf.py gen-bmgr-config -b esp32_p4_function_ev_board
```

Build and flash:

```bash
idf.py -p YOUR_SERIAL_DEVICE flash monitor
```

> Notes: Board manager may overwrite some sdkconfig values defined in `sdkconfig.defaults`.
> Prefer attaching overwritten options into an appended sdkconfig folder when needed.

For how to customize a board with `esp_board_manager`, see [custom-board](https://github.com/espressif/esp-board-manager/tree/main/esp_board_manager#custom-board).

