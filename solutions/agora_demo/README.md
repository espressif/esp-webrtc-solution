# Agora Bot Station duplex audio demo

This solution connects an official ESP32-S3-Korvo V2 board to an Agora Bot
Station Agent. The board uses duplex WHIP as its only RTC join path. Bot
Station assigns the channel, board string UID, and Agent string UID for each
conversation; the firmware does not use a fixed channel.

Microphone and speaker audio use Opus, 48 kHz, mono in both directions. Video
is disabled. All Agora-specific code is contained in this directory, so the
shared `components/` and other solutions do not need changes.

## Prerequisites

- An official ESP32-S3-Korvo V2 board.
- ESP-IDF 5.4.4 or newer.
- One USB connection to the POWER socket and one to the UART socket.
- A 2.4 GHz Wi-Fi network reachable by the board.
- Access to an Agora Bot Station deployment and its console.
- An Agora account and project.

Create a free Agora account or sign in at the
[Agora Console](https://console.agora.io/). Create a project and obtain its
App ID and App Certificate. The project used by the firmware must be the same
project configured for the Bot Station deployment.

## Configure authentication and Wi-Fi

Install and activate ESP-IDF 5.4.4 or newer by following the
[ESP-IDF setup guide](https://docs.espressif.com/projects/esp-idf/en/v5.4.4/esp32s3/get-started/index.html),
then install the ESP Board Manager helper and select the Korvo board profile:

```bash
cd solutions/agora_demo
python -m pip install --upgrade esp-bmgr-assist
idf.py gen-bmgr-config -b esp32_s3_korvo_2_3
idf.py menuconfig
```

Use `idf.py gen-bmgr-config -l` to list the board profiles available in the
installed Board Manager package.

Under **Agora Bot Station duplex audio demo**, configure:

- **Agora App ID**: the App ID shown for the project in Agora Console.
- **Agora App Cert**: the App Certificate shown for the project when dynamic
  authentication is enabled. When dynamic authentication is disabled, use the
  App ID in this field.
- **Agora WHIP endpoint base URL**: the endpoint base URL supplied for the
  deployment. The default is the production endpoint at
  `https://ap-webrtc-whip.ap.sd-rtn.com`.
- **Bot Station API base URL**: the Device API URL for the same deployment.
  The default points to the staging Device API.
- **Bot Station hardware model**: the model reported during pairing. The
  default is suitable for ESP32-S3-Korvo V2.
- **Wi-Fi SSID**: the 2.4 GHz access point name.
- **Wi-Fi password**: the access point password. Leave it empty only for an
  open network.

The App ID returned by Bot Station is checked against the configured App ID.
After a production deployment is ready, change the App ID, App Certificate,
WHIP base URL, and Bot Station API base URL together so they all refer to that
deployment.

Do not commit `sdkconfig`, Wi-Fi credentials, App Certificates, device tokens,
or generated JWTs. Embedding an App Certificate is acceptable for this demo,
but production firmware should not contain a long-lived signing secret.

## Pair the board

On first boot after Wi-Fi and time synchronization succeed, the board derives
a stable device ID from its Wi-Fi MAC address and requests a six-digit pairing
code. The UART console prints:

```text
Bot Station pairing code: <six digits>
```

Sign in to the [Agora Bot Station console](https://botstation.sh2.agoralab.co/),
select the Agent that should own the board, and pair the device with that code.
The code binds the board's stable device ID to that exact Agent. The channel
and UIDs returned later identify one conversation; they are not the Agent's
persistent identity.

While waiting, the firmware follows the polling interval returned by the
server. Once the claim succeeds, it stores only the long-lived `device_token`
in the `bot_station` NVS namespace. The pairing code and short-lived
`pair_token` are never persisted.

On later boots, the firmware loads the device token, validates that the board
is still bound, and starts a conversation automatically. Bot Station returns
the channel and string UIDs. The firmware validates the returned Agent and App
ID, generates a short-lived WHIP JWT with the configured App Certificate, and
joins through duplex WHIP. Runtime binding checks detect a remote unbind and
return the board to pairing.

The duplex WHIP URL carries the Bot Station-assigned RTC account:

```text
/pub/<channel>?stringuid=<Bot Station UID>&duplex=true
```

Do not add a numeric `uid` query or a JWT `uid` claim. The production AP
assigns the numeric routing UID before forwarding the request to vos_web.

To bind the board to a different Agent, unbind it in the Bot Station console,
then run this UART command:

```text
reset-pairing
```

The command stops the current conversation, clears only the local Bot Station
credential, and requests a new pairing code. It does not erase saved Wi-Fi
settings or the full NVS partition.

The demo stores the device token in ordinary NVS. A production design should
enable NVS encryption or use equivalent secure storage.

## Build and flash

The `esp32_s3_korvo_2_3` Board Manager profile configures the official
ESP32-S3-Korvo-2 V3.1 audio ADC, audio DAC, and ESP32-S3 target. No third-party
board files are required.

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

A successful first run includes these stages:

```text
Audio ADC and DAC are ready
Opus media ready: 48000 Hz mono
Bot Station pairing code: <six digits>
Pairing completed; device credential saved
Conversation started for the bound agent
Starting assigned channel=<channel> device_uid=<string uid> agent_uid=<string uid>
WHIP string_uid=<string uid>
POST duplex offer endpoint=<endpoint>
Received SDP answer
WebRTC ICE pair selected
WebRTC connected; Opus uplink and downlink enabled
```

After a reboot, `Persistent Bot Station credential: present` and
`Restored Bot Station binding from NVS` replace the pairing-code stage.

Use the `i` command to print system, WebRTC, decoder, and renderer statistics.
Confirm that both sent and received audio counters increase, that the audio
decoder error count remains zero, and that the audio render PTS advances.
Then verify microphone and speaker audio with the Agent.

The UART console also supports:

- `start`: stop the current conversation and start a new one.
- `stop`: stop the current conversation and automatic restart.
- `reset-pairing`: clear only the local Bot Station credential and pair again.
- `wifi <ssid> [password]`: save new Wi-Fi credentials and reconnect.
