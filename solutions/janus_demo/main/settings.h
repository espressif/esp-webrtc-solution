/* General settings

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#if CONFIG_IDF_TARGET_ESP32P4
#define TEST_BOARD_NAME  "ESP32_P4_DEV_V14"
#else
#define TEST_BOARD_NAME  "S3_Korvo_V2"
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */

#if CONFIG_IDF_TARGET_ESP32P4
#define VIDEO_WIDTH   1920
#define VIDEO_HEIGHT  1080
#define VIDEO_FPS     25
#else
#define VIDEO_WIDTH   320
#define VIDEO_HEIGHT  240
#define VIDEO_FPS     10
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */

#define WIFI_SSID      "XXXX"
#define WIFI_PASSWORD  "XXXX"

/**
 * Janus HTTP API endpoint (e.g. http://192.168.1.10:8088/janus)
 */
#define JANUS_SERVER  "http://XXXX:8088/janus"

/**
 * Janus VideoRoom id
 */
#define JANUS_ROOM_ID  1234

/**
 * Optional room pin/token/apisecret; set to NULL when not used
 */
#define JANUS_PIN         NULL
#define JANUS_TOKEN       NULL
#define JANUS_API_SECRET  "adminpwd"
#define JANUS_DISPLAY     "esp32-publisher"

#ifdef __cplusplus
}
#endif  /* __cplusplus */
