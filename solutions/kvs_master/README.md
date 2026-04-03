# KVS Master Demo

## Overview
This demo demonstrates how to use `esp_webrtc` with Amazon Kinesis Video Streams (KVS) signaling to build a WebRTC application. The ESP32 acts as the KVS **MASTER**, receiving SDP offers from VIEWERs (browsers) and sending answers.

## Hardware requirement
The default setup uses the [ESP32P4-Function-Ev-Board](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html), which includes one SC2336 camera.

## How to build

### IDF version
Requires IDF v5.4 or later.

### Dependencies
This demo only depends on the **ESP-IDF**. All other required modules will be automatically downloaded from the [ESP-IDF Component Registry](https://components.espressif.com/).

### KVS Configuration

Configure AWS KVS credentials and channel settings via `idf.py menuconfig` under **KVS Signaling Configuration**:

- **AWS Access Key ID** - Your AWS access key
- **AWS Secret Access Key** - Your AWS secret key
- **AWS Session Token** - Session token (for temporary credentials)
- **AWS Region** - AWS region (default: `us-east-1`)
- **KVS Channel Name** - Signaling channel name (default: `demo-channel`)

### Change Default Settings
1. Modify the Wi-Fi SSID and password in [settings.h](main/settings.h)
2. If you are using a different camera type or resolution, update the settings in [settings.h](main/settings.h)
3. If you are using USB-JTAG to download, uncomment the following configuration in [sdkconfig.defaults.esp32p4](sdkconfig.defaults.esp32p4)
```
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
```

### Build
```
idf.py -p YOUR_SERIAL_DEVICE flash monitor
```

## Testing

After boot, the board connects to the configured Wi-Fi and automatically joins the KVS channel set in menuconfig.

If you want to connect to a different Wi-Fi STA at runtime:
```
wifi ssid psw
```

### CLI Commands

| Command | Description |
|---------|-------------|
| `join` | Join the KVS channel (as configured in menuconfig) |
| `leave` | Leave the current channel |
| `wifi ssid psw` | Connect to a different Wi-Fi network |
| `i` | Show system status |
| `rec2play` | Test capture and playback |
| `m` | Measure system loading |

### QA
- If the board unexpectedly leaves the channel, the server keeps the channel for 1-2 minutes before timing out. Wait for the timeout to expire before retrying.
  Or `leave` first then `join` again.

## Technical Details

- The ESP32 acts as the **MASTER** in KVS terminology
- The MASTER receives SDP offers from VIEWERs (browsers) and automatically generates answers
- The peer connection is created automatically when signaling connects and ICE info is received
- Supports bidirectional audio and video

For more details on the connection flow, refer to the [Connection Build Flow](../../components/esp_webrtc/README.md#typical-call-sequence-of-esp_webrtc).
