/* RTSP server or pusher test

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#pragma once


#include "esp_rtsp.h"

#ifdef __cplusplus
extern "C" {
#endif

int start_rtsp(esp_rtsp_mode_t mode, const char *uri);

int stop_rtsp(void);

#ifdef __cplusplus
}
#endif
