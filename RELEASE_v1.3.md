# ESP WebRTC Solution v1.3 Release Notes

## Overview

ESP WebRTC Solution v1.3 focuses on a major Peer Connection upgrade (`esp_peer` through **v1.5.3**) and a broader set of end-to-end demos. This release strengthens connectivity (IPv6, TCP/TURNS, ICE Lite, renegotiation), adds media extensibility (RTP transformer / RTX), expands chip and IDF support (ESP32-S31, ESP-IDF v6.x), hardens DTLS handshake across mbedTLS versions, and ships several new server/protocol solutions.

## What's New

### 1️⃣ Peer Connection Enhancements

**esp_peer updated to v1.5.3**

**New Features:**
- ✔️ IPv6 support for ICE / connectivity
- ✔️ TCP and TURNS support for the ICE agent
- ✔️ ICE Lite mode (`ice_use_lite_mode`) and improved FreeSWITCH / RTP port parsing compatibility
- ✔️ SDP renegotiation while connected; ECDSA P-256 support
- ✔️ RTP transformer API for custom packet processing; H.264 RTP decoder support
- ✔️ Video RTP RTX support; configurable PLI interval for key-frame requests
- ✔️ Media direction negotiation from SDP (`sendrecv` / `sendonly` / `recvonly`)
- ✔️ Connecting state notification and option to keep ICE role after disconnect
- ✔️ Weak UDP transport fallback; DTLS `close_notify` for graceful teardown
- ✔️ Configurable maximum ICE candidate count and `alive_binding_retries` for keep-alive
- ✔️ DTLS key dump support for Wireshark analysis
- ✔️ ESP32-S31 target support; ESP-IDF v6.0 support (compatible with IDF v5.x)
- ✔️ Refined STUN handling and DTLS pre-generated certificates for faster setup

**Bug Fixes:**
- ✔️ Fixed first-boot DTLS handshake failure on IDF v6 / mbedTLS 4.1
- ✔️ Fixed DTLS handshake failure on older IDF (mbedTLS < v3.6.6)
- ✔️ Fixed DTLS re-setup leakage and SCTP buildup / aggressive resend issues
- ✔️ Fixed data channel creation when remote SDP has no data channel; unreliable DC open (DCEP retransmit)
- ✔️ Fixed TURN failures with long usernames and auto-disconnect after ~10 minutes without permission refresh
- ✔️ Fixed premature ICE nomination when acting as controlled; binding response discard on high RTT
- ✔️ Fixed crash on early SCTP messages, SCTP refcount races, and agent deinit while still in use
- ✔️ Fixed incorrect H.264 profile when controlled by peer; negative KMS priority crash
- ✔️ Fixed ESP32-C5 link / build issues on IDF v5.5.2
- ✔️ Treat peer `actpass` as DTLS client; treat `MBEDTLS_ERR_SSL_CLIENT_RECONNECT` as disconnected

### 2️⃣ Solution Updates

**New Solutions:**
- Added [janus_demo](https://github.com/espressif/esp-webrtc-solution/tree/main/solutions/janus_demo) — Janus VideoRoom publisher (HTTP signaling)
- Added [kvs_master](https://github.com/espressif/esp-webrtc-solution/tree/main/solutions/kvs_master) — Amazon Kinesis Video Streams WebRTC master mode (SigV4 signaling)
- Added [webrtc_usb_camera](https://github.com/espressif/esp-webrtc-solution/tree/main/solutions/webrtc_usb_camera) — WebRTC-to-USB UVC camera bridge
- Added [rtsp_demo](https://github.com/espressif/esp-webrtc-solution/tree/main/solutions/rtsp_demo) — RTSP server / pusher example
- Added [rtmp_demo](https://github.com/espressif/esp-webrtc-solution/tree/main/solutions/rtmp_demo) — RTMP push example

**Improvements:**
- Migrated [openai_demo](https://github.com/espressif/esp-webrtc-solution/tree/main/solutions/openai_demo) Realtime signaling to the GA API; fixed function-call handling for the new version
- Added ESP32-S3 support to [videocall_demo](https://github.com/espressif/esp-webrtc-solution/tree/main/solutions/videocall_demo)
- [doorbell_demo](https://github.com/espressif/esp-webrtc-solution/tree/main/solutions/doorbell_demo): bitrate CLI, ESP32P4-EYE support, ESP32-S31 test path
- [doorbell_local](https://github.com/espressif/esp-webrtc-solution/tree/main/solutions/doorbell_local): fixed pedestrian-detect build; reduced noisy error logging
- [kvs_master](https://github.com/espressif/esp-webrtc-solution/tree/main/solutions/kvs_master): prevent ICE role flip on viewer reconnect
- ESP32-P4 solutions: enable PSRAM mempool for `wifi_remote`
- `esp_webrtc`: bump `esp_capture` to v1.0; fixed capture pre-start then WebRTC start enabling sink too early
- `codec_board`: fixed LCD init clock error on IDF v6.x; added `board_get_lcd_io_handle`
- Adopted `media_lib_utils`; signaling dependencies cleaned up via `idf_component.yml`; solution overview docs refreshed
- Verified solution builds on ESP-IDF v6.x

**🆕 New board / platform supports:**
- ESP32-S31 / `esp32s31_korvo_1` (`codec_board` Kconfig + peer libraries)
- `codec_board`: Kconfig to select default board (`set_default_codec_board`)

## Obtaining v1.3.0

Users can obtain the release code using either of the following methods:

### Method 1: Using Git (Recommended)

```bash
git clone -b v1.3.0 https://github.com/espressif/esp-webrtc-solution.git esp-webrtc-solution-v1.3.0
cd esp-webrtc-solution-v1.3.0/
```

This is the recommended method for obtaining v1.3.0 of ESP WebRTC Solution.

### Method 2: Download Archive

Alternatively, you can download the release archive directly from GitHub:
[esp-webrtc-solution-v1.3.0.zip](https://github.com/espressif/esp-webrtc-solution/archive/refs/tags/v1.3.0.zip)

## Support

For issues and feature requests, please use the [GitHub issue tracker](https://github.com/espressif/esp-webrtc-solution/issues).

## Contributors
We thank all contributors who helped improve this release.
