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
 *
 * RTMP supports H264 and MJPEG in esp_media_protocols. For public RTMP
 * services, H264 is the most interoperable choice.
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

/**
 * @brief  Audio settings supported by RTMP in esp_media_protocols
 */
#define AUDIO_FORMAT      ESP_CAPTURE_FMT_ID_AAC
#define AUDIO_SAMPLE_RATE 8000
#define AUDIO_CHANNEL     1

/**
 * @brief  Set for wifi ssid
 */
#define WIFI_SSID "XXXX"

/**
 * @brief  Set for wifi password
 */
#define WIFI_PASSWORD "XXXX"

/**
 * @brief  Default RTMP push URL.
 *
 * Leave empty and pass the URL with the console `start` command, or build with
 * `RTMP_PUSH_URL=rtmp://host/app/stream idf.py build`.
 */
#ifndef RTMP_PUSH_URL
#define RTMP_PUSH_URL ""
#endif

#define RTMP_PUSH_CHUNK_SIZE 1024

#ifdef __cplusplus
}
#endif
