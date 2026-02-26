# Janus VideoRoom Publisher Demo

## Overview

This demo shows how to use `esp_webrtc` as a Janus VideoRoom **publisher** over Janus HTTP signaling.

## Janus setup

- Enable Janus HTTP transport plugin (default port `8088`).
- Enable `janus.plugin.videoroom` and create a room.
- Use the room id in `main/settings.h` (`JANUS_ROOM_ID`).

Example API endpoint:

```text
http://<janus-ip>:8088/janus
```

## Configuration

Edit [`main/settings.h`](main/settings.h):

- `WIFI_SSID`, `WIFI_PASSWORD`
- `JANUS_SERVER`
- `JANUS_ROOM_ID`
- Optional `JANUS_PIN`, `JANUS_TOKEN`, `JANUS_API_SECRET`, `JANUS_DISPLAY`

## Usage

After startup, console command:

- `start` : publish with default settings.
- `start <janus_url> <room_id>` : publish with runtime URL/room.
- `stop` : stop publishing.
