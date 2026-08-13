# Local JPEG Stream

This demo streams camera JPEG from an ESP board to a browser over WebRTC, using a local HTTPS signaling server on the device. No external signaling server is required.

## Features

- **Local HTTPS signaling** — the ESP board hosts the WebRTC test page and signaling endpoints
- **Video as MJPEG over data channel** — camera JPEG frames are sent over an SCTP data channel (chunked for large frames)
- **Two-way audio** — G.711A over RTP
- **Optional two-way video** — browser can send JPEG back to the ESP (LCD preview) when enabled
- **Call-style UX** — ring / accept / hangup between board and browser

## Hardware

Typical boards:

| Target | Board | Default camera |
|--------|--------|----------------|
| ESP32-S3 | `S3_Korvo_V2` | OV2640 / OV3660 (DVP) |
| ESP32-S31 | `ESP32_S31_KORVO_1` | OV3660 (DVP) |
| ESP32-P4 | `ESP32_P4_DEV_V14` | SC2336 (MIPI) |

For other boards, see the [codec_board README](../../components/codec_board/README.md).

## Quick start

```bash
cd solutions/local_jpeg_stream
idf.py set-target esp32s3   # or esp32s31 / esp32p4
idf.py build flash monitor
```

1. Set Wi-Fi in [`main/settings.h`](main/settings.h) (`WIFI_SSID` / `WIFI_PASSWORD`), or use the console after boot:
   ```text
   wifi <ssid> <password>
   ```
2. After Wi-Fi connects, the console prints a URL similar to:
   ```text
   Use browser to enter https://<device-ip>/webrtc/test for JPEG stream test
   ```
