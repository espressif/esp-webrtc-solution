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
 * @brief  Board name setting refer to `codec_board` README.md for more details
 */
#if CONFIG_IDF_TARGET_ESP32P4
#define TEST_BOARD_NAME "ESP32_P4_DEV_V14"
#else
#define TEST_BOARD_NAME "S3_Korvo_V2"
#endif

/**
 * @brief  Video resolution settings
 */
#if CONFIG_IDF_TARGET_ESP32P4
#define VIDEO_WIDTH  1024
#define VIDEO_HEIGHT 600
#else
#define VIDEO_WIDTH  320
#define VIDEO_HEIGHT 240
#endif
#define VIDEO_FPS    10

/**
 * @brief  Set for wifi ssid
 */
#define WIFI_SSID     "XXXX"

/**
 * @brief  Set for wifi password
 */
#define WIFI_PASSWORD "XXXX"

/**
 * @brief  Whether enable data channel
 */
#define DATA_CHANNEL_ENABLED (true)

#if CONFIG_IDF_TARGET_ESP32P4
/**
 * @brief  GPIO for ring button
 *
 * @note  When use ESP32P4-Fuction-Ev-Board, GPIO35(boot button) is connected RMII_TXD1
 *        When enable `NETWORK_USE_ETHERNET` will cause socket error
 *        User must replace it to a unused GPIO instead (like GPIO27)
 */
#define VIDEO_CALL_RING_BUTTON  35
#else
/**
 * @brief  GPIO for ring button
 *
 * @note  When use ESP32S3-KORVO-V3 Use ADC button as ring button
 */
#define VIDEO_CALL_RING_BUTTON  5
#endif

#ifdef __cplusplus
}
#endif
