/* KVS Master WebRTC application code

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "esp_webrtc.h"
#include "media_lib_os.h"
#include "common.h"
#include "esp_log.h"
#include "esp_webrtc_defaults.h"
#include "esp_peer_default.h"

#define TAG "KVS_MASTER"

static esp_webrtc_handle_t webrtc;

static int webrtc_event_handler(esp_webrtc_event_t *event, void *ctx)
{
    if (event->type == ESP_WEBRTC_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "WebRTC peer connection established");
    } else if (event->type == ESP_WEBRTC_EVENT_CONNECT_FAILED) {
        ESP_LOGE(TAG, "WebRTC connection failed");
    } else if (event->type == ESP_WEBRTC_EVENT_DISCONNECTED) {
        ESP_LOGI(TAG, "WebRTC peer connection disconnected");
    }
    return 0;
}

int start_webrtc(char *url)
{
    if (network_is_connected() == false) {
        ESP_LOGE(TAG, "WiFi not connected yet");
        return -1;
    }
    if (url[0] == 0) {
        ESP_LOGE(TAG, "Room URL not set yet");
        return -1;
    }
    if (webrtc) {
        esp_webrtc_close(webrtc);
        webrtc = NULL;
    }

    esp_peer_default_cfg_t peer_cfg = {
        .agent_recv_timeout = 500,
    };
    esp_webrtc_cfg_t cfg = {
        .peer_cfg = {
            .audio_info = {
#ifdef WEBRTC_SUPPORT_OPUS
                .codec = ESP_PEER_AUDIO_CODEC_OPUS,
                .sample_rate = 16000,
                .channel = 2,
#else
                .codec = ESP_PEER_AUDIO_CODEC_G711A,
#endif
            },
            .video_info = {
                .codec = ESP_PEER_VIDEO_CODEC_H264,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = VIDEO_FPS,
            },
            .audio_dir = ESP_PEER_MEDIA_DIR_SEND_RECV,
            .video_dir = ESP_PEER_MEDIA_DIR_SEND_RECV,
            .enable_data_channel = DATA_CHANNEL_ENABLED,
            // Note: no_auto_reconnect is not set (defaults to false)
            // For KVS, peer connection is created automatically when ICE info is received
            // KVS MASTER receives offer from VIEWER and sends answer (is_initiator=false)
            .extra_cfg = &peer_cfg,
            .extra_size = sizeof(peer_cfg),
        },
        .signaling_cfg = {
            .signal_url = url,
        },
        .peer_impl = esp_peer_get_default_impl(),
        .signaling_impl = esp_signaling_get_kvs_impl(),
    };
    int ret = esp_webrtc_open(&cfg, &webrtc);
    if (ret != 0) {
        ESP_LOGE(TAG, "Fail to open webrtc");
        return ret;
    }
    // Set media provider
    esp_webrtc_media_provider_t media_provider = {};
    media_sys_get_provider(&media_provider);
    esp_webrtc_set_media_provider(webrtc, &media_provider);

    // Set event handler
    esp_webrtc_set_event_handler(webrtc, webrtc_event_handler, NULL);

    // Note: For KVS, we don't call esp_webrtc_enable_peer_connection(webrtc, false)
    // The peer connection will be created automatically when:
    // 1. Signaling connects and ICE info is received
    // 2. An offer arrives from the VIEWER (browser)
    // The peer connection will process the offer and generate an answer automatically

    // Start webrtc
    ret = esp_webrtc_start(webrtc);
    if (ret != 0) {
        ESP_LOGE(TAG, "Fail to start webrtc");
    } else {
        ESP_LOGI(TAG, "WebRTC started successfully");
    }
    return ret;
}

void query_webrtc(void)
{
    if (webrtc) {
        esp_webrtc_query(webrtc);
    }
}

int stop_webrtc(void)
{
    if (webrtc) {
        esp_webrtc_handle_t handle = webrtc;
        webrtc = NULL;
        ESP_LOGI(TAG, "Start to close webrtc %p", handle);
        esp_webrtc_close(handle);
    }
    return 0;
}

