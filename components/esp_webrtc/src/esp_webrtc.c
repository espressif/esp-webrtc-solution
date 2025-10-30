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

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/time.h>
#include <sys/param.h>
#include "esp_log.h"
#include "media_lib_os.h"
#include "esp_timer.h"
#include "esp_webrtc.h"
#include "esp_codec_dev.h"
#include "esp_webrtc_defaults.h"
#include "esp_capture_sink.h"

#define AUDIO_FRAME_INTERVAL (20)
#define STR_SAME(a, b)       (strncmp(a, b, sizeof(b) - 1) == 0)
#define GOTO_LABEL_ON_NULL(label, ptr, code) if (ptr == NULL) {   \
    ret = code;                                                   \
    goto label;                                                   \
}

#define SAFE_FREE(ptr) if (ptr) {   \
    free(ptr);                      \
    ptr = NULL;                     \
}
#define PC_EXIT_BIT      (1 << 0)
#define PC_PAUSED_BIT    (1 << 1)
#define PC_RESUME_BIT    (1 << 2)
#define PC_SEND_QUIT_BIT (1 << 3)

#define SET_WAIT_BITS(bit) media_lib_event_group_set_bits(rtc->wait_event, bit)
#define WAIT_FOR_BITS(bit)                                                          \
    media_lib_event_group_wait_bits(rtc->wait_event, bit, MEDIA_LIB_MAX_LOCK_TIME); \
    media_lib_event_group_clr_bits(rtc->wait_event, bit)

typedef enum {
    WEBRTC_PRE_SETTING_MASK_AUDIO_BITRATE = (1 << 0),
    WEBRTC_PRE_SETTING_MASK_VIDEO_BITRATE = (1 << 1),
    WEBRTC_PRE_SETTING_MASK_ALL           = 0xFF,
} webrtc_pre_setting_mask_t;

typedef struct {
    uint32_t audio_bitrate;
    uint32_t video_bitrate;
    uint16_t preset_mask;
} webrtc_pre_setting_t;

typedef struct {
    esp_webrtc_cfg_t             rtc_cfg;
    esp_peer_handle_t            pc;
    esp_peer_signaling_handle_t  signaling;
    esp_peer_state_t             peer_state;
    bool                         running;
    bool                         pause;
    media_lib_event_grp_handle_t wait_event;
    esp_webrtc_event_handler_t   event_handler;
    esp_peer_role_t              ice_role;
    void                        *ctx;
    // These field will change to esp_media_stream in the near feature

    esp_timer_handle_t            send_timer;
    bool                          send_going;
    esp_webrtc_media_provider_t   media_provider;
    esp_capture_sink_handle_t     capture_path;
    esp_codec_dev_handle_t        play_handle;
    esp_peer_audio_stream_info_t  recv_aud_info;
    esp_peer_video_stream_info_t  recv_vid_info;
    bool                          pending_connect;
    esp_peer_signaling_ice_info_t ice_info;
    bool                          ice_info_loaded;
    bool                          signaling_connected;
    bool                          no_auto_capture;
    webrtc_pre_setting_t          pre_setting;

    uint8_t *aud_fifo;
    uint32_t aud_fifo_size;
    // For debug only
    uint32_t vid_send_pts;
    uint32_t aud_send_pts;
    uint32_t aud_recv_pts;
    uint32_t vid_send_size;
    uint32_t aud_send_size;
    uint32_t aud_recv_size;
    uint32_t vid_recv_size;
    uint8_t  aud_send_num;
    uint8_t  vid_send_num;
    uint8_t  aud_recv_num;
    uint8_t  vid_recv_num;
} webrtc_t;

static const char *TAG = "webrtc";

bool webrtc_tracing = false;

static void _media_send(void *ctx)
{
    webrtc_t *rtc = (webrtc_t *)ctx;
    if (rtc->rtc_cfg.peer_cfg.audio_info.codec) {
        esp_capture_stream_frame_t audio_frame = {
            .stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO,
        };
        // Get and send all audio frame without wait
        while (esp_capture_sink_acquire_frame(rtc->capture_path, &audio_frame, true) == ESP_CAPTURE_ERR_OK) {
            esp_peer_audio_frame_t audio_send_frame = {
                .pts = audio_frame.pts,
                .data = audio_frame.data,
                .size = audio_frame.size,
            };
            esp_peer_send_audio(rtc->pc, &audio_send_frame);
            esp_capture_sink_release_frame(rtc->capture_path, &audio_frame);
            rtc->aud_send_pts = audio_frame.pts;
            rtc->aud_send_num++;
            rtc->aud_send_size += audio_frame.size;
            if (webrtc_tracing) {
                printf("A\n");
            }
        }
    }
    if (rtc->rtc_cfg.peer_cfg.video_info.codec) {
        esp_capture_stream_frame_t video_frame = {
            .stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO,
        };
        int ret = esp_capture_sink_acquire_frame(rtc->capture_path, &video_frame, true);
        if (ret == ESP_CAPTURE_ERR_OK) {
            if (rtc->rtc_cfg.peer_cfg.enable_data_channel && rtc->rtc_cfg.peer_cfg.video_over_data_channel) {
                esp_peer_data_frame_t data_frame = {
                    .type = ESP_PEER_DATA_CHANNEL_DATA,
                    .data = video_frame.data,
                    .size = video_frame.size,
                };
                esp_peer_send_data(rtc->pc, &data_frame);
            } else {
                esp_peer_video_frame_t video_send_frame = {
                    .pts = video_frame.pts,
                    .data = video_frame.data,
                    .size = video_frame.size,
                };
                // Call the video send callback if provided (for SEI injection, etc.)
                bool should_send = true;
                if (rtc->rtc_cfg.peer_cfg.on_video_send) {
                    ret = rtc->rtc_cfg.peer_cfg.on_video_send(&video_send_frame, rtc->rtc_cfg.peer_cfg.ctx);
                    if (ret != ESP_CAPTURE_ERR_OK) {
                        should_send = false;
                    }
                }
                if (should_send) {
                    esp_peer_send_video(rtc->pc, &video_send_frame);
                }
            }
            esp_capture_sink_release_frame(rtc->capture_path, &video_frame);
            rtc->vid_send_pts = video_frame.pts;
            rtc->vid_send_num++;
            rtc->vid_send_size += video_frame.size;
            if (webrtc_tracing) {
                printf("V\n");
            }
        }
    }
}

