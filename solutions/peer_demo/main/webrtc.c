/* OpenAI realtime communication Demo code

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "media_lib_os.h"
#include "esp_log.h"
#include "esp_webrtc_defaults.h"
#include "esp_peer_default.h"
#include "common.h"
#include "esp_timer.h"
#include "esp_random.h"

#define TAG "PEER_DEMO"

#define TEST_PERIOD 1000

static esp_peer_signaling_handle_t signaling = NULL;
static esp_peer_handle_t peer = NULL;
static esp_timer_handle_t timer;
static int send_sequence = 0;
static bool peer_running = false;

typedef struct {
    const char* question;
    const char* answer;
} data_channel_chat_content_t;

static data_channel_chat_content_t chat_content[] = {
    {"Hi!", "Hello!"},
    {"How are you?", "I am fine."},
    {"Wish to be your friend.", "Great!"},
    {"What's your name?", "I am a chatbot, nice to meet you!"},
    {"What do you do?", "I am here to chat and assist you with various tasks."},
    {"How old are you?", "I don't have an age. I was created to chat with you!"},
    {"Do you have hobbies?", "I enjoy chatting with you and learning new things."},
    {"Tell me a story.", "Once upon a time, a curious cat discovered a magical world..."},
    {"Tell me a joke.", "Why don't skeletons fight each other? They don't have the guts!"},
    {"What is the weather like?", "I am not sure, but you can check your local forecast."},
    {"What is your favorite color?", "I don't have a favorite color, but I like all of them!"},
    {"What is the time?", "Sorry, I can't tell the time. You can check your device for that."},
    {"What can you do?", "I can answer questions, tell jokes, help with tasks, and much more!"},
    {"Where are you from?", "I was created by developers, so I don't have a specific location."},
    {"What is love?", "Love is a complex emotion that connects people. What do you think love is?"},
    {"Do you like music?", "I don't listen to music, but I know about it! What's your favorite genre?"},
    {"Goodbye!", "Bye!"},
};

static void send_cb(void *ctx)
{
    if (peer) {
        uint8_t data[64];
        memset(data, (uint8_t)send_sequence, sizeof(data));
        esp_peer_audio_frame_t audio_frame = {
            .data = data,
            .size = sizeof(data),
            .pts = send_sequence,
        };
        // Send audio data
        esp_peer_send_audio(peer, &audio_frame);
        send_sequence++;
        int question = esp_random() % (sizeof(chat_content) / sizeof(chat_content[0]));

        // Send question through data channel
        esp_peer_data_frame_t data_frame = {
            .type = ESP_PEER_DATA_CHANNEL_DATA,
            .data = (uint8_t*)chat_content[question].question,
            .size = strlen(chat_content[question].question) + 1,
        };
        ESP_LOGI(TAG, "Send question:%s", (char*)data_frame.data);
        esp_peer_send_data(peer, &data_frame);
    }
}

static int peer_state_handler(esp_peer_state_t state, void* ctx)
{
    if (state == ESP_PEER_STATE_CONNECTED) {
        if (timer == NULL) {
            esp_timer_create_args_t cfg = {
                .callback = send_cb,
                .name = "send",
            };
            esp_timer_create(&cfg, &timer);
            if (timer) {
                esp_timer_start_periodic(timer, TEST_PERIOD * 1000);
            }
        }
    } else if (state == ESP_PEER_STATE_DISCONNECTED) {
        if (timer) {
            esp_timer_stop(timer);
            esp_timer_delete(timer);
            timer = NULL;
        }
    }
    return 0;
}

static int peer_msg_handler(esp_peer_msg_t* msg, void* ctx)
{
    if (msg->type == ESP_PEER_MSG_TYPE_SDP) {
        // Send local SDP to signaling server
        esp_peer_signaling_send_msg(signaling, (esp_peer_signaling_msg_t *)msg);
    }
    return 0;
}

static int peer_video_info_handler(esp_peer_video_stream_info_t* info, void* ctx)
{
    return 0;
}

static int peer_audio_info_handler(esp_peer_audio_stream_info_t* info, void* ctx)
{
    return 0;
}

static int peer_audio_data_handler(esp_peer_audio_frame_t* frame, void* ctx)
{
    ESP_LOGI(TAG, "Audio Sequence %d(%d)", (int)frame->pts, (int)frame->data[0]);
    return 0;
}

static int peer_video_data_handler(esp_peer_video_frame_t* frame, void* ctx)
{
    return 0;
}

static int peer_data_handler(esp_peer_data_frame_t* frame, void* ctx)
{
    int ans = -1;
    for (int i = 0; i < sizeof(chat_content) / sizeof(chat_content[0]); i++) {
        if (strcmp((char*)frame->data, chat_content[i].question) == 0) {
            ans = i;
            break;
        }
    }
    if (ans >= 0) {
        ESP_LOGI(TAG, "Get question:%s", (char*)frame->data);
        esp_peer_data_frame_t data_frame = {
            .type = ESP_PEER_DATA_CHANNEL_DATA,
            .data = (uint8_t*)chat_content[ans].answer,
            .size = strlen(chat_content[ans].answer) + 1,
        };
        ESP_LOGI(TAG, "Send answer:%s", (char*)data_frame.data);
        esp_peer_send_data(peer, &data_frame);
    } else {
        ESP_LOGI(TAG, "Get answer:%s", (char*)frame->data);
    }
    return 0;
}

static void pc_task(void *arg)
{
    while (peer_running) {
        esp_peer_main_loop(peer);
        media_lib_thread_sleep(20);
    }
    media_lib_thread_destroy(NULL);
}

static int signaling_ice_info_handler(esp_peer_signaling_ice_info_t* info, void* ctx)
{
    if (peer == NULL) {
        esp_peer_default_cfg_t peer_cfg = {
            .tcp_support = true,
            .agent_recv_timeout = 500,
            /* Lab/testing: the bundled coturn uses a self-signed cert for TURNS. */
            .insecure_skip_turn_cert_verify = true,
        };
        esp_peer_cfg_t cfg = {
            .server_lists = &info->server_info,
            .server_num = 1,
            .audio_dir = ESP_PEER_MEDIA_DIR_SEND_RECV,
            .audio_info = {
                .codec = ESP_PEER_AUDIO_CODEC_G711A,
            },
            .enable_data_channel = true,
            .role = info->is_initiator ? ESP_PEER_ROLE_CONTROLLING : ESP_PEER_ROLE_CONTROLLED,
            .on_state = peer_state_handler,
            .on_msg = peer_msg_handler,
            .on_video_info = peer_video_info_handler,
            .on_audio_info = peer_audio_info_handler,
            .on_video_data = peer_video_data_handler,
            .on_audio_data = peer_audio_data_handler,
            .on_data = peer_data_handler,
            .ctx = ctx,
            .extra_cfg = &peer_cfg,
            .extra_size = sizeof(esp_peer_default_cfg_t),
        };
        int ret = esp_peer_open(&cfg, esp_peer_get_default_impl(), &peer);
        if (ret != ESP_PEER_ERR_NONE) {
            return ret;
        }
        media_lib_thread_handle_t thread = NULL;
        peer_running = true;
        media_lib_thread_create_from_scheduler(&thread, "pc_task", pc_task, NULL);
        if (thread == NULL) {
            peer_running = false;
        }
    }
    return 0;
}

