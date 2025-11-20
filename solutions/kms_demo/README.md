# Kurento Media Server (KMS) Publish Client Demo

## Overview

This demo demonstrates how to use `esp_webrtc` to publish video/audio streams to Kurento Media Server (KMS). ESP32 acts as a WebRTC publisher, and browsers can connect to view the stream through KMS.

### Architecture

```
┌─────────────┐         WebRTC          ┌─────────────┐         WebRTC         ┌─────────────┐
│   ESP32     │ ───────────────────────>│     KMS     │<───────────────────────│   Browser   │
│  (Publisher)│                         │   (Server)  │                        │ (Subscriber)│
└─────────────┘                         └─────────────┘                        └─────────────┘
     │                                          │                                       │
     └─────────── WebSocket Signaling ──────────┴─────────── WebSocket Signaling ───────┘
```

**Flow:**
1. ESP32 connects to KMS via WebSocket and creates a media pipeline
2. ESP32 creates two WebRTC endpoints:
   - Endpoint1: ESP32's own endpoint for publishing
   - Endpoint2: Browser-facing endpoint for subscribers
3. ESP32 connects endpoint1 → endpoint2 for media routing
4. ESP32 logs the browser endpoint ID
5. Browser uses the endpoint ID to connect and receive the stream

## Hardware Requirements

- **ESP32P4-Function-Ev-Board** (recommended)
  - Includes SC2336 camera
  - Supports up to 1920x1080@25fps
  - [Documentation](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/index.html)

- **ESP32-S3-Korvo-V2** (also supported)
  - Supports up to 320x240@10fps

## Prerequisites

- **ESP-IDF**: v5.4 or v5.5
- **Kurento Media Server**: v7.2.0 or later
- **Modern Browser**: Chrome, Firefox, Edge (with WebRTC support)
- **Network**: ESP32 and browser must be able to reach KMS server

## Kurento Server Setup

### Installation

