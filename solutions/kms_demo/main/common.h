/* Video Call Demo

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include "settings.h"
#include "media_sys.h"
#include "network.h"
#include "sys_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Initialize for board
 */
void init_board(void);

/**
 * @brief  Start WebRTC
 *
 * @param[in]  url  Kurento server websocketURL
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to start
 */
int start_webrtc(char *url);

/**
 * @brief  Query WebRTC status
 */
void query_webrtc(void);

/**
 * @brief  Stop WebRTC
 *
 * @return
 *       - 0       On success
 *       - Others  Fail to stop
 */
int stop_webrtc(void);

#ifdef __cplusplus
}
#endif
