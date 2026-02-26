/* Janus publisher WebRTC application code

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "esp_webrtc.h"
#include "common.h"
#include "esp_log.h"
#include "esp_webrtc_defaults.h"

#define TAG  "JANUS_DEMO"

static esp_webrtc_handle_t webrtc;

static int webrtc_event_handler(esp_webrtc_event_t *event, void *ctx)
{
    return 0;
}

int start_webrtc(char *url, uint64_t room_id, const char *pin, const char *token, const char *display, const char *api_secret)
{
    if (network_is_connected() == false) {
        ESP_LOGE(TAG, "Wifi not connected yet");
        return -1;
    }
    if (url == NULL || url[0] == 0 || room_id == 0) {
        ESP_LOGE(TAG, "Janus url/room not configured");
        return -1;
    }
    if (webrtc) {
        esp_webrtc_close(webrtc);
        webrtc = NULL;
    }
    esp_peer_signaling_janus_cfg_t janus_cfg = {
        .room_id = room_id,
        .pin = pin,
        .token = token,
        .display = display,
        .api_secret = api_secret,
    };
    esp_webrtc_cfg_t cfg = {
        .peer_cfg = {
            .audio_info = {
                .codec = ESP_PEER_AUDIO_CODEC_G711A,
            },
            .video_info = {
                .codec = ESP_PEER_VIDEO_CODEC_H264,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = VIDEO_FPS,
            },
            .audio_dir = ESP_PEER_MEDIA_DIR_SEND_ONLY,
            .video_dir = ESP_PEER_MEDIA_DIR_SEND_ONLY,
            .no_auto_reconnect = true,
        },
        .signaling_cfg = {
            .signal_url = url,
            .extra_cfg = &janus_cfg,
            .extra_size = sizeof(janus_cfg),
        },
        .peer_impl = esp_peer_get_default_impl(),
        .signaling_impl = esp_signaling_get_janus_impl(),
    };
    int ret = esp_webrtc_open(&cfg, &webrtc);
    if (ret != 0) {
        ESP_LOGE(TAG, "Fail to open webrtc");
        return ret;
    }
    esp_webrtc_media_provider_t media_provider = {};
    media_sys_get_provider(&media_provider);
    esp_webrtc_set_media_provider(webrtc, &media_provider);
    esp_webrtc_set_event_handler(webrtc, webrtc_event_handler, NULL);
    esp_webrtc_enable_peer_connection(webrtc, true);
    ret = esp_webrtc_start(webrtc);
    if (ret != 0) {
        ESP_LOGE(TAG, "Fail to start webrtc");
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
