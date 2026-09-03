# Agora duplex audio demo

This solution connects an official ESP32-S3-Korvo V2 board to Agora as an
audio-only WebRTC endpoint. Microphone and speaker audio use Opus, 48 kHz,
mono in both directions. Video is disabled.

All Agora-specific code is contained in this directory. The shared
`components/` and other solutions do not need changes.

## Prerequisites

- An official ESP32-S3-Korvo V2 board.
- ESP-IDF 5.4 or newer.
- One USB connection to the POWER socket and one to the UART socket.
- A 2.4 GHz Wi-Fi network reachable by the board.
- An Agora account and project.

Create a free Agora account or sign in at the
[Agora Console](https://console.agora.io/). In the Console, create a project
and obtain its App ID and App Certificate. Configure the project's
authentication mode before building the firmware.

## Configure Agora authentication and Wi-Fi

Install and activate ESP-IDF 5.4 or newer by following the
[ESP-IDF setup guide](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32s3/get-started/index.html),
then open the project configuration from the repository root:

```bash
cd solutions/agora_demo
idf.py set-target esp32s3
idf.py menuconfig
```

Under **Agora duplex WHIP demo**, configure:

- **Agora App ID**: the App ID shown for your project in Agora Console.
- **Agora App Cert**: use the App Certificate shown for your project when
  dynamic authentication is enabled. When dynamic authentication is disabled,
  use the App ID in this field.
- **Agora channel name**: the channel that the ESP32 and the other participant
  will join.
- **Agora WHIP endpoint base URL**: the endpoint base URL supplied for the
  deployment. Enter only the base URL; the application adds the resource path,
  channel, UID, and duplex option.
- **Wi-Fi SSID**: the 2.4 GHz access point name.
- **Wi-Fi password**: the access point password. Leave it empty only for an
  open network.

Do not commit `sdkconfig`, Wi-Fi credentials, App Certificates, or
authentication tokens. If credentials from an earlier run are stored in NVS,
use the serial command below to replace them:

```text
wifi <ssid> [password]
```

## Build and flash

The default board selection is `S3_Korvo_V2`, provided by the unchanged
upstream `codec_board` component. No third-party board files are required.

Build the firmware:

```bash
idf.py build
```

Find the board's UART port using the device manager for your operating system.
Its name can change after reconnecting the board. Replace `PORT` below with
that port:

```bash
idf.py -p PORT flash monitor
```

Keep both POWER and UART connected while the firmware runs. Exit the monitor
with `Ctrl+]`.

## Verify

A successful startup includes these log stages:

```text
Codec ready: board=S3_Korvo_V2
Opus media ready: 48000 Hz mono
Starting channel=<channel> uid=<uid>
POST duplex offer endpoint=<base>/pub/<channel>?Uid=<uid>&duplex=true
POST ... returned HTTP 2xx
Received SDP answer
WebRTC ICE pair selected
WebRTC connected; Opus uplink and downlink enabled
```

Use the `i` serial command to print system and media statistics. Confirm that
both sent and received audio counters increase, then verify microphone and
speaker audio with another participant in the configured channel.

The serial console also supports:

- `start`: create and start a fresh session.
- `stop`: stop the current session and automatic restart.
- `wifi <ssid> [password]`: save new Wi-Fi credentials and reconnect.
