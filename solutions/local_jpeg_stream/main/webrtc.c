/* Local JPEG Stream WebRTC application

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "esp_webrtc.h"
#include "media_lib_os.h"
#include "driver/gpio.h"
#include "common.h"
#include "esp_log.h"
#include "esp_webrtc_defaults.h"
#include "esp_peer_default.h"
#include "webrtc_http_server.h"
#include "esp_peer.h"
#include <string.h>

#define TAG "JPEG_STREAM"

#define JPEG_STREAM_RING_CMD          "RING"
#define JPEG_STREAM_CALL_ACCEPTED_CMD "ACCEPT_CALL"
#define JPEG_STREAM_CALL_DENIED_CMD   "DENY_CALL"
#define JPEG_STREAM_TIMEOUT           30000
#define JPEG_DC_LABEL                 "video_data"

#define SAME_STR(a, b) (strncmp(a, b, sizeof(b) - 1) == 0)
#define SEND_CMD(webrtc, cmd) \
    esp_webrtc_send_custom_data(webrtc, ESP_WEBRTC_CUSTOM_DATA_VIA_SIGNALING, (uint8_t *)cmd, strlen(cmd))

#define RUN_ASYNC(name, body)           \
    void run_async##name(void *arg)     \
    {                                   \
        body;                           \
        media_lib_thread_destroy(NULL); \
    }                                   \
    media_lib_thread_create_from_scheduler(NULL, #name, run_async##name, NULL);

typedef enum {
    JPEG_STREAM_STATE_NONE,
    JPEG_STREAM_STATE_RINGING,
    JPEG_STREAM_STATE_CONNECTING,
    JPEG_STREAM_STATE_CONNECTED,
} jpeg_stream_state_t;

typedef enum {
    JPEG_STREAM_TONE_RING,
    JPEG_STREAM_TONE_JOIN_SUCCESS,
} jpeg_stream_tone_type_t;

typedef struct {
    const uint8_t *start;
    const uint8_t *end;
    int            duration;
} jpeg_stream_tone_data_t;

static esp_webrtc_handle_t webrtc;
static bool                jpeg_stream_initiator;
static jpeg_stream_state_t jpeg_stream_state;
static bool                monitor_key;
static bool                video_dc_created;

extern const uint8_t ring_music_start[] asm("_binary_ring_aac_start");
extern const uint8_t ring_music_end[] asm("_binary_ring_aac_end");
extern const uint8_t join_music_start[] asm("_binary_join_aac_start");
extern const uint8_t join_music_end[] asm("_binary_join_aac_end");

static int play_tone(jpeg_stream_tone_type_t type)
{
    jpeg_stream_tone_data_t tone_data[] = {
        { ring_music_start, ring_music_end, 4000 },
        { join_music_start, join_music_end, 0 },
    };
    if (type >= sizeof(tone_data) / sizeof(tone_data[0])) {
        return 0;
    }
    return play_music(tone_data[type].start, (int)(tone_data[type].end - tone_data[type].start), tone_data[type].duration);
}

static void jpeg_stream_change_state(jpeg_stream_state_t state)
{
    jpeg_stream_state = state;
    if (state == JPEG_STREAM_STATE_CONNECTING || state == JPEG_STREAM_STATE_NONE) {
        stop_music();
    }
    if (state == JPEG_STREAM_STATE_NONE) {
        jpeg_stream_initiator = false;
        video_dc_created = false;
    }
}

static int create_video_data_channel(void)
{
    if (webrtc == NULL || video_dc_created) {
        return 0;
    }
    esp_peer_handle_t peer = NULL;
    if (esp_webrtc_get_peer_connection(webrtc, &peer) != ESP_PEER_ERR_NONE || peer == NULL) {
        return -1;
    }
    esp_peer_data_channel_cfg_t ch_cfg = {
        .type = ESP_PEER_DATA_CHANNEL_PARTIAL_RELIABLE_RETX,
        .ordered = false,
        .label = JPEG_DC_LABEL,
        .max_retransmit_count = 1,
    };
    int ret = esp_peer_create_data_channel(peer, &ch_cfg);
    if (ret == ESP_PEER_ERR_NONE) {
        video_dc_created = true;
        ESP_LOGI(TAG, "Created %s DC (maxRetransmit=0, unordered)", JPEG_DC_LABEL);
    } else {
        ESP_LOGW(TAG, "Create data channel failed: %d", ret);
    }
    return ret;
}

static int jpeg_stream_on_cmd(esp_webrtc_custom_data_via_t via, uint8_t *data, int size, void *ctx)
{
    if (via != ESP_WEBRTC_CUSTOM_DATA_VIA_SIGNALING) {
        return 0;
    }
    if (size == 0 || webrtc == NULL) {
        return 0;
    }
    ESP_LOGI(TAG, "Receive command %.*s", size, (char *)data);
    const char *cmd = (const char *)data;
    if (SAME_STR(cmd, JPEG_STREAM_RING_CMD)) {
        if (jpeg_stream_state < JPEG_STREAM_STATE_CONNECTING) {
            jpeg_stream_change_state(JPEG_STREAM_STATE_CONNECTING);
            RUN_ASYNC(ring, { play_tone(JPEG_STREAM_TONE_RING); });
        }
    } else if (SAME_STR(cmd, JPEG_STREAM_CALL_ACCEPTED_CMD)) {
        jpeg_stream_change_state(JPEG_STREAM_STATE_CONNECTING);
        esp_webrtc_enable_peer_connection(webrtc, true);
    } else if (SAME_STR(cmd, JPEG_STREAM_CALL_DENIED_CMD)) {
        esp_webrtc_enable_peer_connection(webrtc, false);
        jpeg_stream_change_state(JPEG_STREAM_STATE_NONE);
    }
    return 0;
}

static int webrtc_event_handler(esp_webrtc_event_t *event, void *ctx)
{
    if (event->type == ESP_WEBRTC_EVENT_CONNECTED) {
        jpeg_stream_change_state(JPEG_STREAM_STATE_CONNECTED);
        ESP_LOGI(TAG, "Peer connected");
    } else if (event->type == ESP_WEBRTC_EVENT_CONNECT_FAILED || event->type == ESP_WEBRTC_EVENT_DISCONNECTED) {
        jpeg_stream_change_state(JPEG_STREAM_STATE_NONE);
    } else if (event->type == ESP_WEBRTC_EVENT_DATA_CHANNEL_CONNECTED) {
        // SCTP connected: create unreliable video DC as client
        create_video_data_channel();
    } else if (event->type == ESP_WEBRTC_EVENT_DATA_CHANNEL_OPENED) {
        ESP_LOGI(TAG, "Video data channel opened");
    }
    return 0;
}

void send_cmd(char *cmd)
{
    if (webrtc == NULL) {
        return;
    }
    if (cmd && SAME_STR(cmd, "ring")) {
        SEND_CMD(webrtc, JPEG_STREAM_RING_CMD);
        jpeg_stream_initiator = true;
        jpeg_stream_change_state(JPEG_STREAM_STATE_RINGING);
        play_tone(JPEG_STREAM_TONE_RING);
        ESP_LOGI(TAG, "Ring sent");
    } else if (cmd && SAME_STR(cmd, "accept")) {
        SEND_CMD(webrtc, JPEG_STREAM_CALL_ACCEPTED_CMD);
        jpeg_stream_change_state(JPEG_STREAM_STATE_CONNECTING);
        esp_webrtc_enable_peer_connection(webrtc, true);
    } else if (cmd && SAME_STR(cmd, "deny")) {
        SEND_CMD(webrtc, JPEG_STREAM_CALL_DENIED_CMD);
        esp_webrtc_enable_peer_connection(webrtc, false);
        jpeg_stream_change_state(JPEG_STREAM_STATE_NONE);
    } else {
        SEND_CMD(webrtc, JPEG_STREAM_RING_CMD);
        jpeg_stream_initiator = true;
        jpeg_stream_change_state(JPEG_STREAM_STATE_RINGING);
    }
}

static void key_pressed(void)
{
    if (jpeg_stream_state < JPEG_STREAM_STATE_CONNECTING) {
        send_cmd("ring");
    } else if (jpeg_stream_state == JPEG_STREAM_STATE_CONNECTING && jpeg_stream_initiator == false) {
        send_cmd("accept");
        ESP_LOGI(TAG, "Accept call");
    } else if (jpeg_stream_state == JPEG_STREAM_STATE_CONNECTED) {
        send_cmd("deny");
        ESP_LOGI(TAG, "Hang off");
    }
}

static void key_monitor_thread(void *arg)
{
    gpio_config_t io_conf;
    memset(&io_conf, 0, sizeof(io_conf));
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = BIT64(JPEG_STREAM_RING_BUTTON);
    io_conf.pull_down_en = 1;
    gpio_config(&io_conf);

    media_lib_thread_sleep(50);
    int last_level = gpio_get_level(JPEG_STREAM_RING_BUTTON);
    int init_level = last_level;
    uint32_t ring_timeout = 0;

    while (monitor_key) {
        media_lib_thread_sleep(50);
        if (jpeg_stream_state == JPEG_STREAM_STATE_RINGING || jpeg_stream_state == JPEG_STREAM_STATE_CONNECTING) {
            ring_timeout += 50;
            if (ring_timeout > JPEG_STREAM_TIMEOUT) {
                jpeg_stream_change_state(JPEG_STREAM_STATE_NONE);
                ring_timeout = 0;
            }
        } else {
            ring_timeout = 0;
        }
        int level = gpio_get_level(JPEG_STREAM_RING_BUTTON);
        if (level != last_level) {
            last_level = level;
            if (level != init_level) {
                key_pressed();
            }
        }
    }
    media_lib_thread_destroy(NULL);
}

int start_webrtc(char *url)
{
    if (network_is_connected() == false) {
        ESP_LOGE(TAG, "Wifi not connected yet");
        return -1;
    }
    if (webrtc) {
        esp_webrtc_close(webrtc);
        webrtc = NULL;
    }
    monitor_key = true;
    video_dc_created = false;
    media_lib_thread_handle_t key_thread;
    media_lib_thread_create_from_scheduler(&key_thread, "Key", key_monitor_thread, NULL);

    esp_peer_default_cfg_t peer_cfg = {
        .agent_recv_timeout = 500,
        .data_ch_cfg = {
            .send_cache_size = JPEG_DC_SEND_CACHE_SIZE,
            .recv_cache_size = JPEG_DC_RECV_CACHE_SIZE,
        },
    };

    esp_webrtc_cfg_t cfg = {
        .peer_cfg = {
            .audio_info = {
                .codec = ESP_PEER_AUDIO_CODEC_G711A,
                .sample_rate = 8000,
                .channel = 1,
            },
            .video_info = {
                .codec = ESP_PEER_VIDEO_CODEC_MJPEG,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = VIDEO_FPS,
            },
            .audio_dir = ESP_PEER_MEDIA_DIR_SEND_RECV,
#if VIDEO_SEND_RECV
            .video_dir = ESP_PEER_MEDIA_DIR_SEND_RECV,
#else
            .video_dir = ESP_PEER_MEDIA_DIR_SEND_ONLY,
#endif
            .on_custom_data = jpeg_stream_on_cmd,
            .enable_data_channel = DATA_CHANNEL_ENABLED,
            .manual_ch_create = true,
            .no_auto_reconnect = true,
            .video_over_data_channel = true,
            .video_dc_chunked = true,
            .video_dc_chunk_size = 10000,
            .extra_cfg = &peer_cfg,
            .extra_size = sizeof(peer_cfg),
        },
        .signaling_cfg = {
            .signal_url = url,
        },
        .peer_impl = esp_peer_get_default_impl(),
        .signaling_impl = esp_signaling_get_http_impl(),
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
    esp_webrtc_enable_peer_connection(webrtc, false);

    ret = esp_webrtc_start(webrtc);
    if (ret != 0) {
        ESP_LOGE(TAG, "Fail to start webrtc");
    } else {
        play_tone(JPEG_STREAM_TONE_JOIN_SUCCESS);
        ESP_LOGI(TAG, "JPEG stream ready %dx%d@%dfps chunked=%d",
                 VIDEO_WIDTH, VIDEO_HEIGHT, VIDEO_FPS, cfg.peer_cfg.video_dc_chunked);
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
        monitor_key = false;
        esp_webrtc_handle_t handle = webrtc;
        webrtc = NULL;
        video_dc_created = false;
        ESP_LOGI(TAG, "Start to close webrtc %p", handle);
        esp_webrtc_close(handle);
    }
    return 0;
}
