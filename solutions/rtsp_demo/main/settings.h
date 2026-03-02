/* General settings

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include "esp_capture_types.h"

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
#define VIDEO_WIDTH  1920
#define VIDEO_HEIGHT 1080
#define VIDEO_FPS    25
#define VIDEO_FORMAT ESP_CAPTURE_FMT_ID_H264
#else
#define VIDEO_WIDTH  320
#define VIDEO_HEIGHT 240
#define VIDEO_FPS    10
#define VIDEO_FORMAT ESP_CAPTURE_FMT_ID_MJPEG
#endif

#define AUDIO_FORMAT      ESP_CAPTURE_FMT_ID_AAC
#define AUDIO_SAMPLE_RATE 16000
#define AUDIO_CHANNEL     2

#define MAX_VIDEO_FRAME_SIZE 600*1024

/**
 * @brief  Set for wifi ssid
 */
#define WIFI_SSID "XXXX"

/**
 * @brief  Set for wifi password
 */
#define WIFI_PASSWORD "XXXX"


#ifdef __cplusplus
}
#endif