3. Open that URL in **Chrome** or **Edge**, accept the self-signed certificate warning, then follow [Browser usage](#browser-usage).

## Configuration

### 1. Wi-Fi and stream mode

Edit [`main/settings.h`](main/settings.h):

| Setting | Description |
|---------|-------------|
| `WIFI_SSID` / `WIFI_PASSWORD` | Default Wi-Fi credentials |
| `VIDEO_WIDTH` / `VIDEO_HEIGHT` / `VIDEO_FPS` | Capture / stream size and frame rate (keep these aligned with the camera format below) |
| `VIDEO_SEND_RECV` | `true`: ESP sends and receives JPEG; `false`: ESP sends only (browser → ESP video off) |
| `TEST_BOARD_NAME` | Board name for `codec_board` (auto-selected by target in most cases) |
| `JPEG_STREAM_RING_BUTTON` | GPIO used as the ring / accept button |

Defaults by target (can be changed):

- ESP32-S3: `1280x720 @ 12fps`
- ESP32-S31: `640x480 @ 20fps`
- ESP32-P4: `640x480 @ 20fps`

### 2. Camera — prefer hardware JPEG (recommended)

This solution streams **MJPEG**. Prefer a camera format that outputs **JPEG from the sensor** so the ESP does not need to software-encode every frame.

Open menuconfig:

```bash
idf.py menuconfig
```

Go to:

```text
Espressif Camera Sensors Configurations
  → Camera Sensor Configuration
    → Select and Set Camera Sensor
      → <your sensor, e.g. OV2640 / OV3660>
```

Then:

1. **Enable the sensor** (e.g. `OV2640`, `OV3660`).
2. Under **Choose supported formats for DVP interface**, enable at least one **JPEG** entry, for example:
   - OV2640: `DVP … 1280x720 12fps, JPEG` or `640x480 25fps, JPEG`
   - OV3660: `DVP … 640x480 25fps, JPEG` or `1280x720 12fps, JPEG`
3. Under **Select default output format for DVP interface**, choose that **same JPEG** format as the default.

Avoid selecting YUV / RGB / RAW as the default when your goal is JPEG streaming — those formats force extra CPU work (color convert / software JPEG encode).

4. Align [`main/settings.h`](main/settings.h) with the chosen format:

| Camera default (menuconfig) | Set in `settings.h` |
|-----------------------------|---------------------|
| JPEG `1280x720 @ 12fps` | `VIDEO_WIDTH 1280`, `VIDEO_HEIGHT 720`, `VIDEO_FPS 12` |
| JPEG `640x480 @ 25fps` | `VIDEO_WIDTH 640`, `VIDEO_HEIGHT 480`, `VIDEO_FPS 25` (or a slightly lower FPS if Wi-Fi is limited) |
| JPEG `320x240 @ 25/50fps` | `VIDEO_WIDTH 320`, `VIDEO_HEIGHT 240`, matching FPS |

Target defaults already enable HW JPEG for common Korvo setups:

- **ESP32-S3** (`sdkconfig.defaults.esp32s3`): OV2640 / OV3660 JPEG `1280x720 @ 12fps`
- **ESP32-S31** (`sdkconfig.defaults.esp32s31`): OV3660 JPEG `640x480 @ 25fps`

After changing camera options, rebuild:

```bash
idf.py build flash monitor
```

### 3. Browser ICE tip (if connect fails)

Disable WebRTC mDNS ICE candidates, then restart the browser:

- Chrome: `chrome://flags/#enable-webrtc-hide-local-ips-with-mdns`
- Edge: `edge://flags/#enable-webrtc-hide-local-ips-with-mdns`

Set **WebRTC mDNS ICE candidates** to **Disabled**.

## Browser usage

Open `https://<device-ip>/webrtc/test`.

### Call flow

1. Click **Connect Signaling**.
2. On the board, press the ring button, or run `cmd ring` in the serial console.
   You can also click **Ring ESP** from the page.
3. When the Accept button blinks, click **Accept Call**.
4. Remote JPEG from the ESP camera appears in **Remote (DataChannel JPEG)**.
5. Click **Hangup** to end the call.

### Optional browser → ESP video / audio

Before accepting the call:

| Control | Meaning |
|---------|---------|
| **Send Video to ESP** | Encode the browser camera as JPEG and send it over the data channel (needs `VIDEO_SEND_RECV` true on device) |
| **Send Audio** | Send browser microphone audio (RTP G.711A) |
| **Set resolution** | Browser capture / encode size (`320x240` … `1280x720`) |
| **Set FPS** | Browser JPEG send rate |
| **JPEG quality** | Browser JPEG quality (`30%` … `90%`) |

URL query parameters are also supported (useful for bookmarks):

```text
https://<device-ip>/webrtc/test?res=640x480&fps=10&quality=0.4
```

| Parameter | Example | Description |
|-----------|---------|-------------|
| `res` | `640x480` | Send resolution |
| `w` / `h` | `640` / `480` | Same as `res`, used by automation |
| `fps` | `10` | Send FPS |
| `quality` | `0.4` | JPEG quality (`0.3`–`0.9`) |
| `autoconnect` | `1` | Auto-click Connect Signaling |

Lower browser send resolution / FPS / quality if the link is unstable or CPU load is high on the ESP.

## Console commands

At the `esp>` prompt:

| Command | Description |
|---------|-------------|
| `wifi <ssid> <password>` | Connect to Wi-Fi (auto-starts signaling on success) |
| `start` | Start HTTPS signaling / WebRTC |
| `stop` | Stop signaling / WebRTC |
| `cmd ring` | Ring the browser |
| `cmd accept` | Accept an incoming ring from the browser |
| `cmd deny` | Deny an incoming ring |
| `i` | Show memory and WebRTC status |
| `rec2play` | Local camera → LCD loopback test |

## Limitations

- Only one browser peer at a time.
- The device uses a self-signed HTTPS certificate; the browser will warn once until you proceed.
- If the peer disconnects unexpectedly, wait a few seconds before reconnecting (`stop` then `start` if needed).
- For best performance, keep camera output as **hardware JPEG** and match `VIDEO_*` in `settings.h` to that format.
