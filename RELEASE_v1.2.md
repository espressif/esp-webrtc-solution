# ESP WebRTC Solution v1.2 Release Notes

## Overview

ESP WebRTC Solution v1.2 is a significant update that brings enhanced stability, new features, expanded hardware support, and improved developer experience. This release includes major component updates, critical bug fixes.

## What's New

### 1️⃣ Peer Connection Enhancements

**esp_peer updated to v1.2.7**

**New Features:**
- ✔️ Support for multiple data channels
- ✔️ Support for Forward-TSN (Transmission Sequence Number)
- ✔️ Support for ESP32-C5 microcontroller

**Bug Fixes:**
- ✔️ Fixed stability issues under poor network conditions
- ✔️ Fixed potential crash caused by race conditions
- ✔️ Fixed DTLS role mismatch issue
- ✔️ Added msid attribute in SDP for better stream identification
- ✔️ Fixed TURN relay connectivity issues

### 2️⃣ New Capture System – GMF-based [esp_capture](https://github.com/espressif/esp-gmf/tree/main/packages/esp_capture)

The legacy capture system has been replaced with a GMF (Generic Media Framework) implementation, providing significant extensibility improvements:

- Support for multiple capture paths
- Automatic capability negotiation to simplify configuration
- Enhanced pipeline flexibility for multimedia processing

### 3️⃣ Published Components to ESP-IDF Registry

Now available as independent, reusable components:
- [esp_peer](https://components.espressif.com/components/espressif/esp_peer)
- [esp_capture](https://components.espressif.com/components/espressif/esp_capture)
- [av_render](https://components.espressif.com/components/tempotian/av_render)
- [media_lib_sal](https://components.espressif.com/components/tempotian/media_lib_sal)
- [codec_board](https://components.espressif.com/components/tempotian/codec_board)

### 4️⃣ Solution Updates

**New Solution:**
- Added [kms_demo](https://github.com/espressif/esp-webrtc-solution/tree/main/solutions/kms_demo) for Kurento Media Server Publisher

**Improvements:**
- Added AI processing (Pedestrian Detection) to doorbell_local demo
- Fixed OpenAI demo function call build error

### 5️⃣ Other Features and Improvements

- HTTP client refined: supports redirect and OPTIONS requests
- Added SEI injection support via video send hook – thanks to [Todd Sharp](https://github.com/recursivecodes)
- Fixed WHIP signaling issue when ICE server not configured
- Added bitrate setting control for esp_webrtc
- Fixed crash issue when resetting while renderer is created
- Fixed data queue read/write dead-loop issue
- Fixed codec board pin configuration issue

**🆕 New board supports:**
- ESP32P4-EYE
- XIAO ESP32S3 Sense

## Migration Guide — Upgrading to v1.2.0

This release introduces the GMF-based Capture System, which replaces the old esp_capture APIs. To upgrade smoothly, please review the following compatibility changes:

For detailed changes, please refer to commit [0ad48d](https://github.com/espressif/esp-webrtc-solution/commit/0ad48d537082f9c24d4fc228863b6138e3e78417#diff-038ca219eec1eb427e8b0296bdcfbbc0f8af70e89d380763e60f7ce313f9bf70).

Users only need to update `media_sys.c` and replace the relevant APIs or configurations as shown in the table below.

### Capture System API Changes

| Legacy API (Simple Capture)      | New API (GMF Capture)           | Notes   |
|-----------------------------------|---------------------------------|---------|
| esp_capture_audio_codec_src_cfg_t | esp_capture_audio_dev_src_cfg_t | Renamed |
| esp_capture_new_audio_codec_src   | esp_capture_new_audio_dev_src   | Renamed |
| ESP_CAPTURE_CODEC_TYPE_*          | ESP_CAPTURE_FMT_ID_*            | Renamed |
| esp_capture_path_handle_t         | esp_capture_sink_handle_t       | Renamed |
| esp_capture_setup_path            | esp_capture_sink_setup          | Renamed |
| esp_capture_enable_path           | esp_capture_sink_enable         | Renamed |
| esp_capture_acquire_path_frame    | esp_capture_sink_acquire_frame  | Renamed |
| esp_capture_release_path_frame    | esp_capture_sink_release_frame  | Renamed |

## Obtaining v1.2.0

Users can obtain the release code using either of the following methods:

### Method 1: Using Git (Recommended)

```bash
git clone -b v1.2.0 https://github.com/espressif/esp-webrtc-solution.git esp-webrtc-solution-v1.2.0
cd esp-webrtc-solution-v1.2.0/
```

This is the recommended method for obtaining v1.2.0 of ESP WebRTC Solution.

### Method 2: Download Archive

Alternatively, you can download the release archive directly from GitHub:
[esp-webrtc-solution-v1.2.0.zip](https://github.com/espressif/esp-webrtc-solution/archive/refs/tags/v1.2.0.zip)

## Support

For issues and feature requests, please use the [GitHub issue tracker](https://github.com/espressif/esp-webrtc-solution/issues).

## Contributors
We thank all contributors who helped improve this release.
