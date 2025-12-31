/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include "esp_peer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Peer default data channel configuration
 */
typedef struct {
    uint16_t  cache_timeout;    /*!< Data channel frame keep timeout (unit ms) default: 5000ms if set to 0 */
    uint32_t  send_cache_size;  /*!< Cache size for outgoing data channel packets (unit Bytes)
                                    default: 100kB if set to 0 */
    uint32_t  recv_cache_size;  /*!< Cache size for incoming data channel packets (unit Bytes)
                                    default: 100kB if set to 0 */
} esp_peer_default_data_ch_cfg_t;

/**
 * @brief  Peer default jitter buffer configuration
 */
typedef struct {
    uint16_t  cache_timeout;  /*!< Maximum timeout to keep the received RTP packet (unit ms) default: 100ms if set to 0 */
    uint16_t  resend_delay;   /*!< Not resend until resend delay reached (unit ms) default: 20ms if set to 0*/
    uint32_t  cache_size;     /*!< Cache size for incoming data channel frame (unit Bytes)
                                  For audio jitter buffer default: 100kB if set to 0
                                  For video jitter buffer default: 400kB if set to 0 */
} esp_peer_default_jitter_cfg_t;

/**
 * @brief  Peer default RTP send and receive configuration
 *
 * @note  Insufficient RTP resources can disable features like packet retransmission and loss reporting
 *        Without these features, packet loss may result in audio glitches and mosaic-like video artifacts
 */
typedef struct {
    esp_peer_default_jitter_cfg_t  audio_recv_jitter;  /*!< Audio jitter buffer configuration */
    esp_peer_default_jitter_cfg_t  video_recv_jitter;  /*!< Video jitter buffer configuration */
    uint32_t                       send_pool_size;     /*!< Send pool size for outgoing RTP packets (unit Bytes)
                                                            default: 400kB if set to 0 */
    uint32_t                       send_queue_num;     /*!< Maximum queue number to hold outgoing RTP packet metadata info
                                                            default: 256 */
    uint16_t                       max_resend_count;   /*!< Maximum resend count for one RTP packet
                                                            default: 3 times */
} esp_peer_default_rtp_cfg_t;

/**
 * @brief  Peer default configuration (optional)
 */
typedef struct {
    uint16_t                        agent_recv_timeout;  /*!< ICE agent receive timeout setting (unit ms)
                                                              default: 100ms if set to 0
                                                              Some STUN/TURN server reply message slow increase this value */
    esp_peer_default_data_ch_cfg_t  data_ch_cfg;         /*!< Configuration of data channel */
    esp_peer_default_rtp_cfg_t      rtp_cfg;             /*!< Configuration of RTP buffer */
    bool                            keep_role;           /*!< Do not reset role to controlling when disconnected */
} esp_peer_default_cfg_t;

/**
 * @brief  Get default peer connection implementation
 *
 * @return
 *       - NULL    No default implementation, or not enough memory
 *       - Others  Default implementation
 */
const esp_peer_ops_t *esp_peer_get_default_impl(void);

#ifdef __cplusplus
}
#endif