void media_send_task(void *arg)
{
    webrtc_t *rtc = (webrtc_t *)arg;
    while (rtc->send_going) {
        _media_send(arg);
        media_lib_thread_sleep(AUDIO_FRAME_INTERVAL);
    }
    SET_WAIT_BITS(PC_SEND_QUIT_BIT);
    media_lib_thread_destroy(NULL);
}

static int start_stream(webrtc_t *rtc)
{
    int ret = esp_capture_start(rtc->media_provider.capture);
    if (ret == ESP_CAPTURE_ERR_OK) {
        media_lib_thread_handle_t handle = NULL;
        rtc->send_going = true;
        ret = media_lib_thread_create_from_scheduler(&handle, "pc_send", media_send_task, rtc);
        if (ret != 0) {
            rtc->send_going = false;
        }
    } else {
        ESP_LOGE(TAG, "Fail to start capture ret:%d", ret);
    }
    return ret;
}

static int stop_stream(webrtc_t *rtc)
{
    if (rtc->send_going) {
        rtc->send_going = false;
        WAIT_FOR_BITS(PC_SEND_QUIT_BIT);
    }
    if (rtc->no_auto_capture == false) {
        esp_capture_stop(rtc->media_provider.capture);
    } else {
        esp_capture_sink_enable(rtc->capture_path, ESP_CAPTURE_RUN_MODE_DISABLE);
    }
    av_render_reset(rtc->play_handle);
    return 0;
}

static void pc_notify_app(webrtc_t *rtc, esp_webrtc_event_type_t event_type)
{
    esp_webrtc_event_t event = {
        .type = event_type,
    };
    if (rtc->event_handler) {
        rtc->event_handler(&event, rtc->ctx);
    }
}

static int pc_on_state(esp_peer_state_t state, void *ctx)
{
    webrtc_t *rtc = (webrtc_t *)ctx;
    ESP_LOGI(TAG, "PeerConnectionState: %d", state);
    if (state != ESP_PEER_STATE_DATA_CHANNEL_OPENED &&
        state != ESP_PEER_STATE_DATA_CHANNEL_CLOSED &&
        state != ESP_PEER_STATE_DATA_CHANNEL_CONNECTED &&
        state != ESP_PEER_STATE_DATA_CHANNEL_DISCONNECTED) {
        rtc->peer_state = state;
    }

    if (state == ESP_PEER_STATE_CONNECTED) {
        start_stream(rtc);
        pc_notify_app(rtc, ESP_WEBRTC_EVENT_CONNECTED);
    } else if (state == ESP_PEER_STATE_DISCONNECTED) {
        stop_stream(rtc);
        pc_notify_app(rtc, ESP_WEBRTC_EVENT_DISCONNECTED);
    } else if (state == ESP_PEER_STATE_CONNECT_FAILED) {
        // Run in mainloop task
        pc_notify_app(rtc, ESP_WEBRTC_EVENT_CONNECT_FAILED);
    } else if (state == ESP_PEER_STATE_DATA_CHANNEL_CONNECTED) {
        pc_notify_app(rtc, ESP_WEBRTC_EVENT_DATA_CHANNEL_CONNECTED);
    } else if (state == ESP_PEER_STATE_DATA_CHANNEL_DISCONNECTED) {
        pc_notify_app(rtc, ESP_WEBRTC_EVENT_DATA_CHANNEL_DISCONNECTED);
    } else if (state == ESP_PEER_STATE_DATA_CHANNEL_OPENED) {
        pc_notify_app(rtc, ESP_WEBRTC_EVENT_DATA_CHANNEL_OPENED);
    } else if (state == ESP_PEER_STATE_DATA_CHANNEL_CLOSED) {
        pc_notify_app(rtc, ESP_WEBRTC_EVENT_DATA_CHANNEL_CLOSED);
    }
    return 0;
}

static int pc_on_msg(esp_peer_msg_t *info, void *ctx)
{
    webrtc_t *rtc = (webrtc_t *)ctx;
    ESP_LOGI(TAG, "Send client sdp: %s\n", info->data);
    return esp_peer_signaling_send_msg(rtc->signaling, (esp_peer_signaling_msg_t *)info);
}