static int signaling_connected_handler(void* ctx)
{
    if (peer) {
        return esp_peer_new_connection(peer);
    }
    return 0;
}

static int signaling_msg_handler(esp_peer_signaling_msg_t* msg, void* ctx)
{
    if (msg->type == ESP_PEER_SIGNALING_MSG_BYE) {
        esp_peer_close(peer);
        peer = NULL;
    } else if (msg->type == ESP_PEER_SIGNALING_MSG_SDP) {
        // Receive remote SDP
        if (peer) {
            esp_peer_send_msg(peer, (esp_peer_msg_t*)msg);
        }
    } else if (msg->type == ESP_PEER_SIGNALING_MSG_CANDIDATE) {
        if (peer) {
            esp_peer_send_msg(peer, (esp_peer_msg_t*)msg);
        }
    }
    return 0;
}

static int signaling_close_handler(void *ctx)
{
    return 0;
}

static int start_signaling(char* url)
{
    esp_peer_signaling_cfg_t cfg = {
        .signal_url = url,
        .on_ice_info = signaling_ice_info_handler,
        .on_connected = signaling_connected_handler,
        .on_msg = signaling_msg_handler,
        .on_close = signaling_close_handler,
    };
    // Use APPRTC signaling
    return esp_peer_signaling_start(&cfg, esp_signaling_get_apprtc_impl(), &signaling);
}

int start_webrtc(char *url)
{
    if (network_is_connected() == false) {
        ESP_LOGE(TAG, "Wifi not connected yet");
        return -1;
    }
    stop_webrtc();
    return start_signaling(url);
}

void query_webrtc(void)
{
    if (peer) {
        esp_peer_query(peer);
    }
}

int stop_webrtc(void)
{
    peer_running = false;
    if (timer) {
        esp_timer_stop(timer);
        esp_timer_delete(timer);
        timer = NULL;
    }
    if (peer) {
        esp_peer_close(peer);
        peer = NULL;
    }
    if (signaling) {
        esp_peer_signaling_stop(signaling);
        signaling = NULL;
    }
    return 0;
}
