/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_peer_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Peer state
 */
typedef enum {
    ESP_PEER_STATE_CLOSED                    = 0,  /*!< Closed */
    ESP_PEER_STATE_DISCONNECTED              = 1,  /*!< Disconnected */
    ESP_PEER_STATE_NEW_CONNECTION            = 2,  /*!< New connection coming */
    ESP_PEER_STATE_CANDIDATE_GATHERING       = 3,  /*!< Starting to get candidates */
    ESP_PEER_STATE_PAIRING                   = 4,  /*!< Under candidate pairing */
    ESP_PEER_STATE_PAIRED                    = 5,  /*!< Candidate pairing success */
    ESP_PEER_STATE_CONNECTING                = 6,  /*!< Building connection with peer */
    ESP_PEER_STATE_CONNECTED                 = 7,  /*!< Connected with peer */
    ESP_PEER_STATE_CONNECT_FAILED            = 8,  /*!< Connect failed */
    ESP_PEER_STATE_DATA_CHANNEL_CONNECTED    = 9,  /*!< Data channel is connected */
    ESP_PEER_STATE_DATA_CHANNEL_OPENED       = 10, /*!< Data channel is opened */
    ESP_PEER_STATE_DATA_CHANNEL_CLOSED       = 11, /*!< Data channel is closed */
    ESP_PEER_STATE_DATA_CHANNEL_DISCONNECTED = 12, /*!< Data channel is disconnected */
} esp_peer_state_t;

/**
 * @brief  Peer video codec
 */
typedef enum {
    ESP_PEER_VIDEO_CODEC_NONE  = 0, /*!< Invalid video codec type */
    ESP_PEER_VIDEO_CODEC_H264  = 1, /*!< H264 video codec */
    ESP_PEER_VIDEO_CODEC_MJPEG = 2, /*!< MJPEG video codec */
} esp_peer_video_codec_t;

/**
 * @brief  Peer audio codec
 */
typedef enum {
    ESP_PEER_AUDIO_CODEC_NONE  = 0, /*!< Invalid audio codec type */
    ESP_PEER_AUDIO_CODEC_G711A = 1, /*!< G711-alaw(PCMA) audio codec */
    ESP_PEER_AUDIO_CODEC_G711U = 2, /*!< G711-ulaw(PCMU) audio codec */
    ESP_PEER_AUDIO_CODEC_OPUS  = 3, /*!< OPUS audio codec */
} esp_peer_audio_codec_t;

/**
 * @brief  Data channel type
 */
typedef enum {
    ESP_PEER_DATA_CHANNEL_NONE   = 0, /*!< Invalid type */
    ESP_PEER_DATA_CHANNEL_DATA   = 1, /*!< Data type */
    ESP_PEER_DATA_CHANNEL_STRING = 2, /*!< String type */
} esp_peer_data_channel_type_t;

/**
 * @brief  Media transmission direction
 */
typedef enum {
    ESP_PEER_MEDIA_DIR_NONE      = 0,
    ESP_PEER_MEDIA_DIR_SEND_ONLY = (1 << 0), /*!< Send only */
    ESP_PEER_MEDIA_DIR_RECV_ONLY = (1 << 1), /*!< Receive only */
    ESP_PEER_MEDIA_DIR_SEND_RECV = ESP_PEER_MEDIA_DIR_SEND_ONLY | ESP_PEER_MEDIA_DIR_RECV_ONLY, /*!< Send and receive both */
} esp_peer_media_dir_t;

/**
 * @brief  ICE transport policy
 */
typedef enum {
    ESP_PEER_ICE_TRANS_POLICY_ALL   = 0, /*!< All ICE candidates will be used for pairing */
    ESP_PEER_ICE_TRANS_POLICY_RELAY = 1, /*!< Only relay ICE candidates will be used for pairing */
} esp_peer_ice_trans_policy_t;

/**
 * @brief  Video stream information
 */
typedef struct {
    esp_peer_video_codec_t  codec;  /*!< Video codec */
    int                     width;  /*!< Video width */
    int                     height; /*!< Video height */
    int                     fps;    /*!< Video fps */
} esp_peer_video_stream_info_t;

/**
 * @brief  Audio stream information
 */
