# Changelog

## v1.2.7

### Bug Fixes

- Fixed un-reliable data channel forward TSN not send when limit with 0 setting
- Fixed DTLS role not follow sdp answer


## v1.2.6

### Bug Fixes

- Added `msid` support in SDP
- Fixed padding issue of TURN server relay packet
- Fixed race condition for SCTP reference count
- Fixed wrong fingerprint error log output

### Features

- Added esp32c5 support

## v1.2.5

### Bug Fixes

- Fixed crash regression when ICE server number set to 0
- Fixed relay only setting fail to build connection

## v1.2.4

### Bug Fixes

- Fixed some turn server can not connect
- Improve connectivity stability use relay server
- Fixed `mbedtls_ssl_write` fail due to entropy freed
- Fixed crash when keep alive checking during disconnect
- Added DTLS key print for wireshark analysis

## v1.2.3

### Features

- Added support for IDFv6.0
- Added API `esp_peer_pre_generate_cert` for pre-generate DTLS key

### Bug Fixes

- Fixed build error on IDFv6.0
- Make DTLS module to be open source
- Fixed data channel role not follow DTLS role

## v1.2.2

### Bug Fixes

- Fixed build error for component name miss matched

## v1.2.1

### Features

- Make `esp_peer` as separate module for ESP Component Registry
- Allow RTP rolling buffer allocated on RAM

### Bug Fixes

- Fixed handshake may error if agent receive handshake message during connectivity check

## v1.2.0

### Features

- Added reliable and un-ordered data channel support
- Added FORWARD-TSN support for un-ordered data channel

### Bug Fixes

- Fixed keep alive check not take effect if agent only have local candidates
- Fixed agent mode not set if remote candidate already get

## v1.1.0

### Features

- Export buffer configuration for RTP and data channel
- Added support for multiple data channel
- Added notify for data channel open, close event

### Bug Fixes

- Fixed keep alive check not take effect if peer closed unexpectedly


## v1.0.0

- Initial version of `peer_default`
