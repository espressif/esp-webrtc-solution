/* WebRTC HTTPS server signaling

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include "esp_err.h"
#include "esp_peer_signaling.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Get the HTTP signaling implementation
 *
 * @return
 *       - NULL    Not enough memory
 *       - Others  HTTPS server signaling implementation
 */
const esp_peer_signaling_impl_t *esp_signaling_get_http_impl(void);

#ifdef __cplusplus
}
#endif