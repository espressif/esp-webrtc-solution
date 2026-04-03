# esp_webrtc_solution

## Introduction

This repository provides everything needed to build a WebRTC application.
It includes the `esp_webrtc` core code along with its dependent components, such as:

- **`esp_peer`**: WebRTC PeerConnection realization
- **`esp_capture`**: For capturing media data (see [esp_capture](https://components.espressif.com/components/espressif/esp_capture/) on the component registry)
- **`av_render`**: For playing media data

Additionally, the repository contains demo applications under `solutions/` that show how to use `esp_webrtc` and related media stacks. Each solution has its own README with hardware requirements and build instructions.


## Solutions

### Learning and APIs

| Folder | Description |
|--------|-------------|
| [`peer_demo`](solutions/peer_demo/) | Minimal example of building a WebRTC app from scratch using the `esp_peer` API. |

### Cloud, streaming, and SFU integration

| Folder | Description |
|--------|-------------|
| [`openai_demo`](solutions/openai_demo/) | Real-time WebRTC session to an OpenAI Realtime server with a custom signaling path. |
| [`whip_demo`](solutions/whip_demo/) | Publishes AV to a server using WHIP (`esp_webrtc`). |
| [`kvs_master`](solutions/kvs_master/) | Amazon Kinesis Video Streams (**MASTER**): receives SDP offers from viewers and answers over KVS signaling. |
| [`kms_demo`](solutions/kms_demo/) | Publisher to **Kurento** Media Server; includes a browser viewer for the stream. |
| [`janus_demo`](solutions/janus_demo/) | **Janus** VideoRoom publisher over Janus HTTP signaling. |

### Product-style demos

| Folder | Description |
|--------|-------------|
| [`doorbell_demo`](solutions/doorbell_demo/) | Doorbell with AppRTC-style WebSocket signaling: remote control, live video, two-way audio. |
| [`doorbell_local`](solutions/doorbell_local/) | Local doorbell without an external signaling server (ESP as signaling); includes real-time pedestrian detection. |
| [`videocall_demo`](solutions/videocall_demo/) | Video-call style app using `esp_webrtc` **data channel**. |

### Bridges and RTSP

| Folder | Description |
|--------|-------------|
| [`webrtc_usb_camera`](solutions/webrtc_usb_camera/) | WebRTC-to-**USB UVC** bridge: browser sends media over WebRTC; host sees the device as a USB webcam (video path; see demo README). |
| [`rtsp_demo`](solutions/rtsp_demo/) | **RTSP** server or pusher on the device for LAN streaming (complements WebRTC/media examples). |
