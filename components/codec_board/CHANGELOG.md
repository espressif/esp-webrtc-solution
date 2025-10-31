# Changelog

## v1.0.2

- Fixed pin parse error

## v1.0.1

- Upgrade dependency of `esp_codev_dev` to v1.5.x

## v1.0.0

- Upgrade dependency of `esp_codev_dev` to v1.4.0
- Added API `codec_board_parse_all_config` for easy customized board which not in `board_cfg.txt`
- Added API `get_playback/record_i2s_channel` to get low level i2s channel
- Fixed build issues on IDFv6.0

## v0.5.5

- Add board support for `ESP32_S3_KORVO_2L`, `ESP32_S3_EchoEar`, `ATOMS3_ECHO_BASE`
- Mount SDcard use high speed for better performance on esp32s3 and esp32p4

## v0.5.4

- Add PDM support
- Add play and record support for `XD_AIOT_C3` and `ESP_SPOT`

## v0.5.3

- Add support for mount SDCard on esp32
- Add support for I2C manually control use `init_i2c` and `deinit_i2c`
- Fixed I2S driver failed to initialized if more than one

## v0.5.0

- Initial version of `codec_board`