static void pc_task(void *arg)
{
    webrtc_t *rtc = (webrtc_t *)arg;
    ESP_LOGI(TAG, "peer_connection_task started");
    while (rtc->running) {
        if (rtc->pause) {
            SET_WAIT_BITS(PC_PAUSED_BIT);
            WAIT_FOR_BITS(PC_RESUME_BIT);
            continue;
        }
        esp_peer_main_loop(rtc->pc);
        media_lib_thread_sleep(10);
    }
    SET_WAIT_BITS(PC_EXIT_BIT);
    media_lib_thread_destroy(NULL);
}

static av_render_video_codec_t get_video_dec_codec(esp_peer_video_codec_t codec)
{
    switch (codec) {
        case ESP_PEER_VIDEO_CODEC_H264:
            return AV_RENDER_VIDEO_CODEC_H264;
        case ESP_PEER_VIDEO_CODEC_MJPEG:
            return AV_RENDER_VIDEO_CODEC_MJPEG;
        default:
            return AV_RENDER_VIDEO_CODEC_NONE;
    }
}
static void convert_dec_vid_info(esp_peer_video_stream_info_t *info, av_render_video_info_t *dec_info)
{
    dec_info->codec = get_video_dec_codec(info->codec);
    dec_info->width = info->width;
    dec_info->height = info->height;
    dec_info->fps = info->fps;
}

static int pc_on_video_info(esp_peer_video_stream_info_t *info, void *ctx)
{
    webrtc_t *rtc = (webrtc_t *)ctx;
    av_render_video_info_t video_info = {};
    convert_dec_vid_info(info, &video_info);
    av_render_add_video_stream(rtc->play_handle, &video_info);
    rtc->recv_vid_info.codec = info->codec;
    return 0;
}

static av_render_audio_codec_t get_dec_codec(esp_peer_audio_codec_t codec)
{
    switch (codec) {
        case ESP_PEER_AUDIO_CODEC_G711A:
            return AV_RENDER_AUDIO_CODEC_G711A;
        case ESP_PEER_AUDIO_CODEC_G711U:
            return AV_RENDER_AUDIO_CODEC_G711U;
        case ESP_PEER_AUDIO_CODEC_OPUS:
            return AV_RENDER_AUDIO_CODEC_OPUS;
        default:
            return AV_RENDER_AUDIO_CODEC_NONE;
    }
}
static void convert_dec_aud_info(esp_peer_audio_stream_info_t *info, av_render_audio_info_t *dec_info)
{
    dec_info->codec = get_dec_codec(info->codec);
    if (info->codec == ESP_PEER_AUDIO_CODEC_G711A || info->codec == ESP_PEER_AUDIO_CODEC_G711U) {
        dec_info->sample_rate = 8000;
        dec_info->channel = 1;
    } else {
        dec_info->sample_rate = info->sample_rate;
        dec_info->channel = info->channel;
    }
    dec_info->bits_per_sample = 16;
}

static int pc_on_audio_info(esp_peer_audio_stream_info_t *info, void *ctx)
{
    webrtc_t *rtc = (webrtc_t *)ctx;
    rtc->recv_aud_info = *info;
    av_render_audio_info_t audio_info = {};
    convert_dec_aud_info(info, &audio_info);
    printf("Add audio codec %d sample_rate %d\n", audio_info.codec, (int)audio_info.sample_rate);
    av_render_add_audio_stream(rtc->play_handle, &audio_info);
    rtc->recv_aud_info.codec = info->codec;
    return 0;
}

static int pc_on_audio_data(esp_peer_audio_frame_t *info, void *ctx)
{
    webrtc_t *rtc = (webrtc_t *)ctx;
    if (rtc->running == false || rtc->recv_aud_info.codec == ESP_PEER_AUDIO_CODEC_NONE) {
        return 0;
    }
    rtc->aud_recv_pts = info->pts;
    rtc->aud_recv_num++;
    rtc->aud_recv_size += info->size;
    av_render_audio_data_t audio_data = {
        .pts = info->pts,
        .data = info->data,
        .size = info->size,
    };
    av_render_add_audio_data(rtc->play_handle, &audio_data);
    return 0;
}

static int pc_on_video_data(esp_peer_video_frame_t *info, void *ctx)
{
    webrtc_t *rtc = (webrtc_t *)ctx;
    if (rtc->running == false) {
        return 0;
    }
    rtc->vid_recv_num++;
    rtc->vid_recv_size += info->size;
    av_render_video_data_t video_data = {
        .pts = info->pts,
        .data = info->data,
        .size = info->size,
    };
    av_render_add_video_data(rtc->play_handle, &video_data);
    return 0;
}