typedef struct {
    esp_peer_audio_codec_t  codec;       /*!< Audio codec */
    uint32_t                sample_rate; /*!< Audio sample rate */
    uint8_t                 channel;     /*!< Audio channel */
} esp_peer_audio_stream_info_t;

/**
 * @brief  Video frame information
 */
typedef struct {
    uint32_t  pts;  /*!< Video frame presentation timestamp */
    uint8_t  *data; /*!< Video frame data pointer */
    int       size; /*!< Video frame data size */
} esp_peer_video_frame_t;

/**
 * @brief  Audio frame information
 */
typedef struct {
    uint32_t  pts;  /*!< Audio frame presentation timestamp */
    uint8_t  *data; /*!< Audio frame data pointer */
    int       size; /*!< Audio frame data size */
} esp_peer_audio_frame_t;

/**
 * @brief  Data frame information
 */
typedef struct {
    esp_peer_data_channel_type_t  type;      /*!< Data channel type */
    uint16_t                      stream_id; /*!< Data channel stream ID */
    uint8_t                      *data;      /*!< Pointer to data to be sent through data channel */
    int                           size;      /*!< Data size */
} esp_peer_data_frame_t;

/**
 * @brief  Peer message type
 */
typedef enum {
    ESP_PEER_MSG_TYPE_NONE,      /*!< None message type */
    ESP_PEER_MSG_TYPE_SDP,       /*!< SDP message type */
    ESP_PEER_MSG_TYPE_CANDIDATE, /*!< ICE candidate message type */
} esp_peer_msg_type_t;

/**
 * @brief  Peer message
 */
typedef struct {
    esp_peer_msg_type_t type; /*!< Message type */
    uint8_t            *data; /*!< Message data */
    int                 size; /*!< Message data size */
} esp_peer_msg_t;

/**
 * @brief  Reliable type for peer data channel
 */
typedef enum {
    ESP_PEER_DATA_CHANNEL_RELIABLE,                  /*!< Reliable data channel with guaranteed delivery */
    ESP_PEER_DATA_CHANNEL_PARTIAL_RELIABLE_TIMEOUT,  /*!< Unreliable by lifetime (maxPacketLifeTime) */
    ESP_PEER_DATA_CHANNEL_PARTIAL_RELIABLE_RETX,     /*!< Unreliable by max retransmits (maxRetransmits) */
} esp_peer_data_channel_reliable_type_t;

/**
 * @brief  Configuration for peer data channel
 */
typedef struct {
    esp_peer_data_channel_reliable_type_t  type;    /*!< Reliability type */
    bool                                   ordered; /*!< true = ordered delivery, false = unordered */
    char                                  *label;   /*!< Data channel label */
    union {
        uint16_t  max_retransmit_count; /*!< Only valid if type is ESP_PEER_DATA_CHANNEL_PARTIAL_RELIABLE_RETX */
        uint16_t  max_packet_lifetime;  /*!< Only valid if type is ESP_PEER_DATA_CHANNEL_PARTIAL_RELIABLE_TIMEOUT (in milliseconds) */
    };
} esp_peer_data_channel_cfg_t;

/**
 * @brief  Peer data channel information
 */
typedef struct {
    const char *label;      /*!< Data channel label */
    uint16_t    stream_id;  /*!< Chunk stream id for this channel */
} esp_peer_data_channel_info_t;

/**
 * @brief  Peer RTP transform role
 */
typedef enum {
    ESP_PEER_RTP_TRANSFORM_ROLE_SENDER   = 0,  /*!< RTP transformer for sender */
    ESP_PEER_RTP_TRANSFORM_ROLE_RECEIVER = 1,  /*!< RTP transformer for receiver */
} esp_peer_rtp_transform_role_t;

/**
 * @brief  Peer handle
 */
typedef void *esp_peer_handle_t;

/**
 * @brief  Peer configuration
 */
