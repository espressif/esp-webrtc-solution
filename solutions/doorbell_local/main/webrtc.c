/* DoorBell WebRTC application code

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

#define TAG "DOOR_BELL"

// Customized commands
#define DOOR_BELL_OPEN_DOOR_CMD     "OPEN_DOOR"
#define DOOR_BELL_DOOR_OPENED_CMD   "DOOR_OPENED"
#define DOOR_BELL_RING_CMD          "RING"
#define DOOR_BELL_CALL_ACCEPTED_CMD "ACCEPT_CALL"
#define DOOR_BELL_CALL_DENIED_CMD   "DENY_CALL"

#define SAME_STR(a, b) (strncmp(a, b, sizeof(b) - 1) == 0)
#define SEND_CMD(webrtc, cmd) \
    esp_webrtc_send_custom_data(webrtc, ESP_WEBRTC_CUSTOM_DATA_VIA_SIGNALING, (uint8_t *)cmd, strlen(cmd))
#define ELEMS(arr) sizeof(arr)/sizeof(arr[0])

typedef enum {
    DOOR_BELL_STATE_NONE,
    DOOR_BELL_STATE_RINGING,
    DOOR_BELL_STATE_CONNECTING,
    DOOR_BELL_STATE_CONNECTED,
} door_bell_state_t;

typedef enum {
    DOOR_BELL_TONE_RING,
    DOOR_BELL_TONE_OPEN_DOOR,
    DOOR_BELL_TONE_JOIN_SUCCESS,
} door_bell_tone_type_t;

typedef struct {
    const uint8_t *start;
    const uint8_t *end;
    int            duration;
} door_bell_tone_data_t;

typedef struct {
    esp_peer_data_channel_info_t info;
    int send_count;
    int recv_count;
    bool used;
} user_data_ch_t;

static esp_webrtc_handle_t webrtc;
static door_bell_state_t   door_bell_state;
static bool                monitor_key;
static user_data_ch_t      user_ch[2];
static bool data_running = false;

extern const uint8_t ring_music_start[] asm("_binary_ring_aac_start");
extern const uint8_t ring_music_end[] asm("_binary_ring_aac_end");
extern const uint8_t open_music_start[] asm("_binary_open_aac_start");
extern const uint8_t open_music_end[] asm("_binary_open_aac_end");
extern const uint8_t join_music_start[] asm("_binary_join_aac_start");
extern const uint8_t join_music_end[] asm("_binary_join_aac_end");

static int play_tone(door_bell_tone_type_t type)
{
    door_bell_tone_data_t tone_data[] = {
        { ring_music_start, ring_music_end, 4000 },
        { open_music_start, open_music_end, 0 },
        { join_music_start, join_music_end, 0 },
    };
    if (type >= sizeof(tone_data) / sizeof(tone_data[0])) {
        return 0;
    }
    return play_music(tone_data[type].start, (int)(tone_data[type].end - tone_data[type].start), tone_data[type].duration);
}

int play_tone_int(int t)
{
    return play_tone((door_bell_tone_type_t)t);
}

static void door_bell_change_state(door_bell_state_t state)
{
    door_bell_state = state;
    if (state == DOOR_BELL_STATE_CONNECTING || state == DOOR_BELL_STATE_NONE) {
        stop_music();
    }
}

#ifdef CONFIG_DOORBELL_SUPPORT_PEDESTRIAN_DETECT
static esp_capture_sink_handle_t detect_sink = NULL;

esp_capture_sink_handle_t get_detect_sink(void)
{
    return detect_sink;
}

int pedestrian_detected(esp_capture_rgn_t *rgn, void *ctx)
{
    char region_str[128];
    snprintf(region_str, sizeof(region_str) - 1, "PEDESTRIAN_DETECTED");
    SEND_CMD(webrtc, region_str);
    return 0;
}
#endif

static int door_bell_on_cmd(esp_webrtc_custom_data_via_t via, uint8_t *data, int size, void *ctx)
{
    // Only handle signaling message
    if (via != ESP_WEBRTC_CUSTOM_DATA_VIA_SIGNALING) {
        return 0;
    }
    if (size == 0 || webrtc == NULL) {
        return 0;
    }
    ESP_LOGI(TAG, "Receive command %.*s", size, (char *)data);
    const char *cmd = (const char *)data;
    if (SAME_STR(cmd, DOOR_BELL_OPEN_DOOR_CMD)) {
        // Reply with door OPENED
        SEND_CMD(webrtc, DOOR_BELL_DOOR_OPENED_CMD);
        // Only play tome when connection not build up
        if (door_bell_state < DOOR_BELL_STATE_CONNECTING) {
            play_tone(DOOR_BELL_TONE_OPEN_DOOR);
        }
    } else if (SAME_STR(cmd, DOOR_BELL_CALL_ACCEPTED_CMD)) {
        door_bell_change_state(DOOR_BELL_STATE_CONNECTING);
        esp_webrtc_enable_peer_connection(webrtc, true);
    } else if (SAME_STR(cmd, DOOR_BELL_CALL_DENIED_CMD)) {
        esp_webrtc_enable_peer_connection(webrtc, false);
        door_bell_change_state(DOOR_BELL_STATE_NONE);
    }
    return 0;
}

int close_data_channel(int id)
{
    if (id < ELEMS(user_ch) && user_ch[id].used) {
        ESP_LOGI(TAG, "Start to Close data channel %s", user_ch[id].info.label);
        esp_peer_handle_t peer_handle = NULL;
        esp_webrtc_get_peer_connection(webrtc, &peer_handle);
        esp_peer_close_data_channel(peer_handle, user_ch[id].info.label);
    }
    return 0;
}

static void add_channel(esp_peer_data_channel_info_t *ch)
{
    for (int i = 0; i < ELEMS(user_ch); i++) {
        if (user_ch[i].used == false) {
            user_ch[i].used = true;
            char def_label[2] = "0";
            def_label[0] += i;
            user_ch[i].info.label = strdup(ch->label ? ch->label : def_label);
            user_ch[i].info.stream_id = ch->stream_id;
            user_ch[i].send_count = 0;
            user_ch[i].recv_count = 0;
            break;
        }
    }
}

user_data_ch_t *get_channel(uint16_t stream_id)
{
    for (int i = 0; i < ELEMS(user_ch); i++) {
        if (user_ch[i].used && user_ch[i].info.stream_id == stream_id) {
            return &user_ch[i];
        }
    }
    return NULL;
}

static void remove_channel(esp_peer_data_channel_info_t *ch)
{
    for (int i = 0; i < ELEMS(user_ch); i++) {
        if (user_ch[i].used && user_ch[i].info.stream_id == ch->stream_id) {
            user_ch[i].used = false;
            ESP_LOGI(TAG, "Removed %s id %d finished", user_ch[i].info.label, ch->stream_id);
            free((char*)user_ch[i].info.label);
            user_ch[i].info.label = NULL;
            break;
        }
    }
}

#define CNT_TO_CHAR(c) (((c) & 0xFF) % 94 + 33)

static void data_thread_hdlr(void *arg)
{
    data_running = true;
#define SEND_PERIOD 1000
    int time = 0;
    int last_send_time = -SEND_PERIOD;
    int str_len = 8192;
    char *str = calloc(1, 8192);
    while (webrtc) {
        bool need_send = false;
        for (int i = 0; i < ELEMS(user_ch); i++) {
            if (user_ch[i].used && time >= last_send_time + SEND_PERIOD) {
                need_send = true;
            }
        }
        if (need_send) {
            for (int i = 0; i < ELEMS(user_ch); i++) {
                if (user_ch[i].used == false) {
                    continue;
                }
                int n = snprintf(str, str_len -1 , "Send to %s count %d\n",
                    user_ch[i].info.label, user_ch[i].send_count);
                memset(str + n, CNT_TO_CHAR(user_ch[i].send_count), str_len - n);
                user_ch[i].send_count++;
                esp_peer_data_frame_t data_frame = {
                    .type = ESP_PEER_DATA_CHANNEL_STRING,
                    .stream_id = user_ch[i].info.stream_id,
                    .data = (uint8_t*)str,
                    .size = str_len,
                };
                esp_peer_handle_t peer_handle = NULL;
                esp_webrtc_get_peer_connection(webrtc, &peer_handle);
                esp_peer_send_data(peer_handle, &data_frame);
                printf("Send string %.*s", n, str);
            }
            last_send_time = time;
        }
        media_lib_thread_sleep(50);
        time += 50;;
    }
    data_running = false;
    media_lib_thread_destroy(NULL);
}

static int webrtc_data_channel_opened(esp_peer_data_channel_info_t *ch, void *ctx)
{
    ESP_LOGI(TAG, "Channel %s opened stream id %d", ch->label ? ch->label : "NULL", ch->stream_id);
    add_channel(ch);
    return 0;
}

static bool verify_data(uint8_t *data, int size)
{
    char *line_end = strchr((char*)data, '\n');
    char * count = strstr((char*)data, "the ");
    if (line_end == NULL || count == NULL) {
        return false;
    }
    int n = (line_end - (char*)data) + 1;
    int left_size = size - n;
    int send_count = atoi(count + strlen("the "));
    uint8_t expect = CNT_TO_CHAR(send_count);
    for (int i = 0; i < left_size; i++) {
        if (data[n + i] != expect) {
            return false;
        }
    }
    return true;
}

static int webrtc_on_data(esp_peer_data_frame_t *frame, void *ctx)
{
    user_data_ch_t *ch = get_channel(frame->stream_id);
    char *line_end = strchr((char*)frame->data, '\n');
    if (line_end == NULL) {
        return -1;
    }
    int str_len = (line_end - (char*)frame->data);
    bool verified = verify_data(frame->data, frame->size);
    if (verified == false) {
        ESP_LOGE(TAG, "Get data label %s verify:%d data: %.*s",
            ch && ch->info.label ? ch->info.label : "NULL",
            verified,
            str_len, (char *)frame->data);
    } else {
        ESP_LOGI(TAG, "Get data label %s verify:%d data: %.*s",
            ch && ch->info.label ? ch->info.label : "NULL",
            verified,
            str_len, (char *)frame->data);
    }
    return 0;
}

static int webrtc_data_channel_closed(esp_peer_data_channel_info_t *ch, void *ctx)
{
    remove_channel(ch);
    return 0;
}

static int webrtc_event_handler(esp_webrtc_event_t *event, void *ctx)
{
    if (event->type == ESP_WEBRTC_EVENT_CONNECTED) {
        door_bell_change_state(DOOR_BELL_STATE_CONNECTED);
    } else if (event->type == ESP_WEBRTC_EVENT_CONNECT_FAILED || event->type == ESP_WEBRTC_EVENT_DISCONNECTED) {
        door_bell_change_state(DOOR_BELL_STATE_NONE);
    }
    return 0;
}

void send_cmd(char *cmd)
{
    if (SAME_STR(cmd, "ring")) {
        SEND_CMD(webrtc, DOOR_BELL_RING_CMD);
        ESP_LOGI(TAG, "Ring button on state %d", door_bell_state);
        if (door_bell_state < DOOR_BELL_STATE_CONNECTING) {
            door_bell_state = DOOR_BELL_STATE_RINGING;
            play_tone(DOOR_BELL_TONE_RING);
        }
    }
}

static void key_monitor_thread(void *arg)
{
    gpio_config_t io_conf;
    memset(&io_conf, 0, sizeof(io_conf));
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = BIT64(DOOR_BELL_RING_BUTTON);
    io_conf.pull_down_en = 1;
    esp_err_t ret = 0;
    ret |= gpio_config(&io_conf);

    media_lib_thread_sleep(50);
    int last_level = gpio_get_level(DOOR_BELL_RING_BUTTON);
    int init_level = last_level;

    while (monitor_key) {
        media_lib_thread_sleep(50);
        int level = gpio_get_level(DOOR_BELL_RING_BUTTON);
        if (level != last_level) {
            last_level = level;
            if (level != init_level) {
                send_cmd("ring");
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
    media_lib_thread_handle_t key_thread;
    media_lib_thread_create_from_scheduler(&key_thread, "Key", key_monitor_thread, NULL);

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
                .sample_rate = 8000,
                .channel = 1,
#endif
            },
            .video_info = {
                .codec = ESP_PEER_VIDEO_CODEC_H264,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = VIDEO_FPS,
            },
            .audio_dir = ESP_PEER_MEDIA_DIR_SEND_RECV,
            .video_dir = ESP_PEER_MEDIA_DIR_SEND_ONLY,
            .on_custom_data = door_bell_on_cmd,
            // Add following data channel callback for more accurate control over data channel
            .on_channel_open = webrtc_data_channel_opened,
            .on_data = webrtc_on_data,
            .on_channel_close = webrtc_data_channel_closed,
            .enable_data_channel = true,
            .manual_ch_create = true, // If work as SCTP client disable create data channel automatically
            .no_auto_reconnect = true, // No auto connect peer when signaling connected
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
    // Set media provider
    esp_webrtc_media_provider_t media_provider = {};
    media_sys_get_provider(&media_provider);
    esp_webrtc_set_media_provider(webrtc, &media_provider);

#ifdef CONFIG_DOORBELL_SUPPORT_PEDESTRIAN_DETECT
    // Disable auto capture
    esp_webrtc_set_no_auto_capture(webrtc, true);
    // Pre-configuration for all sink
    esp_capture_sink_cfg_t sink_cfg = {
        .audio_info = {
#ifdef WEBRTC_SUPPORT_OPUS
            .format_id = ESP_CAPTURE_FMT_ID_OPUS,
#else
            .format_id = ESP_CAPTURE_FMT_ID_G711A,
#endif
            .sample_rate = cfg.peer_cfg.audio_info.sample_rate,
            .channel = cfg.peer_cfg.audio_info.channel,
            .bits_per_sample = 16,
        },
        .video_info = {
            .format_id = ESP_CAPTURE_FMT_ID_H264,
            .width = cfg.peer_cfg.video_info.width,
            .height = cfg.peer_cfg.video_info.height,
            .fps = cfg.peer_cfg.video_info.fps,
        },
    };
    if (cfg.peer_cfg.audio_dir == ESP_PEER_MEDIA_DIR_RECV_ONLY) {
        sink_cfg.audio_info.format_id = ESP_CAPTURE_FMT_ID_NONE;
    }
    if (cfg.peer_cfg.video_dir == ESP_PEER_MEDIA_DIR_RECV_ONLY) {
        sink_cfg.video_info.format_id = ESP_CAPTURE_FMT_ID_NONE;
    }
    esp_capture_sink_handle_t main_sink = NULL;
    esp_capture_sink_setup(media_provider.capture, 0, &sink_cfg, &main_sink);

    // Configuration for capture sink 1 before start
    esp_capture_sink_cfg_t sink_cfg_1 = {
        .video_info = {
            .format_id = ESP_CAPTURE_FMT_ID_RGB565,
            .width = DETECT_WIDTH,
            .height = DETECT_HEIGHT,
            .fps = DETECT_FPS,
        },
    };
    esp_capture_sink_setup(media_provider.capture, 1, &sink_cfg_1, &detect_sink);
    esp_capture_sink_enable(detect_sink, ESP_CAPTURE_RUN_MODE_ALWAYS);
#endif

    // Set event handler
    esp_webrtc_set_event_handler(webrtc, webrtc_event_handler, NULL);

    // Default disable auto connect of peer connection
    esp_webrtc_enable_peer_connection(webrtc, false);

    media_lib_thread_handle_t data_thread;
    media_lib_thread_create_from_scheduler(&data_thread, "data", data_thread_hdlr, NULL);

    // Start webrtc
    ret = esp_webrtc_start(webrtc);
    if (ret != 0) {
        ESP_LOGE(TAG, "Fail to start webrtc");
    } else {
        play_tone(DOOR_BELL_TONE_JOIN_SUCCESS);
#ifdef CONFIG_DOORBELL_SUPPORT_PEDESTRIAN_DETECT
        esp_capture_start(media_provider.capture);
        pedestrian_detect_cfg_t cfg = {
            .detected = pedestrian_detected,
        };
        start_pedestrian_detection(&cfg);
#endif
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
        ESP_LOGI(TAG, "Start to close webrtc %p", handle);

#ifdef CONFIG_DOORBELL_SUPPORT_PEDESTRIAN_DETECT
       stop_pedestrian_detection();
       esp_webrtc_media_provider_t media_provider = {};
       media_sys_get_provider(&media_provider);
       esp_capture_stop(media_provider.capture);
       detect_sink = NULL;
#endif
        esp_webrtc_close(handle);
        // Wait for data running exit
        while (data_running) {
            media_lib_thread_sleep(10);
        }
    }
    return 0;
}
