
/**
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2025 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#pragma once

#include "esp_peer.h"
#include "esp_peer_signaling.h"
#include "esp_peer_whip_signaling.h"
#include "esp_peer_janus_signaling.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Get APPRTC signaling implement
 *
 * @param[in]   cfg   Signaling configuration
 * @param[in]   impl  Implement of signaling interface
 * @param[out]  sig   Signaling handle
 *
 * @return
 *       - NULL    Not enough memory
 *       - Others  APPRTC signaling implementation
 */
const esp_peer_signaling_impl_t *esp_signaling_get_apprtc_impl(void);

/**
 * @brief  Get WHIP signaling implementation
 *
 * @return
 *       - NULL    Not enough memory
 *       - Others  WHIP signaling implementation
 */
const esp_peer_signaling_impl_t *esp_signaling_get_whip_impl(void);

/**
 * @brief  Get Janus signaling implementation
 *
 * @return
 *       - NULL    Not enough memory
 *       - Others  Janus signaling implementation
 */
const esp_peer_signaling_impl_t *esp_signaling_get_janus_impl(void);

/**
 * @brief  Get KVS signaling implementation
 *
 * @return
 *       - NULL    Failure to get KVS signaling implementation
 *       - Others  KVS signaling implementation pointer
 */
const esp_peer_signaling_impl_t *esp_signaling_get_kvs_impl(void);

/**
 * @brief  Get default peer connection implementation
 * @return
 *       - NULL    No default implementation, or not enough memory
 *       - Others  Default implementation
 */
const esp_peer_ops_t *esp_peer_get_default_impl(void);

#ifdef __cplusplus
}
#endif