typedef struct {
    esp_peer_ice_server_cfg_t   *server_lists;        /*< ICE server list */
    uint8_t                      server_num;          /*!< Number of ICE server */
    esp_peer_role_t              role;                /*!< Peer role */
    esp_peer_ice_trans_policy_t  ice_trans_policy;    /*!< ICE transport policy */
    esp_peer_audio_stream_info_t audio_info;          /*!< Audio stream information */
    esp_peer_video_stream_info_t video_info;          /*!< Video stream information */
    esp_peer_media_dir_t         audio_dir;           /*!< Audio transmission direction */
    esp_peer_media_dir_t         video_dir;           /*!< Video transmission direction */
    bool                         no_auto_reconnect;   /*!< Disable auto reconnect if connected fail */
    bool                         enable_data_channel; /*!< Enable data channel */
    bool                         manual_ch_create;    /*!< Manual create data channel
                                                           When SCTP role is client, it will try to send DCEP automatically
                                                           The default configuration are ordered, partial reliable timeout when use peer_default
                                                           To disable this behavior can create data channel manually and set this flag
                                                          */
    void                        *extra_cfg;           /*!< Extra configuration */
    int                          extra_size;          /*!< Extra configuration size */
    void                        *ctx;                 /*!< User context */

    /**
     * @brief  Event callback for state
     * @param[in]  state  Current peer state
     * @param[in]  ctx    User context
     * @return            Status code indicating success or failure.
     */
    int (*on_state)(esp_peer_state_t state, void* ctx);

    /**
     * @brief  Message callback
     * @param[in]  info  Pointer to peer message
     * @param[in]  ctx   User context
     * @return           Status code indicating success or failure.
     */
    int (*on_msg)(esp_peer_msg_t* info, void* ctx);

    /**
     * @brief  Peer video stream information callback
     * @param[in]  info  Video stream information
     * @param[in]  ctx   User context
     * @return           Status code indicating success or failure.
     */
    int (*on_video_info)(esp_peer_video_stream_info_t* info, void* ctx);

    /**
     * @brief  Peer audio stream information callback
     * @param[in]  info  Audio stream information
     * @param[in]  ctx   User context
     * @return           Status code indicating success or failure.
     */
    int (*on_audio_info)(esp_peer_audio_stream_info_t* info, void* ctx);

    /**
     * @brief  Peer audio frame callback
     * @param[in]  frame  Audio frame information
     * @param[in]  ctx    User context
     * @return            Status code indicating success or failure.
     */
    int (*on_audio_data)(esp_peer_audio_frame_t* frame, void* ctx);

    /**
     * @brief  Peer video frame callback
     * @param[in]  frame  Video frame information
     * @param[in]  ctx    User context
     * @return            Status code indicating success or failure.
     */
    int (*on_video_data)(esp_peer_video_frame_t* frame, void* ctx);

    /**
     * @brief  Peer data channel opened event callback
     * @param[in]  ch   Data channel information
     * @param[in]  ctx  User context
     * @return          Status code indicating success or failure.
     */
    int (*on_channel_open)(esp_peer_data_channel_info_t *ch, void *ctx);

    /**
     * @brief  Peer data frame callback
     * @param[in]  frame  Data frame information
     * @param[in]  ctx    User context
     * @return            Status code indicating success or failure.
     */
    int (*on_data)(esp_peer_data_frame_t *frame, void *ctx);

    /**
     * @brief  Peer data channel closed event callback
     * @param[in]  ch   Data channel information
     * @param[in]  ctx  User context
     * @return          Status code indicating success or failure.
     */
    int (*on_channel_close)(esp_peer_data_channel_info_t *ch, void *ctx);
} esp_peer_cfg_t;

/**
 * @brief  Peer connection interface
 */
