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