static int pc_on_data(esp_peer_data_frame_t *frame, void *ctx)
{
    webrtc_t *rtc = (webrtc_t *)ctx;
    // Notify custom data over data channel
    if (rtc->rtc_cfg.peer_cfg.video_over_data_channel == false) {
        if (rtc->rtc_cfg.peer_cfg.on_data) {
            rtc->rtc_cfg.peer_cfg.on_data(frame, rtc->rtc_cfg.peer_cfg.ctx);
        }
        if (rtc->rtc_cfg.peer_cfg.on_custom_data) {
            rtc->rtc_cfg.peer_cfg.on_custom_data(ESP_WEBRTC_CUSTOM_DATA_VIA_DATA_CHANNEL,
                                                 frame->data, frame->size, rtc->rtc_cfg.peer_cfg.ctx);
        }
        return 0;
    }
    rtc->vid_recv_num++;
    rtc->vid_recv_size += frame->size;
    // Treat received data as video data
    if (rtc->recv_vid_info.codec == ESP_PEER_VIDEO_CODEC_NONE) {
        rtc->recv_vid_info.codec = rtc->rtc_cfg.peer_cfg.video_info.codec;
        av_render_video_info_t video_info = {};
        convert_dec_vid_info(&rtc->recv_vid_info, &video_info);
        av_render_add_video_stream(rtc->play_handle, &video_info);
    }
    av_render_video_data_t video_data = {
        .data = frame->data,
        .size = frame->size,
    };
    av_render_add_video_data(rtc->play_handle, &video_data);
    return 0;
}

static void free_server_cfg(webrtc_t *rtc)
{
    if (rtc->rtc_cfg.peer_cfg.server_lists == NULL) {
        return;
    }
    esp_peer_ice_server_cfg_t *server_cfg = rtc->rtc_cfg.peer_cfg.server_lists;
    for (int i = 0; i < rtc->rtc_cfg.peer_cfg.server_num; i++) {
        SAFE_FREE(server_cfg[i].stun_url);
        SAFE_FREE(server_cfg[i].user);
        SAFE_FREE(server_cfg[i].psw);
    }
    SAFE_FREE(rtc->rtc_cfg.peer_cfg.server_lists);
    rtc->rtc_cfg.peer_cfg.server_num = 0;
}

static int malloc_server_cfg(webrtc_t *rtc, esp_peer_ice_server_cfg_t *cfg, int server_num)
{
    if (server_num == 0) {
        return ESP_PEER_ERR_NONE;
    }
    if (rtc->rtc_cfg.peer_cfg.server_num) {
        ESP_LOGE(TAG, "Already have server config");
        return ESP_PEER_ERR_WRONG_STATE;
    }
    rtc->rtc_cfg.peer_cfg.server_lists = calloc(1, server_num * sizeof(esp_peer_ice_server_cfg_t));
    if (rtc->rtc_cfg.peer_cfg.server_lists == NULL) {
        return ESP_PEER_ERR_NO_MEM;
    }
    rtc->rtc_cfg.peer_cfg.server_num = server_num;
    esp_peer_ice_server_cfg_t *dst = rtc->rtc_cfg.peer_cfg.server_lists;
    int ret = ESP_PEER_ERR_NONE;
    for (int i = 0; i < server_num; i++) {
        GOTO_LABEL_ON_NULL(_exit, cfg[i].stun_url, ESP_PEER_ERR_INVALID_ARG);
        dst[i].stun_url = strdup(cfg[i].stun_url);
        GOTO_LABEL_ON_NULL(_exit, dst[i].stun_url, ESP_PEER_ERR_NO_MEM);
        if (cfg[i].user) {
            dst[i].user = strdup(cfg[i].user);
            GOTO_LABEL_ON_NULL(_exit, dst[i].user, ESP_PEER_ERR_NO_MEM);
        }
        if (cfg[i].psw) {
            dst[i].psw = strdup(cfg[i].psw);
            GOTO_LABEL_ON_NULL(_exit, dst[i].psw, ESP_PEER_ERR_NO_MEM);
        }
    }
    return ret;
_exit:
    free_server_cfg(rtc);
    return ESP_PEER_ERR_INVALID_ARG;
}

static esp_capture_format_id_t get_capture_audio_codec(esp_peer_audio_codec_t aud_codec)
{
    switch (aud_codec) {
        default:
            return ESP_CAPTURE_FMT_ID_NONE;
        case ESP_PEER_AUDIO_CODEC_G711A:
            return ESP_CAPTURE_FMT_ID_G711A;
        case ESP_PEER_AUDIO_CODEC_G711U:
            return ESP_CAPTURE_FMT_ID_G711U;
        case ESP_PEER_AUDIO_CODEC_OPUS:
            return ESP_CAPTURE_FMT_ID_OPUS;
    }
}

static esp_capture_format_id_t get_capture_video_codec(esp_peer_audio_codec_t vid_codec)
{
    switch (vid_codec) {
        default:
            return ESP_CAPTURE_FMT_ID_NONE;
        case ESP_PEER_VIDEO_CODEC_H264:
            return ESP_CAPTURE_FMT_ID_H264;
        case ESP_PEER_VIDEO_CODEC_MJPEG:
            return ESP_CAPTURE_FMT_ID_MJPEG;
    }
}

static int pc_close(webrtc_t *rtc)
{
    if (rtc->pc) {
        esp_peer_disconnect(rtc->pc);
        bool still_running = rtc->running;
        // Wait for PC task quit
        if (rtc->pause) {
            rtc->pause = false;
            SET_WAIT_BITS(PC_RESUME_BIT);
        }
        rtc->running = false;
        if (still_running) {
            WAIT_FOR_BITS(PC_EXIT_BIT);
        }
        esp_peer_close(rtc->pc);
        rtc->pc = NULL;
    }
    if (rtc->wait_event) {
        media_lib_event_group_destroy(rtc->wait_event);
        rtc->wait_event = NULL;
    }
    return ESP_PEER_ERR_NONE;
}

