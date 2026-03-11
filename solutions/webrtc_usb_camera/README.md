# WebRTC USB Camera Demo

## Overview

This example shows how to build a **WebRTC-to-USB UVC bridge** with ESP32:

- A PC browser page captures webcam + microphone.
- The browser sends media over WebRTC using **AppRTC signaling**.
- ESP32 receives the media stream with `esp_peer`.
- The ESP32 forwards received **video frames** to a USB UVC device API, so a host PC can open it directly as a camera.

> Current implementation forwards **video only** to UVC output. Incoming audio is negotiated and ignored.

## Hardware / Software Notes

- Target boards: ESP32-S3 / ESP32-P4 (must support both Wi-Fi and USB device mode).
- Signaling server: `https://webrtc.espressif.com` (AppRTC-compatible).
- USB host side should see an extra UVC camera after `uvc_device_init()`.

## Build and Flash

1. Update Wi-Fi credentials in `main/settings.h`.
2. Build and flash:

```bash
idf.py -p YOUR_SERIAL_PORT flash monitor
```

## Run

1. On ESP console:
   - `start <room_id>`: join AppRTC room and start WebRTC receive.
   - `stop`: stop signaling/peer connection.
2. On PC browser:
   - Open `main/webrtc_usb_sender.html`.
   - Set the same room ID and click **Start**.
3. On USB host (connected to ESP USB device port):
   - Open the new UVC camera from any camera app (e.g. VLC, OBS, system camera app).

## AppRTC Origin/CORS Local Test Proxy

`webrtc.espressif.com` signaling does origin checks. For local validation before deploying sender HTML under the official namespace, use `main/apprtc_proxy.js`.

### Start proxy

```bash
cd solutions/webrtc_usb_camera/main
npm i ws
node apprtc_proxy.js --port 8080
```

### Use proxy from sender page

- Open sender HTML from a browser.
- Set **server** to `http://<proxy-ip>:8080`.
- Use same `room_id` on ESP (`start <room_id>`).

The proxy rewrites AppRTC URLs from `webrtc.espressif.com` to itself and relays the WebSocket signaling channel.

## Design Notes

- WebRTC peer is configured as `video_dir = RECV_ONLY` and `audio_dir = RECV_ONLY`.
- Received video frames are copied into a small pool and exposed via:
  - `fb_get_cb` (UVC host asks next frame)
  - `fb_return_cb` (UVC host releases frame)
- The demo maps WebRTC codec to UVC format:
  - H264 -> `UVC_FORMAT_H264`
  - MJPEG -> `UVC_FORMAT_JPEG`