typedef struct {
    /**
     * @brief  Open peer connection
     * @param[in]   cfg   Peer configuration
     * @param[out]  peer  Peer handle
     * @return            Status code indicating success or failure.
     */
    int (*open)(esp_peer_cfg_t* cfg, esp_peer_handle_t* peer);

    /**
     * @brief  Create a new conenction
     * @param[in]   peer  Peer handle
     * @return            Status code indicating success or failure.
     */
    int (*new_connection)(esp_peer_handle_t peer);

    /**
     * @brief  Update ICE information
     * @param[in]   peer        Peer handle
     * @param[in]   servers     ICE Server settings
     * @param[in]   server_num  Number of ICE servers
     * @return            Status code indicating success or failure.
     */
    int (*update_ice_info)(esp_peer_handle_t peer, esp_peer_role_t role, esp_peer_ice_server_cfg_t* server, int server_num);

    /**
     * @brief  Send message to peer
     * @param[in]   peer  Peer handle
     * @param[in]   msg   Message to be sent
     * @return            Status code indicating success or failure.
     */
    int (*send_msg)(esp_peer_handle_t peer, esp_peer_msg_t* msg);

    /**
     * @brief  Send video frame data to peer
     * @param[in]   peer   Peer handle
     * @param[in]   frame  Video frame to be sent
     * @return             Status code indicating success or failure.
     */
    int (*send_video)(esp_peer_handle_t peer, esp_peer_video_frame_t* frame);

    /**
     * @brief  Send audio frame data to peer
     * @param[in]   peer   Peer handle
     * @param[in]   frame  Audio frame to be sent
     * @return             Status code indicating success or failure.
     */
    int (*send_audio)(esp_peer_handle_t peer, esp_peer_audio_frame_t* frame);

    /**
     * @brief  Send data frame data to peer
     * @param[in]   peer   Peer handle
     * @param[in]   frame  Data frame to be sent
     * @return             Status code indicating success or failure.
     */
    int (*send_data)(esp_peer_handle_t peer, esp_peer_data_frame_t* frame);

    /**
     * @brief  Manually create data chanel for peer
     * @param[in]   peer    Peer handle
     * @param[in]   ch_cfg  Data channel configuration
     * @return              Status code indicating success or failure.
     */
    int (*create_data_channel)(esp_peer_handle_t peer, esp_peer_data_channel_cfg_t *ch_cfg);

    /**
     * @brief  Close data chanel for peer
     * @param[in]   peer   Peer handle
     * @param[in]   label  Data channel label
     * @return             Status code indicating success or failure.
     */
    int (*close_data_channel)(esp_peer_handle_t peer, const char *label);

    /**
     * @brief  Peer main loop
     * @note  Peer connection need handle peer status change, receive stream data in this loop
     *        Or create a thread to handle these things and synchronize with this loop
     * @param[in]   peer   Peer handle
     * @return             Status code indicating success or failure.
     */
    int (*main_loop)(esp_peer_handle_t peer);

    /**
     * @brief  Disconnected with peer
     * @param[in]   peer   Peer handle
     * @return             Status code indicating success or failure.
     */
    int (*disconnect)(esp_peer_handle_t peer);

    /**
     * @brief  Query peer status
     * @param[in]   peer   Peer handle
     */
    void (*query)(esp_peer_handle_t peer);

    /**
     * @brief  Close peer connection and release related resources
     * @param[in]   peer   Peer handle
     * @return             Status code indicating success or failure.
     */
    int (*close)(esp_peer_handle_t peer);

    /**
     * @brief  Set RTP transformer for peer
     *
     * @param[in]  handle        Peer handle
     * @param[in]  role          RTP transform role
     * @param[in]  transform_cb  Transform callback
     * @param[in]  ctx           Callback context
     *
     * @return
     *       - 0      On success
     *       - Others Failed to set
     */
    int (*set_rtp_transformer)(esp_peer_handle_t handle, esp_peer_rtp_transform_role_t role,
                               esp_peer_rtp_transform_cb_t *transform_cb, void *ctx);
} esp_peer_ops_t;

/**
 * @brief  Open peer connection
 *
 * @param[in]   cfg   Peer configuration
 * @param[in]   ops   Peer connection implementation
 * @param[out]  peer  Peer handle
 *
 * @return
 *       - ESP_PEER_ERR_NONE         Open peer connection success
 *       - ESP_PEER_ERR_INVALID_ARG  Invalid argument
 *       - ESP_PEER_ERR_NOT_SUPPORT  Not support
 */
int esp_peer_open(esp_peer_cfg_t *cfg, const esp_peer_ops_t *ops, esp_peer_handle_t *peer);

/**
 * @brief  Create new conenction
 *
 * @note  After new connection is created, It will try gather ICE candidate from ICE servers.
 *        And report local SDP to let user send to signaling server.
 *
 * @param[in]  peer  Peer handle
 *
 * @return
 *       - ESP_PEER_ERR_NONE         Open peer connection success
 *       - ESP_PEER_ERR_INVALID_ARG  Invalid argument
 *       - ESP_PEER_ERR_NOT_SUPPORT  Not support
 */
