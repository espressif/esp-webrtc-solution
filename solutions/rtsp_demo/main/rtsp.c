/* RTSP server or pusher test

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include "media_lib_netif.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_capture.h"
#include "esp_capture_sink.h"
#include "esp_gmf_video_enc.h"
#include "esp_capture_advance.h"
#include "common.h"
#include "rtsp.h"
#include "esp_log.h"

static const char *TAG = "RTSP_SERVICE";

static esp_rtsp_mode_t rtsp_mode;
static esp_rtsp_handle_t         rtsp_handle;
static esp_capture_sink_handle_t rtsp_sink;

static char *_get_network_ip()
{
    media_lib_ipv4_info_t ip_info;
    media_lib_netif_get_ipv4_info(MEDIA_LIB_NET_TYPE_STA, &ip_info);
    return media_lib_ipv4_ntoa(&ip_info.ip);
}

static int _esp_rtsp_state_handler(esp_rtsp_state_t state, void *ctx)
{
    ESP_LOGD(TAG, "_esp_rtsp_state_handler state %d", state);
    switch ((int)state) {
        case RTSP_STATE_SETUP:
            ESP_LOGI(TAG, "RTSP_STATE_SETUP");
            break;
        case RTSP_STATE_PLAY:
            if (rtsp_mode == RTSP_CLIENT_PLAY) {

            } else {
                esp_capture_sink_enable(rtsp_sink, ESP_CAPTURE_RUN_MODE_ALWAYS);
            }

            ESP_LOGI(TAG, "RTSP_STATE_PLAY");
            break;
        case RTSP_STATE_TEARDOWN:
            if (rtsp_mode == RTSP_CLIENT_PLAY) {
            }
            else {
                esp_capture_sink_enable(rtsp_sink, ESP_CAPTURE_RUN_MODE_DISABLE);
            }
            ESP_LOGI(TAG, "RTSP_STATE_TEARDOWN");
            break;
    }
    return 0;
}

static int _send_audio(unsigned char *data, int len, uint32_t* pts, void *ctx)
{
    if (rtsp_sink == NULL) {
        return -1;
    }
    esp_capture_stream_frame_t frame = {
        .stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO,
    };
    if (esp_capture_sink_acquire_frame(rtsp_sink, &frame, true) == ESP_CAPTURE_ERR_OK) {
        if (len < frame.size) {
            ESP_LOGE(TAG, "Audio frame size not enough for %d", (int)frame.size);
            esp_capture_sink_release_frame(rtsp_sink, &frame);
            return -1;
        }
        memcpy(data, frame.data, frame.size);
        esp_capture_sink_release_frame(rtsp_sink, &frame);
        *pts = frame.pts;
        return frame.size;
    }
    return 0;
}

static int _send_video(unsigned char *data, unsigned int *len, uint32_t *pts, void *ctx)
{
    if (rtsp_sink == NULL) {
        return -1;
    }
    esp_capture_stream_frame_t frame = {
        .stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO,
    };
    if (esp_capture_sink_acquire_frame(rtsp_sink, &frame, false) == ESP_CAPTURE_ERR_OK) {
        if (*len < frame.size) {
            esp_capture_sink_release_frame(rtsp_sink, &frame);
            ESP_LOGE(TAG, "Video frame size not enough for %d", (int)frame.size);
            return -1;
        }
        memcpy(data, frame.data, frame.size);
        esp_capture_sink_release_frame(rtsp_sink, &frame);
        *len = frame.size;
        *pts = frame.pts;
        return ESP_OK;
    }
    *len = 0;
    return ESP_OK;
}

static int _receive_audio(unsigned char *data, int len, void *ctx)
{
    return 0;
}

static int _receive_video(unsigned char *data, int len, void *ctx)
{
    return 0;
}

static int _stream_info_cb(esp_rtsp_aud_info_t* aud_info, esp_rtsp_video_info_t* vid_info, void *ctx)
{
    return 0;
}

rtsp_payload_codec_t get_rtsp_codec(uint32_t format_id)
{
    switch (format_id) {
        case ESP_CAPTURE_FMT_ID_H264:
            return RTSP_VCODEC_H264;
        case ESP_CAPTURE_FMT_ID_MJPEG:
            return RTSP_VCODEC_MJPEG;
        case ESP_CAPTURE_FMT_ID_G711A:
            return RTSP_ACODEC_G711A;
        case ESP_CAPTURE_FMT_ID_AAC:
            return RTSP_ACODEC_AAC;
        default:
            return (rtsp_payload_codec_t)0xFF;
    }
}

static esp_capture_err_t capture_event_hdlr(esp_capture_event_t event, void *ctx)
{
    switch (event) {
        default:
            break;
        case ESP_CAPTURE_EVENT_VIDEO_PIPELINE_BUILT: {
            // Do extra setting for video pipeline here
            if (VIDEO_FORMAT  == ESP_CAPTURE_FMT_ID_H264) {
                // Setting for GOP use video encoder element
                esp_gmf_element_handle_t venc_hd = NULL;
                esp_capture_sink_get_element_by_tag(rtsp_sink, ESP_CAPTURE_STREAM_TYPE_VIDEO, "vid_enc", &venc_hd);
                if (venc_hd) {
                    esp_gmf_video_enc_set_gop(venc_hd, VIDEO_FPS * 2);
                }
            }
            break;
        }
    }
    return ESP_CAPTURE_ERR_OK;
}

int start_rtsp(esp_rtsp_mode_t mode, const char *uri)
{
    if (rtsp_handle) {
        ESP_LOGI(TAG, "Already started");
        return 0;
    }
    esp_webrtc_media_provider_t provider = {};
    media_sys_get_provider(&provider);
    if (provider.capture == NULL) {
        return -1;
    }
     esp_capture_sink_cfg_t sink_cfg = {
        .audio_info = {
            .format_id = AUDIO_FORMAT,
            .sample_rate = AUDIO_SAMPLE_RATE,
            .channel = AUDIO_CHANNEL,
            .bits_per_sample = 16,
        },
        .video_info = {
            .format_id = VIDEO_FORMAT,
            .width = VIDEO_WIDTH,
            .height = VIDEO_HEIGHT,
            .fps = VIDEO_FPS,
        },
    };
    esp_capture_sink_setup(provider.capture, 0, &sink_cfg, &rtsp_sink);
    esp_capture_sink_enable(rtsp_sink, ESP_CAPTURE_RUN_MODE_DISABLE);
    esp_capture_set_event_cb(provider.capture, capture_event_hdlr, NULL);
    if (esp_capture_start(provider.capture) != 0) {
        ESP_LOGE(TAG, "Fail to start capture");
        return -1;
    }
    esp_rtsp_video_info_t vcodec_info = {
        .vcodec = get_rtsp_codec(VIDEO_FORMAT),
        .width = VIDEO_WIDTH,
        .height = VIDEO_HEIGHT,
        .fps = VIDEO_FPS + 5,
        .len = MAX_VIDEO_FRAME_SIZE,
    };
    esp_rtsp_data_cb_t data_cb = {
        .send_audio = _send_audio,
        .receive_audio = _receive_audio,
        .receive_video = _receive_video,
        .send_video = _send_video,
    };
    esp_rtsp_config_t rtsp_config = {
        .uri = uri,
        .mode = mode,
        .data_cb = &data_cb,
        .audio_enable = true,
        .video_enable = true,
        .acodec = get_rtsp_codec(AUDIO_FORMAT),
        .aud_sample_rate = AUDIO_SAMPLE_RATE,
        .aud_channel = AUDIO_CHANNEL,
        .aud_frame_size = 2048,
        .video_info = &vcodec_info,
        .local_addr = _get_network_ip(),
        .stack_size = 15*1024,
        .task_prio = 15,
        .state = _esp_rtsp_state_handler,
        .trans = RTSP_TRANSPORT_TCP,
    };
    rtsp_mode = mode;
    if (mode == RTSP_CLIENT_PLAY) {
        data_cb.receive_audio = _receive_audio;
        data_cb.stream_codec = _stream_info_cb;
        data_cb.send_audio = NULL;
        data_cb.send_video = NULL;
        rtsp_config.video_enable = false;
        rtsp_config.trans = RTSP_TRANSPORT_UDP;
    }
    if (mode == RTSP_SERVER) {
        rtsp_config.local_port = 8554;
        rtsp_handle = esp_rtsp_server_start(&rtsp_config);
    } else {
        rtsp_handle = esp_rtsp_client_start(&rtsp_config);
    }
    if (rtsp_handle == NULL) {
        ESP_LOGE(TAG, "Fail to start RTSP");
        return -1;
    }
    return 0;
}

int stop_rtsp(void)
{
    if (rtsp_handle) {
        if (rtsp_mode == RTSP_SERVER) {
            esp_rtsp_server_stop(rtsp_handle);
        } else {
            esp_rtsp_client_stop(rtsp_handle);
        }
        rtsp_handle = NULL;
    }
    esp_webrtc_media_provider_t provider = {};
    media_sys_get_provider(&provider);
    if (provider.capture) {
        esp_capture_stop(provider.capture);
    }
    return 0;
}
