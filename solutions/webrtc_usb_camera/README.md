# WebRTC USB Camera Bridge Demo

## Overview

This example shows how to turn an ESP32 device into a **WebRTC-to-USB UVC bridge**:

- A browser captures webcam and microphone media.
- The browser sends media to ESP32 through WebRTC (AppRTC signaling).
- ESP32 receives the stream via `esp_peer`.
- ESP32 forwards video frames to its USB UVC device interface.
- A host PC sees ESP32 as a standard USB camera.

In short, this demo converts a WebRTC video stream into a plug-and-play USB webcam feed.

> Current status: only **video** is forwarded to UVC output.
> Audio is negotiated in WebRTC but currently ignored.

---

## System Architecture

```text
+--------------------+
|  Browser (Sender)  |
|--------------------|
| Webcam + Mic       |
| WebRTC Sender      |
+---------+----------+
          |
          | WebRTC (SRTP)
          v
+--------------------+
|       ESP32        |
|--------------------|
| esp_peer (WebRTC)  |
| Video Frame Queue  |
| UVC Device Driver  |
+---------+----------+
          |
          | USB UVC
          v
+--------------------+
|   Host Computer    |
|--------------------|
| Camera Application |
| (OBS / VLC / etc.) |
+--------------------+
```

---

## Requirements

### Hardware

Supported targets:

- ESP32-S3
- ESP32-P4

Required capabilities:

- Wi-Fi connectivity
- USB device mode support

### Software / Services

- ESP-IDF build environment
- AppRTC-compatible signaling server:
  - <https://webrtc.espressif.com>

The signaling server is used for SDP and ICE exchange, then peer connection setup.

---

## Build and Flash

### 1) Configure Wi-Fi

Edit `main/settings.h`:

```c
#define WIFI_SSID "your_ssid"
#define WIFI_PASS "your_password"
```

### 2) Build + flash + monitor

```bash
idf.py -p YOUR_SERIAL_PORT flash monitor
```

After boot, the console shows system logs and WebRTC status.

---

## Run the Demo

### Step 1: Start ESP32 receiver

In the ESP console:

```bash
start <room_id>
```

This command connects to signaling, joins the room, and waits for the browser peer.

To stop:

```bash
stop
```

### Step 2: Start browser sender

Open `main/webrtc_usb_sender.html` and set:

- **Server**: `https://webrtc.espressif.com`
- **Room ID**: same value used with `start <room_id>`

Click **Start**. The browser captures local media and sends it to ESP32.

### Step 3: Open the USB camera on host

Connect ESP32 USB device port to the host PC.
After `uvc_device_init()` completes, a new UVC camera should appear in the OS.

You can test with:

- OBS Studio
- VLC
- Zoom
- System camera app

---

## Local AppRTC Proxy (CORS/Origin Workaround)

`webrtc.espressif.com` may reject direct requests from local HTML pages due to origin checks.
For local development, use the included proxy at `main/apprtc_proxy.js`.

### Start proxy

```bash
cd solutions/webrtc_usb_camera/main
npm install ws
node apprtc_proxy.js --port 8080
```

### Sender page configuration

Set **Server** to:

```text
http://<proxy-ip>:8080
```

The proxy rewrites AppRTC HTTP endpoints and relays WebSocket signaling.

---

## Frame Flow and Buffers

```text
WebRTC Decoder
      |
      v
Video Frame Buffer Pool
      |
      v
UVC Device Driver
      |
      v
USB Host Camera Client
```

Frame callback behavior:

| Callback | Description |
| --- | --- |
| `fb_get_cb` | UVC driver requests the next video frame |
| `fb_return_cb` | UVC driver releases the previous frame |

A small reusable frame pool is used to reduce allocation overhead.

---

## Example Use Cases

1. **Remote Camera Bridge**
   Remote camera -> WebRTC -> ESP32 -> USB webcam on PC.

2. **Browser Camera Virtualization**
   Browser (camera/screen source) appears as a USB camera to desktop apps.

3. **Wireless Webcam Adapter**
   Convert Wi-Fi stream to USB webcam without custom host drivers.

4. **Cloud Camera Gateway**
   Forward remote streams across networks and present locally as UVC camera.