static int pc_on_channel_open(esp_peer_data_channel_info_t *ch, void *ctx)
{
    webrtc_t *rtc = (webrtc_t *)ctx;
    if (rtc->rtc_cfg.peer_cfg.on_channel_open) {
        return rtc->rtc_cfg.peer_cfg.on_channel_open(ch, rtc->ctx);
    }
    return 0;
}

static int pc_on_channel_close(esp_peer_data_channel_info_t *ch, void *ctx)
{
    webrtc_t *rtc = (webrtc_t *)ctx;
    if (rtc->rtc_cfg.peer_cfg.on_channel_close) {
        return rtc->rtc_cfg.peer_cfg.on_channel_close(ch, rtc->ctx);
    }
    return 0;
}

static int pc_apply_capture_pre_setting(webrtc_t *rtc, uint16_t set_mask)
{
    if (rtc->capture_path == NULL) {
        return ESP_PEER_ERR_NONE;
    }
    int ret = ESP_PEER_ERR_NONE;
    // TODO we not clear in case stop and start again can use pre-setting also
    if (rtc->pre_setting.preset_mask & (set_mask & WEBRTC_PRE_SETTING_MASK_AUDIO_BITRATE)) {
        ret |= esp_capture_sink_set_bitrate(rtc->capture_path, ESP_CAPTURE_STREAM_TYPE_AUDIO, rtc->pre_setting.audio_bitrate);
    }
    if (rtc->pre_setting.preset_mask & (set_mask & WEBRTC_PRE_SETTING_MASK_VIDEO_BITRATE)) {
        ret |= esp_capture_sink_set_bitrate(rtc->capture_path, ESP_CAPTURE_STREAM_TYPE_VIDEO, rtc->pre_setting.video_bitrate);
    }
    return ret;
}

static int pc_start(webrtc_t *rtc, esp_peer_ice_server_cfg_t *server_info, int server_num)
{
    if (rtc->pc) {
        return esp_peer_update_ice_info(rtc->pc, rtc->ice_role, server_info, server_num);
    }
    esp_peer_cfg_t peer_cfg = {
        .server_lists = server_info,
        .server_num = server_num,
        .ice_trans_policy = rtc->rtc_cfg.peer_cfg.ice_trans_policy,
        .audio_dir = rtc->rtc_cfg.peer_cfg.audio_dir,
        .video_dir = rtc->rtc_cfg.peer_cfg.video_dir,
        .enable_data_channel = rtc->rtc_cfg.peer_cfg.enable_data_channel,
        .manual_ch_create = rtc->rtc_cfg.peer_cfg.manual_ch_create,
        .no_auto_reconnect = rtc->rtc_cfg.peer_cfg.no_auto_reconnect,
        .extra_cfg = rtc->rtc_cfg.peer_cfg.extra_cfg,
        .extra_size = rtc->rtc_cfg.peer_cfg.extra_size,
        .on_state = pc_on_state,
        .on_msg = pc_on_msg,
        .on_video_info = pc_on_video_info,
        .on_audio_info = pc_on_audio_info,
        .on_video_data = pc_on_video_data,
        .on_audio_data = pc_on_audio_data,
        .on_channel_open = pc_on_channel_open,
        .on_channel_close = pc_on_channel_close,
        .on_data = pc_on_data,
        .role = rtc->ice_role,
        .ctx = rtc,
    };
    memcpy(&peer_cfg.audio_info, &rtc->rtc_cfg.peer_cfg.audio_info, sizeof(esp_peer_audio_stream_info_t));
    if (rtc->rtc_cfg.peer_cfg.enable_data_channel == false || rtc->rtc_cfg.peer_cfg.video_over_data_channel == false) {
        memcpy(&peer_cfg.video_info, &rtc->rtc_cfg.peer_cfg.video_info, sizeof(esp_peer_video_stream_info_t));
    }
    int ret = esp_peer_open(&peer_cfg, esp_peer_get_default_impl(), &rtc->pc);
    if (ret != ESP_PEER_ERR_NONE) {
        ESP_LOGE(TAG, "Fail to open peer ret %d", ret);
        return ret;
    }
    media_lib_event_group_create(&rtc->wait_event);
    if (rtc->wait_event == NULL) {
        return ESP_PEER_ERR_NO_MEM;
    }
    // Set running flag
    rtc->running = true;
    media_lib_thread_handle_t thread;
    media_lib_thread_create_from_scheduler(&thread, "pc_task", pc_task, rtc);
    esp_capture_sink_cfg_t sink_cfg = {
        .audio_info = {
            .format_id = get_capture_audio_codec(peer_cfg.audio_info.codec),
            .sample_rate = peer_cfg.audio_info.sample_rate ? peer_cfg.audio_info.sample_rate : 8000,
            .channel = peer_cfg.audio_info.channel ? peer_cfg.audio_info.channel : 1,
            .bits_per_sample = 16,
        },
        .video_info = {
            .format_id = get_capture_video_codec(rtc->rtc_cfg.peer_cfg.video_info.codec),
            .width = rtc->rtc_cfg.peer_cfg.video_info.width,
            .height = rtc->rtc_cfg.peer_cfg.video_info.height,
            .fps = rtc->rtc_cfg.peer_cfg.video_info.fps,
        },
    };
    rtc->play_handle = rtc->media_provider.player;
    if (peer_cfg.audio_dir == ESP_PEER_MEDIA_DIR_RECV_ONLY) {
        sink_cfg.audio_info.format_id = ESP_CAPTURE_FMT_ID_NONE;
    } else if (peer_cfg.audio_dir == ESP_PEER_MEDIA_DIR_SEND_ONLY) {
        rtc->play_handle = NULL;
    }
    if (peer_cfg.video_dir == ESP_PEER_MEDIA_DIR_RECV_ONLY) {
        sink_cfg.video_info.format_id = ESP_CAPTURE_FMT_ID_NONE;
    }
    esp_capture_sink_setup(rtc->media_provider.capture, 0, &sink_cfg, &rtc->capture_path);
    pc_apply_capture_pre_setting(rtc, WEBRTC_PRE_SETTING_MASK_ALL);
    esp_capture_sink_enable(rtc->capture_path, ESP_CAPTURE_RUN_MODE_ALWAYS);
    return ret;
}

