/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Peer error code
 */
typedef enum {
    ESP_PEER_ERR_NONE         = 0,  /*!< None error */
    ESP_PEER_ERR_INVALID_ARG  = -1, /*!< Invalid argument */
    ESP_PEER_ERR_NO_MEM       = -2, /*!< Not enough memory */
    ESP_PEER_ERR_WRONG_STATE  = -3, /*!< Operate on wrong state */
    ESP_PEER_ERR_NOT_SUPPORT  = -4, /*!< Not supported operation */
    ESP_PEER_ERR_NOT_EXISTS   = -5, /*!< Not existed */
    ESP_PEER_ERR_FAIL         = -6, /*!< General error code */
    ESP_PEER_ERR_OVER_LIMITED = -7, /*!< Overlimited */
    ESP_PEER_ERR_BAD_DATA     = -8, /*!< Bad input data */
    ESP_PEER_ERR_WOULD_BLOCK  = -9, /*!< Not enough buffer for output packet, need sleep and retry later */
} esp_peer_err_t;

/**
 * @brief  ICE server configuration
 */
typedef struct {
    char  *stun_url; /*!< STUN/Relay server URL */
    char  *user;     /*!< User name */
    char  *psw;      /*!< User password */
} esp_peer_ice_server_cfg_t;

/**
 * @brief  Peer role
 */
typedef enum {
    ESP_PEER_ROLE_CONTROLLING, /*!< Controlling role who initialize the connection */
    ESP_PEER_ROLE_CONTROLLED,  /*!< Controlled role */
} esp_peer_role_t;

/**
 * @brief  Peer address
 */
typedef struct  {
    uint8_t     family;   /*!< AF_INET and so on */
    uint16_t    port;     /*!< IP port */
    union {
        uint8_t ipv4[4];  /*!< IPV4 address */
        uint8_t ipv6[16]; /*!< IPV6 address */
    };
} esp_peer_addr_t;

/**
 * @brief  Peer RTP transform frame
 */
typedef struct {
    uint8_t  *orig_data;     /*!< Original RTP frame data */
    uint32_t  orig_size;     /*!< Original RTP frame data size */
    uint8_t  *encoded_data;  /*!< Data after transformed */
    uint32_t  encoded_size;  /*!< Data size after encoded */
    uint8_t   payload_type;  /*!< RTP payload type */
} esp_peer_rtp_frame_t;

/**
 * @brief  Peer RTP transform callback
 */
typedef struct {
    /**
     * @brief  Get encoded size after transform
     *
     * @note  If transformer not support in place
     *        `esp_peer` will try to re-allocate memory according the returned `frame->encoded_size`
     *         and set `frame->encoded_data` accordingly
     *
     * @param[in/out]  frame     RTP frame information
     * @param[out]     in_place  Whether transform in place
     * @param[in]      ctx       User context
     *
     * @return
     *       - 0                         On success
     *       - ESP_PEER_ERR_NOT_SUPPORT  Not support transforming (no transform executed)
     *       - Others                    Failed to get encoded size
     */
    int (*get_encoded_size)(esp_peer_rtp_frame_t *frame, bool *in_place, void *ctx);

    /**
     * @brief  Transform function for RTP frame
     *
     * @param[in/out]  frame     RTP frame information
     * @param[out]     in_place  Whether transform in place
     * @param[in]      ctx       User context
     *
     * @return
     *       - 0      On success
     *       - Others Failed to get encoded size
     */
    int (*transform)(esp_peer_rtp_frame_t *frame, void *ctx);
} esp_peer_rtp_transform_cb_t;

#ifdef __cplusplus
}
#endif
