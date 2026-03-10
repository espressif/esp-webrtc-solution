# WHIP Publish Client Demo

## Overview

This demo shows how to use `esp_webrtc` as a WHIP publish client to stream media to a WHIP server.

## Hardware Requirements

- An [ESP32P4-Function-Ev-Board](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32p4-function-ev-board/user_guide.html) (includes a SC2336 camera).

## WHIP Server Setup

For testing, you can use [mediaMTX](https://github.com/bluenviron/mediamtx) as your WHIP server.

### Configuring mediaMTX

1. **User Authentication**
   In your `mediamtx.yml` file, add user accounts:
   ```yaml
   authInternalUsers:
     - user: username
       pass: password
   ```

2. **STUN/Relay Server**
   Add a STUN (or relay) server:
   ```yaml
   webrtcICEServers2:
     - url: stun:stun_server:3478
   ```

## How to Build

### IDF Version

You may use either the IDF master branch or the IDF release v5.4.

### Configuration Steps

1. **Wi-Fi Settings**
   Update the Wi-Fi SSID and password in [main/settings.h](main/settings.h).

2. **WHIP Server Settings**
   Modify the WHIP server URL and access token in [main/settings.h](main/settings.h).

3. **ICE Lite Support**
   Some WHIP servers may only support ICE Lite mode. Ensure to enable ICE Lite in [main/settings.h](main/settings.h) if required.

4. **Support for Other Boards**
   For instructions on supporting other boards, see the [codec_board README](../../components/codec_board/README.md).

### Building and Flashing

Build, flash, and monitor your device with:
```bash
idf.py -p YOUR_SERIAL_DEVICE flash monitor
```

## Testing

After booting, the board will:
1. Connect to the configured Wi-Fi network.
2. Automatically push the media stream to the WHIP server upon successful connection.

You can also control streaming via CLI commands:
- `start server_url token` : Begin streaming to the WHIP server.
- `stop` : Stop streaming.
- `i` : Display system information.
- `wifi` : Connect to a new Wi-Fi network by specifying the SSID and password.

### Playing the Published Stream

To view the stream, use `ffplay` with:
```bash
ffplay rtsp://username:password@whip_server_ip:rtsp_port/push_stream_name
```
## Technical Details

This demo uses a custom signaling implementation `esp_signaling_get_whip_impl`, to exchange SDP with the WHIP server. The detail process is as follows:
- The client sends an initial SDP to the WHIP server.
- If the response includes a STUN server URL, the client will call `esp_peer_update_ice_info` then sends a PATCH with new candidates to notify the server.
- When signaling closed it will send `DELETE` to delete the session

For a complete explanation of the connection process, refer to the [Connection Build Flow](../../components/esp_webrtc/README.md#typical-call-sequence-of-esp_webrtc).