int esp_peer_new_connection(esp_peer_handle_t peer);

/**
 * @brief  Manually create data channel
 *
 * @note  It will send DCEP event to peer until data channel created
 *
 * @param[in]  peer    Peer handle
 * @param[in]  ch_cfg  Configuration for data channel creation
 *
 * @return
 *       - ESP_PEER_ERR_NONE         Open data channel success
 *       - ESP_PEER_ERR_INVALID_ARG  Invalid argument
 *       - ESP_PEER_ERR_NOT_SUPPORT  Not support
 */
int esp_peer_create_data_channel(esp_peer_handle_t peer, esp_peer_data_channel_cfg_t *ch_cfg);

/**
 * @brief  Manually close data channel by label
 *
 * @param[in]  peer   Peer handle
 * @param[in]  label  Channel label
 *
 * @return
 *       - ESP_PEER_ERR_NONE         Close data channel success
 *       - ESP_PEER_ERR_INVALID_ARG  Invalid argument
 *       - ESP_PEER_ERR_NOT_SUPPORT  Not support
 */
int esp_peer_close_data_channel(esp_peer_handle_t peer, const char *label);

/**
 * @brief  Update ICE server information
 *
 * @note  After new connection is created, It will try gather ICE candidate from ICE servers.
 *        And report local SDP to let user send to signaling server.
 *
 * @param[in]  peer        Peer handle
 * @param[in]  role        Peer roles of controlling or controlled
 * @param[in]  server      Pointer to array of ICE server configuration
 * @param[in]  server_num  ICE server number
 *
 * @return
 *       - ESP_PEER_ERR_NONE         Open peer connection success
 *       - ESP_PEER_ERR_INVALID_ARG  Invalid argument
 *       - ESP_PEER_ERR_NOT_SUPPORT  Not support
 */
int esp_peer_update_ice_info(esp_peer_handle_t peer, esp_peer_role_t role, esp_peer_ice_server_cfg_t* server, int server_num);

/**
 * @brief  Set RTP transformer for peer
 *
 * @param[in]  handle        Peer handle
 * @param[in]  role          RTP transform role
 * @param[in]  transform_cb  Transform callback (set to NULL to disable transformer)
 * @param[in]  ctx           Callback context
 *
 * @return
 *       - 0      On success
 *       - Others Failed to get encoded size
 */
int esp_peer_set_rtp_transformer(esp_peer_handle_t handle, esp_peer_rtp_transform_role_t role,
                                 esp_peer_rtp_transform_cb_t *transform_cb, void *ctx);

/**
 * @brief  Send message to peer
 *
 * @param[in]  peer  Peer handle
 * @param[in]  msg   Message to send to peer
 *
 * @return
 *       - ESP_PEER_ERR_NONE         Open peer connection success
 *       - ESP_PEER_ERR_INVALID_ARG  Invalid argument
 *       - ESP_PEER_ERR_NOT_SUPPORT  Not support
 */
int esp_peer_send_msg(esp_peer_handle_t peer, esp_peer_msg_t *msg);

/**
 * @brief  Send video data to peer
 *
 * @param[in]  peer   Peer handle
 * @param[in]  frame  Video frame data
 *
 * @return
 *       - ESP_PEER_ERR_NONE         Open peer connection success
 *       - ESP_PEER_ERR_INVALID_ARG  Invalid argument
 *       - ESP_PEER_ERR_NOT_SUPPORT  Not support
 */
int esp_peer_send_video(esp_peer_handle_t peer, esp_peer_video_frame_t *frame);

/**
 * @brief  Send audio data to peer
 *
 * @param[in]  peer   Peer handle
 * @param[in]  frame  Audio frame data
 *
 * @return
 *       - ESP_PEER_ERR_NONE         Open peer connection success
 *       - ESP_PEER_ERR_INVALID_ARG  Invalid argument
 *       - ESP_PEER_ERR_NOT_SUPPORT  Not support
 */
int esp_peer_send_audio(esp_peer_handle_t peer, esp_peer_audio_frame_t *info);

