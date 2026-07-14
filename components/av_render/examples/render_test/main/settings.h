/* General settings

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Board name setting refer to `codec_board` README.md for more details
 */
#if CONFIG_IDF_TARGET_ESP32P4
#define TEST_BOARD_NAME "ESP32_P4_DEV_V14"
#elif CONFIG_IDF_TARGET_ESP32S31
#define TEST_BOARD_NAME "ESP32_S31_KORVO_1"
#else
#define TEST_BOARD_NAME "S3_Korvo_V2"
#endif

/**
 * @brief  Video resolution settings
 */
#if CONFIG_IDF_TARGET_ESP32P4
#define VIDEO_WIDTH  1024
#define VIDEO_HEIGHT 600
#define VIDEO_FPS    10
#elif CONFIG_IDF_TARGET_ESP32S31
#define VIDEO_WIDTH  640
#define VIDEO_HEIGHT 480
#define VIDEO_FPS    10
#else
#define VIDEO_WIDTH  320
#define VIDEO_HEIGHT 240
#define VIDEO_FPS    10
#endif

#ifdef __cplusplus
}
#endif