static int start_peer_connection(webrtc_t *rtc, esp_peer_signaling_ice_info_t *info)
{
    rtc->ice_role = info->is_initiator ? ESP_PEER_ROLE_CONTROLLING : ESP_PEER_ROLE_CONTROLLED;
    int ret;
    if (info->server_info.stun_url) {
        ret = pc_start(rtc, &info->server_info, 1);
    } else {
        ret = pc_start(rtc, rtc->rtc_cfg.peer_cfg.server_lists, rtc->rtc_cfg.peer_cfg.server_num);
    }
    return ret;
}

static int signal_ice_received(esp_peer_signaling_ice_info_t *info, void *ctx)
{
    webrtc_t *rtc = (webrtc_t *)ctx;
    rtc->ice_info_loaded = true;
    rtc->ice_info = *info;
    if (rtc->pending_connect) {
        ESP_LOGI(TAG, "Pending connection until user enable");
        return ESP_PEER_ERR_NONE;
    }
    return start_peer_connection(rtc, info);
}

static int signal_connected(void *ctx)
{
    webrtc_t *rtc = (webrtc_t *)ctx;
    rtc->signaling_connected = true;
    if (rtc->rtc_cfg.peer_cfg.no_auto_reconnect && rtc->pending_connect) {
        printf("Signaling connected, pending for use not enable\n");
        return 0;
    }
    if (rtc->pc) {
        // Create offer so that fetch ice candidate
        esp_peer_new_connection(rtc->pc);
    }
    return 0;
}

static int signal_new_msg(esp_peer_signaling_msg_t *msg, void *ctx)
{
    webrtc_t *rtc = (webrtc_t *)ctx;
    if (msg->type == ESP_PEER_SIGNALING_MSG_BYE) {
        ESP_LOGI(TAG, "Received BYE");
        int ret = ESP_PEER_ERR_NONE;
        if (rtc->running) {
            // Wait for main loop paused
            if (rtc->pause == false) {
                rtc->pause = true;
                WAIT_FOR_BITS(PC_PAUSED_BIT);
            }
            esp_peer_disconnect(rtc->pc);
            rtc->recv_vid_info.codec = ESP_PEER_VIDEO_CODEC_NONE;
            stop_stream(rtc);
            if (rtc->rtc_cfg.peer_cfg.no_auto_reconnect == false) {
                // Reconnect
                ret = esp_peer_new_connection(rtc->pc);
                if (rtc->pause) {
                    // resume main loop
                    rtc->pause = false;
                    SET_WAIT_BITS(PC_RESUME_BIT);
                }
            }
        }
        return ret;
    } else {
        if (msg->type == ESP_PEER_SIGNALING_MSG_CUSTOMIZED) {
            ESP_LOGI(TAG, "Received customized data");
            if (rtc->rtc_cfg.peer_cfg.on_custom_data) {
                return rtc->rtc_cfg.peer_cfg.on_custom_data(
                    ESP_WEBRTC_CUSTOM_DATA_VIA_SIGNALING,
                    msg->data,
                    msg->size,
                    rtc->rtc_cfg.peer_cfg.ctx);
            }
            return 0;
        }
        char *sdp = (char *)msg->data;
        esp_peer_msg_t peer_msg = {
            .type = msg->type,
            .data = msg->data,
            .size = msg->size,
        };
        if (STR_SAME(sdp, "candidate:")) {
            peer_msg.type = ESP_PEER_MSG_TYPE_CANDIDATE;
        }
        return esp_peer_send_msg(rtc->pc, &peer_msg);
    }
}

static int signal_closed(void *ctx)
{
    webrtc_t *rtc = (webrtc_t *)ctx;
    rtc->signaling_connected = false;
    // TODO disconnect if peer closed
    esp_peer_disconnect(rtc->pc);
    return 0;
}

