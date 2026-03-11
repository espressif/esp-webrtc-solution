/* Common API definition

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#include "settings.h"
#include "network.h"
#include "sys_state.h"

int start_webrtc(char *url);

void query_webrtc(void);

int stop_webrtc(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
