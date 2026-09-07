# VideoCall Demo

## OverView
This demo showcases how to use `esp_webrtc` to build a device to device video call application. The code uses a modified version of the [apprtc](https://github.com/webrtc/apprtc) as signaling server and a customized command channel.


## Hardware requirement
The default setup uses the [ESP32P4-Function-Ev-Board](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html), which includes a SC2336 camera. For a video call, two `ESP32P4-Function-Ev-Board` devices are required, one as the caller and the other as the callee.

## How to build

### IDF version
Can select IDF master or IDF release v5.4.

### Change Default Settings
1. Modify the Wi-Fi SSID and password in the file in [settings.h](main/settings.h)
2. If you are using a different camera type or resolution, update the settings for the camera type and resolution in [settings.h](main/settings.h)
3. If you are using USB-JTAG to download, uncomment the following configuration in [sdkconfig.defaults.esp32p4](sdkconfig.defaults.esp32p4)
```
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
```

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


## Testing

After the board boots up, it will attempt to connect to the configured Wi-Fi SSID. If you want to connect to a different Wi-Fi STA, you can use the following CLI command:
```
wifi ssid psw
```

After a successful Wi-Fi connection, use the join command to join a random room. Both boards must enter the same room:
```
join mytestroom
```

### Interactions

1. **Ring**
   - One board clicks the boot button or enters the `b`` command in the console to ring the peer board.
   - The peer board receives the `Ring` command and plays ring music.

2. **Accept:**
   - The peer board presses the boot button or enters the `b` command to accept the call.
   - The call will automatically reset after a timeout if not accepted.
  
3. **Hang Off:**
   - The peer board presses the boot button or enters the `b` command to end the ongoing call.

4. **Clear-up Test:**
   - On either board, enter the `leave` command to exit the room.

## Technical Details
  To support the video call functionality, this demo uses [apprtc](https://github.com/webrtc/apprtc) and requires separate signaling from the peer connection build logic. The peer connection is only established when a special signaling message is received from the peer.

### Key Changes in `esp_webrtc`:
- **`no_auto_reconnect` Configuration**: This configuration disables the automatic building of the peer connection when the signaling connection is established.
- **`esp_webrtc_enable_peer_connection` API**: A new API is introduced to manually control the connection and disconnection of the peer connection.
- **Video over Data Channel**: To enable video over the data channel, use the following configuration:
```
   .enable_data_channel = true,
   .video_over_data_channel = true,
```
All other steps follow the typical call flow of `esp_webrtc`. For more details on the standard connection build flow, refer to the [Connection Build Flow](../../components/esp_webrtc/README.md#typical-call-sequence-of-esp_webrtc).