For detailed installation instructions, refer to the [official Kurento documentation](https://doc-kurento.readthedocs.io/en/latest/user/installation.html).

### Quick Start with Docker

The easiest way to run KMS is using Docker:

```bash
docker run --rm \
     --network host \
     -p 8888:8888/tcp \
     -p 5000-5050:5000-5050/udp \
     -e KMS_MIN_PORT=5000 \
     -e KMS_MAX_PORT=5050 \
     kurento/kurento-media-server:7.2.0
```

**Important Notes:**
- Use `--network host` for direct network access
- Ensure port 8888 (WebSocket) is accessible
- UDP ports 5000-5050 are for RTP media (adjust as needed)
- For remote access, ensure KMS binds to `0.0.0.0`, not `127.0.0.1`

### Verify KMS is Running

Check if KMS is accessible:
```bash
curl http://YOUR_SERVER_IP:8888/kurento
```

You should see a JSON response with KMS version information.

## Configuration

### 1. Wi-Fi Settings

Edit [main/settings.h](main/settings.h):

```c
#define WIFI_SSID "YourWiFiSSID"
#define WIFI_PASSWORD "YourWiFiPassword"
```

### 2. Kurento Server URL

Set the KMS WebSocket URL in [main/settings.h](main/settings.h):

```c
#define KURENTO_SERVER "ws://YOUR_SERVER_IP:8888/kurento"
```

**Examples:**
- Local server: `ws://192.168.1.100:8888/kurento`
- Remote server: `ws://example.com:8888/kurento`
- Secure (WSS): `wss://example.com:8888/kurento`

### 3. Video Settings

Video resolution and frame rate are configured in [main/settings.h](main/settings.h):

**ESP32P4:**
```c
#define VIDEO_WIDTH  1920
#define VIDEO_HEIGHT 1080
#define VIDEO_FPS    25
```

**ESP32-S3:**
```c
#define VIDEO_WIDTH  320
#define VIDEO_HEIGHT 240
#define VIDEO_FPS    10
```

### 4. Board Configuration

The board type is automatically detected based on the target chip. To support other boards, see the [codec_board README](../../components/codec_board/README.md).

## Building and Flashing

### Build

```bash
cd solutions/kms_demo
idf.py build
```

### Flash and Monitor

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Replace `/dev/ttyUSB0` with your serial port:
- Linux: `/dev/ttyUSB0` or `/dev/ttyACM0`
- macOS: `/dev/cu.usbserial-*` or `/dev/cu.usbmodem*`
- Windows: `COM3` or similar

### Monitor Only

```bash
idf.py -p /dev/ttyUSB0 monitor
```

Press `Ctrl+]` to exit monitor.

## Usage

### Automatic Streaming

After booting, ESP32 will:
1. Connect to the configured Wi-Fi network
2. Connect to KMS server
3. Create media pipeline and endpoints
4. Start publishing video/audio stream
5. Log the browser endpoint ID

### CLI Commands

You can control streaming via serial console:

| Command | Description |
|---------|-------------|
| `start [server_url]` | Start streaming to KMS server (optional URL override) |
| `stop` | Stop streaming and disconnect from KMS |
| `i` | Display system information (memory, uptime, etc.) |
| `wifi <ssid> <password>` | Connect to a new Wi-Fi network |

**Example:**
```
esp32> start ws://192.168.1.100:8888/kurento
esp32> stop
esp32> wifi MyNetwork MyPassword
```

### Getting the Browser Endpoint ID

When ESP32 successfully publishes to KMS, it will log the browser endpoint ID:

```
I (417491) KWS_SIG: ========================================
I (417492) KWS_SIG: BROWSER_ENDPOINT_ID: 6e22da0f-d509-40bd-ae42-00a3c8b28244_kurento.MediaPipeline/0aad6cb2-75d9-411e-9658-0d7ab5420db8_kurento.WebRtcEndpoint
I (417493) KWS_SIG: ========================================
```

**Copy this endpoint ID** - you'll need it for the browser client.

## Browser Client

### Opening the Client

Open [main/kurento_client.html](main/kurento_client.html) in a modern browser.

### Configuration

1. **Kurento WebSocket URL**: Enter your KMS server URL
   - Example: `ws://192.168.1.100:8888/kurento`
   - Must match the server ESP32 is connected to

2. **ESP32 Browser Endpoint ID**: Paste the endpoint ID from ESP32 logs
   - Format: `xxx_kurento.MediaPipeline/yyy_kurento.WebRtcEndpoint`
   - This is the endpoint2 ID, NOT endpoint1

3. **Receive Only**: Check this box (browser only receives, doesn't send)

4. **STUN Server**: Optional, defaults to Google's STUN server

### Connecting

1. Click **Connect** button
2. Wait for WebRTC connection to establish
3. Video should appear in the "Remote Video (ESP32)" section

### Troubleshooting Browser Connection

**Issue: "Endpoint not found"**
- Verify the endpoint ID is correct (copy from ESP32 logs)
- Ensure ESP32 is still connected to KMS
- Check that endpoint ID hasn't expired (ESP32 recreates it when browser disconnects)

**Issue: "Endpoint already negotiated"**
- ESP32 automatically recreates the endpoint when browser disconnects
- Wait a moment and get the new endpoint ID from ESP32 logs
- Reconnect using the new generated endpoint ID

**Issue: No video displayed**
- Check browser console for errors
- Verify WebRTC connection state shows "connected"
- Check network connectivity between browser and KMS server


## Technical Details

### Signaling Implementation

This demo uses a custom Kurento WebSocket signaling implementation (`esp_signaling_get_kms_impl`). The signaling flow:

1. **WebSocket Connection**: ESP32 connects to KMS WebSocket endpoint
2. **Pipeline Creation**: Creates a MediaPipeline in KMS
3. **Endpoint Creation**: Creates two WebRtcEndpoints:
   - Endpoint1: For ESP32's own WebRTC connection
   - Endpoint2: For browser connections
4. **Media Routing**: Connects endpoint1 → endpoint2
5. **SDP Exchange**: Exchanges SDP offer/answer with KMS
6. **ICE Candidates**: Exchanges ICE candidates for NAT traversal
7. **Media Streaming**: Starts streaming video/audio

### Connection Flow

For a complete explanation of the WebRTC connection process, refer to the [Connection Build Flow](../../components/esp_webrtc/README.md#typical-call-sequence-of-esp_webrtc).


## Limitations

- **Session Isolation**: Kurento objects are scoped to WebSocket sessions. ESP32 and browser must use different sessions, which is why endpoint2 is needed.
- **Single Browser**: Each browser connection requires a new endpoint2. ESP32 automatically recreates endpoint2 when browser disconnects.
- **Network Requirements**: Requires STUN server for NAT traversal. TURN server needed for restrictive networks.
- **Demo Purpose**: The test code is for demonstration only; deploy customized signaling to use more advanced features.

## Additional Resources

- [Kurento Documentation](https://doc-kurento.readthedocs.io/)
- [WebRTC Specification](https://www.w3.org/TR/webrtc/)
- [ESP32 WebRTC Component](../../components/esp_webrtc/README.md)

## License

This example code is in the Public Domain (CC0 licensed).

