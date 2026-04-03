/* esp_peer Demo code

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_peer_default.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_log.h"

#define TAG "PEER_DEMO"

#define TEST_DURATION (10000)
#define PERIOD_CAT    (1100)
#define PERIOD_SHEEP  (900)

typedef struct {
    esp_peer_handle_t  peer;
    int                send_sequence;
    bool               peer_running;
    bool               peer_connected;
    bool               peer_stopped;
    const char        *name;
    uint16_t           period;
    esp_timer_handle_t timer;
} peer_info_t;

typedef struct {
    const char *question;
    const char *answer;
} data_channel_chat_content_t;

peer_info_t peers[2];

static data_channel_chat_content_t chat_content[] = {
    { "Meow!", "Mie-mie!" },
    { "What’s that fluffy cloud on your back?", "It’s my wool, silly cat!" },
    { "Can I nap on it?", "Only if you don’t claw!" },
    { "Deal.", "Soft kitty." },
};

static void send_cb(void *ctx)
{
    peer_info_t *peer_info = (peer_info_t *)ctx;
    if (peer_info && peer_info->peer_running) {
        uint8_t data[64];
        memset(data, (uint8_t)peer_info->send_sequence, sizeof(data));
        esp_peer_audio_frame_t audio_frame = {
            .data = data,
            .size = sizeof(data),
            .pts = peer_info->send_sequence,
        };
        // Send audio data
        esp_peer_send_audio(peer_info->peer, &audio_frame);
        peer_info->send_sequence++;
        char str[64];
        int sel = esp_random() % (sizeof(chat_content) / sizeof(chat_content[0]));
        snprintf(str, sizeof(str) - 1, "%s: %s", peer_info->name, chat_content[sel].question);
        // Send question through data channel
        esp_peer_data_frame_t data_frame = {
            .type = ESP_PEER_DATA_CHANNEL_DATA,
            .data = (uint8_t *)str,
            .size = strlen(str) + 1,
        };
        ESP_LOGI(TAG, "Asking   %s", (char *)data_frame.data);
        esp_peer_send_data(peer_info->peer, &data_frame);
    }
}

static int peer_state_handler(esp_peer_state_t state, void *ctx)
{
    peer_info_t *peer_info = (peer_info_t *)ctx;
    if (state == ESP_PEER_STATE_CONNECTED) {
        peer_info->peer_connected = true;
        if (peer_info->timer == NULL) {
            esp_timer_create_args_t cfg = {
                .callback = send_cb,
                .name = "send",
                .arg = peer_info,
            };
            esp_timer_create(&cfg, &peer_info->timer);
            if (peer_info->timer) {
                esp_timer_start_periodic(peer_info->timer, peer_info->period * 1000);
            }
        }
    } else if (state == ESP_PEER_STATE_DISCONNECTED) {
        peer_info->peer_connected = false;
        if (peer_info->timer) {
            esp_timer_stop(peer_info->timer);
            esp_timer_delete(peer_info->timer);
            peer_info->timer = NULL;
        }
    }
    return 0;
}

static int peer_msg_handler(esp_peer_msg_t *msg, void *ctx)
{
    if (msg->type == ESP_PEER_MSG_TYPE_SDP) {
        peer_info_t *peer_info = (peer_info_t *)ctx;
        // Exchange SDP with peer
        esp_peer_handle_t peer = (peer_info == &peers[0]) ? peers[1].peer : peers[0].peer;
        esp_peer_send_msg(peer, (esp_peer_msg_t *)msg);
    }
    return 0;
}

static int peer_video_info_handler(esp_peer_video_stream_info_t *info, void *ctx)
{
    return 0;
}

static int peer_audio_info_handler(esp_peer_audio_stream_info_t *info, void *ctx)
{
    return 0;
}

static int peer_audio_data_handler(esp_peer_audio_frame_t *frame, void *ctx)
{
    peer_info_t *peer_info = (peer_info_t *)ctx;
    ESP_LOGI(TAG, "%s received %d(%d)", peer_info->name, (int)frame->pts, (int)frame->data[0]);
    return 0;
}

static int peer_video_data_handler(esp_peer_video_frame_t *frame, void *ctx)
{
    return 0;
}

static int peer_data_handler(esp_peer_data_frame_t *frame, void *ctx)
{
    int ans = -1;
    peer_info_t *peer_info = (peer_info_t *)ctx;
    int name_len = (peer_info == &peers[0]) ? strlen(peers[1].name) : strlen(peers[0].name);
    name_len += 2;
    // Check whether is question
    for (int i = 0; i < sizeof(chat_content) / sizeof(chat_content[0]); i++) {
        if (strcmp((char *)frame->data + name_len, chat_content[i].question) == 0) {
            ans = i;
            break;
        }
    }
    // Answer question
    if (ans >= 0) {
        char str[64];
        snprintf(str, sizeof(str) - 1, "%s: %s", peer_info->name, chat_content[ans].answer);
        esp_peer_data_frame_t data_frame = {
            .type = ESP_PEER_DATA_CHANNEL_DATA,
            .data = (uint8_t *)str,
            .size = strlen(str) + 1,
        };
        esp_peer_send_data(peer_info->peer, &data_frame);
    } else {
        ESP_LOGI(TAG, "Answered %s\n", (char *)frame->data);
    }
    return 0;
}

static void pc_task(void *arg)
{
    peer_info_t *peer_info = (peer_info_t *)arg;
    while (peer_info->peer_running) {
        esp_peer_main_loop(peer_info->peer);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    peer_info->peer_stopped = true;
    vTaskDelete(NULL);
}

static int avail_mem(void)
{
    int left_size = esp_get_free_heap_size();
#ifdef CONFIG_SPIRAM_BOOT_INIT
    ESP_LOGI(TAG, "MEM Avail:%d, IRam:%d, PSRam:%d\n", (int)esp_get_free_heap_size(),
             (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#else
    ESP_LOGI(TAG, "MEM Avail:%d\n", left_size);
#endif
    return left_size;
}

static int create_peer(int idx)
{
    if (peers[idx].peer) {
        return 0;
    }
    esp_peer_default_cfg_t peer_cfg = {
        .agent_recv_timeout = 100,   // Enlarge this value if network is poor
        .data_ch_cfg = {
            .recv_cache_size = 1536, // Should big than one MTU size
            .send_cache_size = 1536, // Should big than one MTU size
        },
        .rtp_cfg = {
            .audio_recv_jitter = {
                .cache_size = 1024,
            },
            .send_pool_size = 1024,
            .send_queue_num = 10,
        },
    };
    esp_peer_cfg_t cfg = {
        //.server_lists = &server_info, // Should set to actual stun/turn servers
        //.server_num = 0,
        .audio_dir = ESP_PEER_MEDIA_DIR_SEND_RECV,
        .audio_info = {
            .codec = ESP_PEER_AUDIO_CODEC_G711A,
        },
        .enable_data_channel = true,
        .role = idx == 0 ? ESP_PEER_ROLE_CONTROLLING : ESP_PEER_ROLE_CONTROLLED,
        .on_state = peer_state_handler,
        .on_msg = peer_msg_handler,
        .on_video_info = peer_video_info_handler,
        .on_audio_info = peer_audio_info_handler,
        .on_video_data = peer_video_data_handler,
        .on_audio_data = peer_audio_data_handler,
        .on_data = peer_data_handler,
        .ctx = &peers[idx],
        .extra_cfg = &peer_cfg,
        .extra_size = sizeof(esp_peer_default_cfg_t),
    };
    int ret = esp_peer_open(&cfg, esp_peer_get_default_impl(), &peers[idx].peer);
    if (ret != ESP_PEER_ERR_NONE) {
        ESP_LOGE(TAG, "Fail to create PeerConnection ret %d", ret);
        return ret;
    }
    if (idx == 0) {
        peers[idx].name = "🐱";
        peers[idx].period = PERIOD_CAT;
    } else {
        peers[idx].name = "🐑";
        peers[idx].period = PERIOD_SHEEP;
    }
    peers[idx].peer_running = true;
    // We create on separate core to avoid when handshake (heavy calculation) peer Cat block peer Sheep
    if (xTaskCreatePinnedToCore(pc_task, peers[idx].name, 10 * 1024, &peers[idx], 5, NULL, idx) != pdPASS) {
        ESP_LOGE(TAG, "Fail to create thread %s", peers[idx].name);
        peers[idx].peer_running = false;
        return -1;
    }
    return 0;
}

static int destroy_peer(int idx)
{
    peer_info_t *peer_info = &peers[idx];
    if (peer_info->timer) {
        esp_timer_stop(peer_info->timer);
        esp_timer_delete(peer_info->timer);
        peer_info->timer = NULL;
    }
    if (peer_info->peer_running) {
        peer_info->peer_running = false;
        while (!peer_info->peer_stopped) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        peer_info->peer_stopped = false;
    }
    if (peer_info->peer) {
        esp_peer_close(peer_info->peer);
        peer_info->peer = NULL;
    }
    peer_info->peer_connected = false;
    return 0;
}

int wifi_init_softap(void);

void app_main()
{
    // Create SoftAP
    wifi_init_softap();

    uint32_t cur = esp_timer_get_time() / 1000;
    uint32_t end = cur + TEST_DURATION;
    int before_run = avail_mem();
    esp_peer_pre_generate_cert();
    // Create 2 peers
    create_peer(0);
    create_peer(1);

    // Delay to create connection only when both ready
    esp_peer_new_connection(peers[0].peer);
    esp_peer_new_connection(peers[1].peer);
    avail_mem();

    // Wait for test duration reached
    while (cur < end) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        cur = esp_timer_get_time() / 1000;
    }
    // Destroy 2 peers
    int after_run = avail_mem();
    destroy_peer(0);
    destroy_peer(1);

    int after_stop = avail_mem();
    ESP_LOGI(TAG, "Dual PeerConenction used memory %d kept %d",
             after_stop - after_run, before_run - after_stop);
}
