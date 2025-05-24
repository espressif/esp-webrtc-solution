/* General settings

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Set used board name, see `codec_board` README.md for more details
 */
#if CONFIG_AUDIO_BOARD_DEFAULT_P4
#define TEST_BOARD_NAME "ESP32_P4_DEV_V14"
#elif CONFIG_AUDIO_BOARD_S3_KORVO_V2
#define TEST_BOARD_NAME "S3_Korvo_V2"
#elif CONFIG_AUDIO_BOARD_MUSE_RADIO
#define TEST_BOARD_NAME "MUSE_RADIO"
#endif

/**
 * @brief  If defined will use OPUS codec
 */
#define WEBRTC_SUPPORT_OPUS

/**
 * @brief  Whether enable data channel
 */
#define DATA_CHANNEL_ENABLED (true)

/**
 * @brief  Set WiFi SSID
 */
#define WIFI_SSID "XXXX"

/**
 * @brief  Set WiFi password
 */
#define WIFI_PASSWORD "XXXX"

/**
 * @brief  Set default playback volume
 */
#define DEFAULT_PLAYBACK_VOL (85)

#ifdef __cplusplus
}
#endif