int esp_webrtc_open(esp_webrtc_cfg_t *cfg, esp_webrtc_handle_t *handle)
{
    if (cfg == NULL || cfg->signaling_impl == NULL || cfg->peer_impl == NULL) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    webrtc_t *rtc = (webrtc_t *)calloc(1, sizeof(webrtc_t));
    if (rtc == NULL) {
        return ESP_PEER_ERR_NO_MEM;
    }
    // TODO deep copy of other settings
    rtc->rtc_cfg = *cfg;
    rtc->rtc_cfg.peer_cfg.server_num = 0;
    rtc->rtc_cfg.peer_cfg.server_lists = NULL;
    malloc_server_cfg(rtc, cfg->peer_cfg.server_lists, cfg->peer_cfg.server_num);
    if (cfg->peer_cfg.extra_cfg) {
        void *peer_exta_cfg = calloc(1, cfg->peer_cfg.extra_size);
        if (peer_exta_cfg) {
            memcpy(peer_exta_cfg, cfg->peer_cfg.extra_cfg, cfg->peer_cfg.extra_size);
        }
        rtc->rtc_cfg.peer_cfg.extra_cfg = peer_exta_cfg;
    }
    if (cfg->signaling_cfg.extra_cfg) {
        void *signaling_cfg = calloc(1, cfg->signaling_cfg.extra_size);
        if (signaling_cfg) {
            memcpy(signaling_cfg, cfg->signaling_cfg.extra_cfg, cfg->signaling_cfg.extra_size);
        }
        rtc->rtc_cfg.signaling_cfg.extra_cfg = signaling_cfg;
    }
    *handle = rtc;
    return ESP_PEER_ERR_NONE;
}

int esp_webrtc_enable_peer_connection(esp_webrtc_handle_t handle, bool enable)
{
    if (handle == NULL) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    webrtc_t *rtc = (webrtc_t *)handle;
    rtc->pending_connect = !enable;
    int ret = ESP_PEER_ERR_NONE;
    if (rtc->pending_connect == false) {
        if (rtc->pc == NULL) {
            // Create peer connection firstly
            if (rtc->ice_info_loaded == false) {
                // Wait for ice info loaded
                ESP_LOGE(TAG, "ICE info not fetched yet");
                return 0;
            } else {
                ret = start_peer_connection(rtc, &rtc->ice_info);
                if (ret != ESP_PEER_ERR_NONE) {
                    return ret;
                }
            }
        }
        // Signaling already connected
        if (rtc->signaling_connected) {
            ret = esp_peer_new_connection(rtc->pc);
            // Let mainloop resume
            if (rtc->pause) {
                rtc->pause = false;
                SET_WAIT_BITS(PC_RESUME_BIT);
            }
        }
    } else {
        // Close connection better than reconnect?
        rtc->recv_vid_info.codec = ESP_PEER_VIDEO_CODEC_NONE;
        stop_stream(rtc);
        pc_close(rtc);
    }
    return ret;
}

int esp_webrtc_set_event_handler(esp_webrtc_handle_t handle, esp_webrtc_event_handler_t handler, void *ctx)
{
    if (handle == NULL || handler == NULL) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    webrtc_t *rtc = (webrtc_t *)handle;
    rtc->event_handler = handler;
    rtc->ctx = ctx;
    return ESP_PEER_ERR_NONE;
}

int esp_webrtc_set_media_provider(esp_webrtc_handle_t handle, esp_webrtc_media_provider_t *provider)
{
    if (handle == NULL || provider->capture == NULL) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    webrtc_t *rtc = (webrtc_t *)handle;
    rtc->media_provider = *provider;
    // Temp use esp_codec_dev as simple player
    rtc->play_handle = provider->player;
    return ESP_PEER_ERR_NONE;
}

int esp_webrtc_set_no_auto_capture(esp_webrtc_handle_t handle, bool no_auto_capture)
{
    if (handle == NULL) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    webrtc_t *rtc = (webrtc_t *)handle;
    rtc->no_auto_capture = no_auto_capture;
    return ESP_PEER_ERR_NONE;
}

int esp_webrtc_set_audio_bitrate(esp_webrtc_handle_t rtc_handle, uint32_t bitrate)
{
    if (rtc_handle == NULL) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    webrtc_t *rtc = (webrtc_t *)rtc_handle;
    rtc->pre_setting.audio_bitrate = bitrate;
    rtc->pre_setting.preset_mask |= WEBRTC_PRE_SETTING_MASK_AUDIO_BITRATE;
    return pc_apply_capture_pre_setting(rtc, WEBRTC_PRE_SETTING_MASK_AUDIO_BITRATE);
}

int esp_webrtc_set_video_bitrate(esp_webrtc_handle_t rtc_handle, uint32_t bitrate)
{
    if (rtc_handle == NULL) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    webrtc_t *rtc = (webrtc_t *)rtc_handle;
    rtc->pre_setting.video_bitrate = bitrate;
    rtc->pre_setting.preset_mask |= WEBRTC_PRE_SETTING_MASK_VIDEO_BITRATE;
    return pc_apply_capture_pre_setting(rtc, WEBRTC_PRE_SETTING_MASK_VIDEO_BITRATE);
}

