# Agora Bot Station duplex audio demo

This solution connects an official ESP32-S3-Korvo V2 board to an Agora Bot
Station Agent. The board uses Agora duplex WHIP as its only RTC join path. Bot
Station assigns the channel, board string UID, and Agent string UID for each
conversation; the firmware does not use a fixed channel. Microphone and
speaker audio use Opus, 48 kHz, mono in both directions.

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
  The default is `https://botstation.sh2.agoralab.co/api`.
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

### UART console commands

The UART console also supports:

- `start`: stop the current conversation and start a new one.
- `stop`: stop the current conversation and disable automatic restart.
- `reset-pairing`: clear only the local Bot Station credential and pair again.
- `wifi <ssid> [password]`: save new Wi-Fi credentials and reconnect.

## Verify

Keep the UART monitor open while the board starts. Timestamps and assigned
identifiers vary, but a healthy startup reaches each stage below in order.

The common hardware and network startup is:

```text
Audio ADC and DAC are ready
Opus media ready: 48000 Hz mono, 100 ms playout threshold
wifi_init_sta finished.
got ip:<address>
Network connected
Device ID: AG-<MAC address>
```

On the first boot, expect the pairing path:

```text
Persistent Bot Station credential: not present
/devices/pair-codes returned HTTP 201
Bot Station pairing code: <six digits>
Waiting for this device to be claimed
Pairing pending; next poll in <seconds> seconds
Pairing completed; device credential saved
```

`Pairing pending` is normal until the six-digit code is entered in the Bot
Station console. On later boots, the saved credential takes the shorter path:

```text
Persistent Bot Station credential: present
Binding response auth=Device ... status=bound device_id=match
Runtime binding remains valid
Restored Bot Station binding from NVS
```

After either pairing path succeeds, the conversation and WHIP connection
should produce:

```text
/devices/AG-<MAC address>/conversations/start returned HTTP 201
Conversation started for the bound agent
Starting assigned channel=<channel> device_uid=<uid> agent_uid=<uid>
WHIP string_uid=<uid>
Created WHIP credential channel=<channel> string_uid=<uid> exp=<time>
POST duplex offer endpoint=<URL>
WebRTC connecting
POST <URL> returned HTTP 201
Received SDP answer: <bytes> bytes
WebRTC ICE pair selected
WebRTC connected; Opus uplink and downlink enabled
```

The WHIP POST may return another successful `2xx` status. The decisive
markers are the SDP answer, selected ICE pair, and connected event.

### Troubleshoot by stage

- **Board and media:** investigate `Failed to initialize audio ADC`,
  `Failed to initialize audio DAC`, any `Failed to` message from
  `AGORA_MEDIA`, or `Board or audio initialization failed`. Regenerate the
  Board Manager config for `esp32_s3_korvo_2_3`, and verify that both POWER
  and UART are connected.

- **Wi-Fi and time:** repeated `retry to connect to the AP`,
  `Network disconnected`, or `Wi-Fi has no IP; reconnecting` means the board
  has not reached the network. Check the 2.4 GHz SSID, password, DHCP, and
  internet access. `Failed to initialize SNTP`,
  `SNTP has not supplied a valid time`, or
  `System time is not synchronized` means JWT creation cannot proceed. Check
  DNS, internet access, and NTP reachability.

- **Pairing and saved credentials:** investigate
  `Pair-code request failed`, `Pair-code response is incomplete`,
  `Binding response is incomplete or inconsistent`,
  `Bound pairing response omitted device_token`, or NVS save errors. Check
  the Bot Station API base URL, bind the displayed device ID in the matching
  Bot Station console, and verify that NVS is writable.
  `Binding status=expired` or `Binding status=unbound` intentionally returns
  the device to pairing; enter the newly displayed code.

- **Conversation assignment:** investigate `Conversation start failed`,
  `Conversation response failed identity validation`, or
  `Bot Station conversation start failed`. Check that the bound Agent is
  available and that Bot Station returns the configured App ID, a channel,
  device UID, Agent UID, and conversation ID.

- **JWT and WHIP request:** investigate
  `Configure App ID, App Cert, and WHIP base URL`, any JWT error,
  `POST <URL> failed`, or `WHIP offer failed`. An HTTP `401` or `403` usually
  indicates an App ID, App Certificate, token, or deployment mismatch. For
  other `4xx` responses, verify the endpoint and the assigned channel and
  string UID. A transport error before an HTTP status points to DNS, TLS, or
  network connectivity.

- **SDP and ICE:** `Peer rejected the SDP answer`, `WebRTC connection failed`,
  or `Connection timed out; a fresh session is required` after receiving an
  SDP answer means signaling completed but the peer connection did not.
  Check the SDP/codec negotiation, ICE server response, firewall, and UDP
  reachability. `WebRTC disconnected` after a previously healthy connection
  indicates that the network or remote session ended.

- **Bot-to-board audio:** if WebRTC is connected but `Recv A` remains `[0:0]`
  while the Agent is speaking, the board is receiving no downlink audio.
  Check that the Agent joined the logged channel with the logged Agent UID and
  is publishing Opus audio. If receive counts grow but decoder errors increase
  or audio render PTS does not advance, inspect codec negotiation and the
  decoder/render path. If receive counts and render PTS both advance but the
  speaker is silent, inspect the DAC, speaker connection, output volume, and
  board audio hardware.
