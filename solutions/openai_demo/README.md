# OpenAI Real-Time Chat Demo

## Overview

This demo showcases the use of `esp_webrtc` to establish a real-time chat connection with OpenAI. It also demonstrates how to utilize voice commands for device control through function calls.

This demo uses the OpenAI Realtime GA WebRTC ephemeral token flow. It first creates a short-lived client secret with `POST /v1/realtime/client_secrets`, then exchanges SDP through `POST /v1/realtime/calls` using that token.

## Comparison with OpenAI Realtime Embedded SDK

This demo provides functionality similar to the [OpenAI Realtime Embedded SDK](https://github.com/openai/openai-realtime-embedded-sdk), with several enhancements:

1. **Integrated Solution**: Eliminates the need to build media system components from scratch, offering a ready-to-use implementation.
2. **Optimized Codec**: Leverages `esp_audio_codec` for efficient OPUS encoding and decoding.
3. **Enhanced Interaction**: Includes Acoustic Echo Cancellation (AEC) for better voice interaction quality.
4. **Improved Peer Connection**: Utilizes an enhanced version of `esp_peer` for better performance and reliability.

## Hardware Requirements
The default setup uses the [ESP32-S3-Korvo-2](https://docs.espressif.com/projects/esp-adf/en/latest/design-guide/dev-boards/user-guide-esp32-s3-korvo-2.html).

## How to build

### IDF version
You can select either the IDF master branch or the IDF release v5.4.

### Dependencies
This demo only depends on the **ESP-IDF**. All other required modules will be automatically downloaded from the [ESP-IDF Component Registry](https://components.espressif.com/).

### Change Default Settings
1. Modify the Wi-Fi SSID and password in the file in [settings.h](main/settings.h)
2. Export your OpenAI API key
   ```bash
   export OPENAI_API_KEY=XXXXXXX
   ```
3. To support other boards, refer to the [codec_board README](../../components/codec_board/README.md)

### Build
```
idf.py -p YOUR_SERIAL_DEVICE flash monitor
```

## Testing

After the board boots up, it will attempt to connect to the configured Wi-Fi SSID. 
Once the Wi-Fi is connected successfully, it will try to connect to OpenAI server automatically.  
User can also use following console command to control the session.
1. `start`: start chat
2. `stop`: stop chat
3. `i`: Show system loadings
4. `wifi`: Connect to a new wifi ssid with password
The demo code add some predefined function call for light on off, light color, speaker volume and door open control.
User can use voice command to trigger the function call.

## Technical Details
To connect to OpenAI, this example adds a customized signaling realization `esp_signaling_get_openai_signaling`. It does not use a STUN/TURN server, and thus the `on_ice_info` will have `stun_url` set to `NULL`. The signaling first uses the configured API key to request an ephemeral token from `/v1/realtime/client_secrets`; the SDP information is then exchanged with `/v1/realtime/calls` using that temporary token. All other steps follow the typical call flow of `esp_webrtc`.

For more details on the standard connection build flow, refer to the [Connection Build Flow](../../components/esp_webrtc/README.md#typical-call-sequence-of-esp_webrtc).