int esp_webrtc_start(esp_webrtc_handle_t handle)
{
    if (handle == NULL) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    webrtc_t *rtc = (webrtc_t *)handle;
    if (rtc->signaling) {
        ESP_LOGW(TAG, "Already started");
        return ESP_PEER_ERR_WRONG_STATE;
    }

    // Start signaling firstly
    esp_peer_signaling_cfg_t sig_cfg = {
        .signal_url = rtc->rtc_cfg.signaling_cfg.signal_url,
        .extra_cfg = rtc->rtc_cfg.signaling_cfg.extra_cfg,
        .extra_size = rtc->rtc_cfg.signaling_cfg.extra_size,
        .on_ice_info = signal_ice_received,
        .on_connected = signal_connected,
        .on_msg = signal_new_msg,
        .on_close = signal_closed,
        .ctx = rtc
    };
    int ret = esp_peer_signaling_start(&sig_cfg, rtc->rtc_cfg.signaling_impl, &rtc->signaling);
    if (ret != ESP_PEER_ERR_NONE) {
        ESP_LOGE(TAG, "Fail to start signaling");
        return ret;
    }
    return ret;
}

int esp_webrtc_send_custom_data(esp_webrtc_handle_t handle, esp_webrtc_custom_data_via_t via, uint8_t *data, int size)
{
    if (handle == NULL) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    webrtc_t *rtc = (webrtc_t *)handle;
    if (via == ESP_WEBRTC_CUSTOM_DATA_VIA_SIGNALING) {
        esp_peer_signaling_msg_t msg = {
            .type = ESP_PEER_SIGNALING_MSG_CUSTOMIZED,
            .data = data,
            .size = size,
        };
        return esp_peer_signaling_send_msg(rtc->signaling, &msg);
    }
    if (via == ESP_WEBRTC_CUSTOM_DATA_VIA_DATA_CHANNEL) {
        esp_peer_data_frame_t data_frame = {
            .type = ESP_PEER_DATA_CHANNEL_STRING,
            .data = data,
            .size = size,
        };
        return esp_peer_send_data(rtc->pc, &data_frame);
    }
    return ESP_PEER_ERR_INVALID_ARG;
}

int esp_webrtc_get_peer_connection(esp_webrtc_handle_t handle, esp_peer_handle_t *peer_handle)
{
    if (handle == NULL) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    webrtc_t *rtc = (webrtc_t *)handle;
    if (rtc->pc == NULL) {
        return ESP_PEER_ERR_WRONG_STATE;
    }
    *peer_handle = rtc->pc;
    return ESP_PEER_ERR_NONE;
}

int esp_webrtc_restart(esp_webrtc_handle_t handle)
{
    if (handle == NULL) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    // TODO add restart code, send bye to signaling server
    return ESP_PEER_ERR_NONE;
}

int esp_webrtc_query(esp_webrtc_handle_t handle)
{
    if (handle == NULL) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    webrtc_t *rtc = (webrtc_t *)handle;
    if (rtc->peer_state != ESP_PEER_STATE_CONNECTED) {
        return ESP_PEER_ERR_WRONG_STATE;
    }
    if (rtc->vid_send_num == 0) {
        // Audio only case
        ESP_LOGI(TAG, "Send A:%d [%d:%d] Recv A:%d [%d:%d]",
                (int)rtc->aud_send_pts, (int)rtc->aud_send_num, (int)rtc->aud_send_size,
                (int)rtc->aud_recv_pts, (int)rtc->aud_recv_num, (int)rtc->aud_recv_size);
    } else {
        ESP_LOGI(TAG, "Send A:%d [%d:%d] V:%d [%d:%d] Recv A:%d [%d:%d] Recv V:[%d:%d]",
                (int)rtc->aud_send_pts, (int)rtc->aud_send_num, (int)rtc->aud_send_size,
                (int)rtc->vid_send_pts, (int)rtc->vid_send_num, (int)rtc->vid_send_size,
                (int)rtc->aud_recv_pts, (int)rtc->aud_recv_num, (int)rtc->aud_recv_size,
                (int)rtc->vid_recv_num, (int)rtc->vid_recv_size);
    }
    esp_peer_query(rtc->pc);
    printf("\n");
    // Clear send and receive info
    rtc->vid_send_num = 0;
    rtc->aud_send_num = 0;
    rtc->aud_send_size = 0;
    rtc->vid_send_size = 0;
    rtc->aud_recv_num = 0;
    rtc->aud_recv_size = 0;
    rtc->vid_recv_num = 0;
    rtc->vid_recv_size = 0;
    return ESP_PEER_ERR_NONE;
}

int esp_webrtc_stop(esp_webrtc_handle_t handle)
{
    if (handle == NULL) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    webrtc_t *rtc = (webrtc_t *)handle;
    int ret = 0;
    stop_stream(rtc);
    // TODO stop agent
    pc_close(rtc);
    if (rtc->signaling) {
        esp_peer_signaling_stop(rtc->signaling);
        rtc->signaling = NULL;
    }
    return ret;
}

int esp_webrtc_close(esp_webrtc_handle_t handle)
{
    if (handle == NULL) {
        return ESP_PEER_ERR_INVALID_ARG;
    }
    webrtc_t *rtc = (webrtc_t *)handle;
    esp_webrtc_stop(handle);
    free_server_cfg(rtc);
    SAFE_FREE(rtc->rtc_cfg.peer_cfg.extra_cfg);
    SAFE_FREE(rtc->rtc_cfg.signaling_cfg.extra_cfg);
    SAFE_FREE(rtc->aud_fifo);
    free(rtc);
    return ESP_PEER_ERR_NONE;
}