/**
 * @brief  Send data through data channel to peer
 *
 * @param[in]  peer   Peer handle
 * @param[in]  frame  Video frame data
 *
 * @return
 *       - ESP_PEER_ERR_NONE         Open peer connection success
 *       - ESP_PEER_ERR_INVALID_ARG  Invalid argument
 *       - ESP_PEER_ERR_NOT_SUPPORT  Not support
 *       - ESP_PEER_ERR_WOULD_BLOCK  Data channel buffer is full, need sleep some time and retry later
 */
int esp_peer_send_data(esp_peer_handle_t peer, esp_peer_data_frame_t *frame);

/**
 * @brief  Run peer connection main loop
 *
 * @note  This loop need to be call repeatedly
 *        It handle peer connection status change also receive stream data
 *        Currently default peer realization have no extra thread internally, all is triggered in this loop
 *
 * @param[in]  peer  Peer handle
 *
 * @return
 *       - ESP_PEER_ERR_NONE         Open peer connection success
 *       - ESP_PEER_ERR_INVALID_ARG  Invalid argument
 *       - ESP_PEER_ERR_NOT_SUPPORT  Not support
 */
int esp_peer_main_loop(esp_peer_handle_t peer);

/**
 * @brief  Disconnect peer connection
 *
 * @note  Disconnect will try to close socket which communicate with peer and signaling server
 *        If user want to continue to listen for peer connect in
 *        User can call `esp_peer_new_connection` so that it will retry to gather ICE candidate and report local SDP
 *        So that new peer can connection in through offered SDP from signaling server.
 *
 * @param[in]  peer  Peer handle
 *
 * @return
 *       - ESP_PEER_ERR_NONE         Open peer connection success
 *       - ESP_PEER_ERR_INVALID_ARG  Invalid argument
 *       - ESP_PEER_ERR_NOT_SUPPORT  Not support
 */
int esp_peer_disconnect(esp_peer_handle_t peer);

/**
 * @brief  Query of peer connection
 *
 * @note  This API is for debug usage only
 *
 * @param[in]  peer  Peer handle
 *
 * @return
 *       - ESP_PEER_ERR_NONE         Open peer connection success
 *       - ESP_PEER_ERR_INVALID_ARG  Invalid argument
 */
int esp_peer_query(esp_peer_handle_t peer);

/**
 * @brief  Get paired remote address after connectivity check success
 *
 * @note  This API current only supported by peer_default
 *        Need to call if after `ESP_PEER_STATE_PAIRED` received
 *
 * @param[in]  peer  Peer handle
 *
 * @return
 *       - ESP_PEER_ERR_NONE         Open peer connection success
 *       - ESP_PEER_ERR_INVALID_ARG  Invalid argument
 *       - ESP_PEER_ERR_WRONG_STATE  Not paired yet
 */
int esp_peer_get_paired_addr(esp_peer_handle_t handle, esp_peer_addr_t *addr);

/**
 * @brief  Close peer connection
 *
 * @note  Close peer connection will do `esp_peer_disconnect` firstly then release all resource occupied by peer realization.
 *
 * @param[in]  peer  Peer handle
 *
 * @return
 *       - ESP_PEER_ERR_NONE         Open peer connection success
 *       - ESP_PEER_ERR_INVALID_ARG  Invalid argument
 *       - ESP_PEER_ERR_NOT_SUPPORT  Not support
 */
int esp_peer_close(esp_peer_handle_t peer);

/**
 * @brief  Pre-generates cryptographic materials for DTLS handshake to optimize connection establishment
 *
 * @note  This function generates and caches X.509 certificates and associated private keys
 *          that will be used for subsequent DTLS handshakes. Pre-generation is recommended
 *          because:
 *          - Cryptographic operations during runtime can significantly delay connection establishment
 *          - Certificate generation is computationally intensive
 *          - Reusing pre-generated materials maintains security while improving performance
 *       Important considerations:
 *       - Generated materials are stored in internal memory and persist until reset
 *       - Each call overwrites any previously generated materials
 *
 * @return
 *       - ESP_PEER_ERR_NONE  On success
 *       - Others             Failed to generate
 */
int esp_peer_pre_generate_cert(void);

#ifdef __cplusplus
}
#endif
