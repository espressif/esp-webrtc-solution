/* Janus Demo

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <stdint.h>
#include "settings.h"
#include "media_sys.h"
#include "network.h"
#include "sys_state.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

void init_board(void);

int  start_webrtc(char *url, uint64_t room_id, const char *pin, const char *token,
                  const char *display, const char *api_secret);

void query_webrtc(void);

int  stop_webrtc(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
