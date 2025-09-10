
# Doorbell Local Demo

## Overview

This demo shows how to use `esp_webrtc` to build a local doorbell application with optional AI-powered pedestrian detection. An ESP32 series board acts as an HTTPS signaling server and provides real-time video streaming, two-way audio communication, and intelligent motion detection capabilities.

## Hardware Requirements

The default hardware setup uses the [ESP32P4-Function-Ev-Board](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html), which includes an SC2336 camera.

## Build Instructions

### ESP-IDF Version

Use either the **IDF master branch** or **release v5.4**.

### Dependencies

This demo depends only on **ESP-IDF**. All other required components will be automatically fetched from the [ESP-IDF Component Registry](https://components.espressif.com/).

### Configuration

1. **Wi-Fi Settings**
   Set your Wi-Fi SSID and password in [`settings.h`](main/settings.h).

2. **Camera Configuration**
   If using a different camera model or resolution, update the corresponding settings in [`settings.h`](main/settings.h).

3. **Pedestrian Detection (Optional)**
   To enable pedestrian detection functionality:
   - Set `CONFIG_DOORBELL_SUPPORT_PEDESTRIAN_DETECT=y` in your configuration
   - The detection resolution is defined by `DETECT_WIDTH` and `DETECT_HEIGHT` in [`settings.h`](main/settings.h)
   - Detection frame rate is controlled by `DETECT_FPS` setting
   - Requires additional memory and processing power

4. **USB-JTAG Download (Optional)**
   If using USB-JTAG, uncomment the following line in [`sdkconfig.defaults.esp32p4`](sdkconfig.defaults.esp32p4):
   ```c
   CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
   ```

### Build and Flash

```bash
idf.py -p YOUR_SERIAL_DEVICE flash monitor
```

## Testing

After the board boots, it will connect to the configured Wi-Fi. To switch to a different Wi-Fi network dynamically, use the CLI:

```bash
wifi YOUR_SSID YOUR_PASSWORD
```

Once connected, the board will start the signaling server and wait for a peer to connect. A test URL will be printed in the console:

```
Webrtc_Test: Use browser to enter https://192.168.10.33/webrtc/test for test
```

You can stop or restart signaling with:

```bash
stop
start
```

### Browser Access

Open **Chrome** or **Edge** and visit the printed URL. Since the site uses a self-signed certificate, you may need to trust the page manually.

Also, disable mDNS ICE candidates to ensure proper WebRTC connectivity:

- **Chrome**: `chrome://flags/#enable-webrtc-hide-local-ips-with-mdns`
- **Edge**: `edge://flags/#enable-webrtc-hide-local-ips-with-mdns`

Disable the option: **WebRTC mDNS ICE candidates**
Restart the browser for the change to take effect.

### Interactions

After opening the page in the browser:

1. **Open Door**
   - Click the **Door** icon.
   - The board will play a "Door is opened" tone.
   - The browser will display: `Receiving Door opened event`.

2. **Calling (Ring Button)**
   - Press the **boot key** on the board.
   - The board plays ring music, and the browser shows **Accept Call** and **Deny Call** icons.
   - If accepted: starts **two-way audio** and **one-way video** (board → browser).
   - If denied: the call ends.
   - To change the ring button, modify `DOOR_BELL_RING_BUTTON` in [`settings.h`](main/settings.h).

3. **Pedestrian Detection** *(if enabled)*
   - The system automatically detects pedestrians using the camera feed.
   - When a pedestrian is detected, a `PEDESTRIAN_DETECTED` event is sent to the browser.
   - The browser displays detection events in a scrollable list with timestamps.
   - Click the **Detection Events** button to view/clear the event history.
   - Events are displayed in chronological order (newest first) with animation effects.

4. **End Call / Cleanup**
   - In the browser: click the **Hangup** icon.
   - On the board: run the `stop` command.

## Technical Details

This demo uses SSE (Server-Sent Events) to send instant messages to the browser and HTTP POST to receive instant messages from the browser.

### Enhancements in `esp_webrtc`

- **`no_auto_reconnect`**: Disables automatic peer connection setup on signaling connect.
- **`esp_webrtc_enable_peer_connection()`**: Explicit API to control peer connection lifecycle.

For a detailed call flow, refer to the [esp_webrtc connection flow](../../components/esp_webrtc/README.md#typical-call-sequence-of-esp_webrtc).

### Pedestrian Detection Implementation

When enabled, the pedestrian detection system:

- **Camera Processing**: Captures frames from the camera at the configured resolution and frame rate
- **AI Detection**: Uses ESP-DL (Deep Learning) library for real-time pedestrian detection
- **Event Notification**: Sends `PEDESTRIAN_DETECTED` custom commands via WebRTC signaling
- **Browser UI**: Displays detection events with timestamps in a scrollable, animated list

The detection system operates independently of video calls and continues running in the background when enabled.

### Limitations

- Only one peer can connect at a time.
- A heartbeat timeout of 5 seconds is enforced.
- If the peer leaves unexpectedly, wait a few seconds before reconnecting.
- Pedestrian detection requires additional CPU and memory resources, which may affect overall system performance.
- Detection accuracy depends on lighting conditions, camera quality, and pedestrian positioning.
