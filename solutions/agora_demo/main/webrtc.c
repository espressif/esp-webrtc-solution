#include "common.h"

#include <stdbool.h>
#include <stdint.h>

#include "agora_signaling.h"
#include "esp_log.h"
#include "esp_peer_default.h"
#include "esp_timer.h"
#include "esp_webrtc.h"

#define TAG "AGORA_WEBRTC"
#define CONNECT_TIMEOUT_US (30LL * 1000 * 1000)

static esp_webrtc_handle_t webrtc;
static volatile bool connected;
static volatile bool restart_required = true;
static int64_t connect_started_us;

static int webrtc_event_handler(esp_webrtc_event_t *event, void *ctx)
{
    (void)ctx;
    switch (event->type) {
        case ESP_WEBRTC_EVENT_CONNECTING:
            ESP_LOGI(TAG, "WebRTC connecting");
            break;
        case ESP_WEBRTC_EVENT_PAIRED:
            ESP_LOGI(TAG, "WebRTC ICE pair selected");
            break;
        case ESP_WEBRTC_EVENT_CONNECTED:
            connected = true;
            restart_required = false;
            connect_started_us = 0;
            ESP_LOGI(TAG, "WebRTC connected; Opus uplink and downlink enabled");
            break;
        case ESP_WEBRTC_EVENT_CONNECT_FAILED:
            connected = false;
            restart_required = true;
            ESP_LOGE(TAG, "WebRTC connection failed");
            break;
        case ESP_WEBRTC_EVENT_DISCONNECTED:
            connected = false;
            restart_required = true;
            ESP_LOGW(TAG, "WebRTC disconnected");
            break;
        default:
            break;
    }
    return 0;
}

int start_webrtc(const char *url, const char *token)
{
    if (!network_is_connected() || url == NULL || url[0] == '\0' ||
        token == NULL || token[0] == '\0') {
        ESP_LOGE(TAG, "Cannot start without network, URL, and bearer token");
        restart_required = true;
        return -1;
    }
    if (webrtc != NULL) {
        stop_webrtc();
    }

    connected = false;
    restart_required = false;
    connect_started_us = esp_timer_get_time();

    esp_peer_default_cfg_t peer_cfg = {
        .rtp_cfg = {
            .audio_recv_jitter = {
                .cache_timeout = 200,
                .resend_delay = 20,
            },
        },
    };
    agora_signaling_cfg_t signaling_cfg = {
        .bearer_token = token,
    };
    esp_webrtc_cfg_t cfg = {
        .peer_cfg = {
            .audio_info = {
                .codec = ESP_PEER_AUDIO_CODEC_OPUS,
                .sample_rate = AGORA_DEMO_AUDIO_SAMPLE_RATE,
                .channel = AGORA_DEMO_AUDIO_CHANNELS,
            },
            .video_info = {
                .codec = ESP_PEER_VIDEO_CODEC_NONE,
            },
            .audio_dir = ESP_PEER_MEDIA_DIR_SEND_RECV,
            .video_dir = ESP_PEER_MEDIA_DIR_NONE,
            .no_auto_reconnect = true,
            .extra_cfg = &peer_cfg,
            .extra_size = sizeof(peer_cfg),
        },
        .signaling_cfg = {
            .signal_url = (char *)url,
            .extra_cfg = &signaling_cfg,
            .extra_size = sizeof(signaling_cfg),
        },
        .peer_impl = esp_peer_get_default_impl(),
        .signaling_impl = agora_signaling_get_impl(),
    };

    ESP_LOGI(TAG, "POST duplex offer endpoint=%s", url);
    int ret = esp_webrtc_open(&cfg, &webrtc);
    if (ret != ESP_PEER_ERR_NONE) {
        webrtc = NULL;
        restart_required = true;
        ESP_LOGE(TAG, "Failed to open WebRTC: %d", ret);
        return ret;
    }

    esp_webrtc_media_provider_t provider = {};
    if (media_sys_get_provider(&provider) != 0 ||
        esp_webrtc_set_media_provider(webrtc, &provider) !=
            ESP_PEER_ERR_NONE ||
        esp_webrtc_set_event_handler(webrtc, webrtc_event_handler, NULL) !=
            ESP_PEER_ERR_NONE) {
        ESP_LOGE(TAG, "Failed to attach WebRTC media or event handler");
        esp_webrtc_close(webrtc);
        webrtc = NULL;
        restart_required = true;
        return -1;
    }

    ret = esp_webrtc_start(webrtc);
    if (ret != ESP_PEER_ERR_NONE) {
        ESP_LOGE(TAG, "Failed to start WHIP signaling: %d", ret);
        esp_webrtc_close(webrtc);
        webrtc = NULL;
        restart_required = true;
        connect_started_us = 0;
    }
    return ret;
}

void query_webrtc(void)
{
    if (webrtc != NULL) {
        esp_webrtc_query(webrtc);
    }
}

bool webrtc_needs_restart(void)
{
    if (webrtc == NULL || restart_required) {
        return true;
    }
    if (!connected && connect_started_us != 0 &&
        esp_timer_get_time() - connect_started_us >= CONNECT_TIMEOUT_US) {
        ESP_LOGW(TAG, "Connection timed out; a fresh session is required");
        restart_required = true;
        return true;
    }
    return false;
}

int stop_webrtc(void)
{
    if (webrtc != NULL) {
        esp_webrtc_handle_t handle = webrtc;
        webrtc = NULL;
        ESP_LOGI(TAG, "Closing WebRTC session");
        esp_webrtc_close(handle);
    }
    connected = false;
    restart_required = true;
    connect_started_us = 0;
    return 0;
}
