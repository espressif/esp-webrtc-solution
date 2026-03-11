#include <string.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "media_lib_os.h"
#include "esp_peer_default.h"
#include "esp_webrtc_defaults.h"
#include "common.h"
#include "usb_device_uvc.h"

#define TAG "WEBRTC_USB_CAM"

typedef struct {
    uvc_fb_t frames[WEBRTC_USB_FRAME_SLOTS];
    uint8_t *buffers[WEBRTC_USB_FRAME_SLOTS];
    QueueHandle_t free_q;
    QueueHandle_t ready_q;
    esp_peer_video_stream_info_t stream_info;
    uint8_t  sps_pps[256];
    int      sps_pps_len;
    bool host_started;
    bool uvc_inited;
} usb_video_bridge_t;

static esp_peer_signaling_handle_t signaling = NULL;
static esp_peer_handle_t peer = NULL;
static bool peer_running = false;
static usb_video_bridge_t s_bridge = {0};

static int peer_msg_handler(esp_peer_msg_t* msg, void* ctx)
{
    if (msg->type == ESP_PEER_MSG_TYPE_SDP) {
        esp_peer_signaling_send_msg(signaling, (esp_peer_signaling_msg_t *)msg);
    }
    return 0;
}

static int get_frame_index(uvc_fb_t *fb)
{
    for (int i = 0; i < WEBRTC_USB_FRAME_SLOTS; i++) {
        if (&s_bridge.frames[i] == fb) {
            return i;
        }
    }
    return -1;
}

static esp_err_t uvc_start_cb(uvc_format_t format, int width, int height, int rate, void *cb_ctx)
{
    usb_video_bridge_t *bridge = (usb_video_bridge_t *)cb_ctx;
    bridge->host_started = true;
    ESP_LOGI(TAG, "UVC host start format=%d %dx%d@%d", format, width, height, rate);
    return ESP_OK;
}

static uvc_fb_t *uvc_fb_get_cb(void *cb_ctx)
{
    usb_video_bridge_t *bridge = (usb_video_bridge_t *)cb_ctx;
    int idx = -1;
    if (xQueueReceive(bridge->ready_q, &idx, pdMS_TO_TICKS(50)) == pdTRUE) {
        printf("get video %d\n", (int)bridge->frames[idx].len);
        return &bridge->frames[idx];
    }
    return NULL;
}

static void uvc_fb_return_cb(uvc_fb_t *fb, void *cb_ctx)
{
    usb_video_bridge_t *bridge = (usb_video_bridge_t *)cb_ctx;
    int idx = get_frame_index(fb);
    if (idx >= 0) {
        xQueueSend(bridge->free_q, &idx, 0);
    }
}

static void uvc_stop_cb(void *cb_ctx)
{
    usb_video_bridge_t *bridge = (usb_video_bridge_t *)cb_ctx;
    bridge->host_started = false;
    ESP_LOGI(TAG, "UVC host stop");
}

static esp_err_t init_uvc_device(void)
{
    if (s_bridge.uvc_inited) {
        return ESP_OK;
    }
    s_bridge.free_q = xQueueCreate(WEBRTC_USB_FRAME_SLOTS, sizeof(int));
    s_bridge.ready_q = xQueueCreate(WEBRTC_USB_FRAME_SLOTS, sizeof(int));
    if (!s_bridge.free_q || !s_bridge.ready_q) {
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < WEBRTC_USB_FRAME_SLOTS; i++) {
        s_bridge.buffers[i] = heap_caps_malloc(WEBRTC_USB_FRAME_MAX_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_bridge.buffers[i] == NULL) {
            s_bridge.buffers[i] = heap_caps_malloc(WEBRTC_USB_FRAME_MAX_SIZE, MALLOC_CAP_8BIT);
        }
        if (s_bridge.buffers[i] == NULL) {
            return ESP_ERR_NO_MEM;
        }
        s_bridge.frames[i].buf = s_bridge.buffers[i];
        s_bridge.frames[i].len = 0;
        s_bridge.frames[i].format = UVC_FORMAT_H264;
        xQueueSend(s_bridge.free_q, &i, 0);
    }

    static uint8_t *uvc_dma_buffer;
    if (uvc_dma_buffer == NULL) {
        uvc_dma_buffer = heap_caps_malloc(WEBRTC_USB_FRAME_MAX_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    }
    if (!uvc_dma_buffer) {
        return ESP_ERR_NO_MEM;
    }

    uvc_device_config_t cfg = {
        .uvc_buffer = uvc_dma_buffer,
        .uvc_buffer_size = WEBRTC_USB_FRAME_MAX_SIZE,
        .start_cb = uvc_start_cb,
        .fb_get_cb = uvc_fb_get_cb,
        .fb_return_cb = uvc_fb_return_cb,
        .stop_cb = uvc_stop_cb,
        .cb_ctx = &s_bridge,
    };
    ESP_ERROR_CHECK(uvc_device_config(0, &cfg));
    ESP_ERROR_CHECK(uvc_device_init());
    s_bridge.uvc_inited = true;
    return ESP_OK;
}

static int peer_state_handler(esp_peer_state_t state, void* ctx)
{
    ESP_LOGI(TAG, "Peer state %d", state);
    return 0;
}

static int peer_video_info_handler(esp_peer_video_stream_info_t *info, void *ctx)
{
    s_bridge.stream_info = *info;
    s_bridge.sps_pps_len = 0;

    ESP_LOGI(TAG, "Remote video codec=%d size=%dx%d fps=%d", info->codec, info->width, info->height, info->fps);
    return 0;
}

static int peer_audio_info_handler(esp_peer_audio_stream_info_t* info, void* ctx)
{
    ESP_LOGI(TAG, "Ignore audio stream codec=%d", info->codec);
    return 0;
}

static int peer_audio_data_handler(esp_peer_audio_frame_t* frame, void* ctx)
{
    return 0;
}

static int find_start_code(const uint8_t *data, int size, int start)
{
    for (int i = start; i + 3 < size; i++) {
        if (data[i] == 0 && data[i + 1] == 0) {
            if (data[i + 2] == 1) {
                return i;
            }
            if ((i + 4 < size) && data[i + 2] == 0 && data[i + 3] == 1) {
                return i;
            }
        }
    }
    return -1;
}

static int peer_extract_append_sps_pps(uint8_t *data, int size, uint8_t *fb_buf)
{
    bool has_idr = false;
    int pos = 0;
    while (1) {
        int sc = find_start_code(data, size, pos);
        if (sc < 0) {
            break;
        }
        int sc_len = (data[sc + 2] == 1) ? 3 : 4;
        int nal_start = sc + sc_len;
        int next_sc = find_start_code(data, size, nal_start);
        int nal_end = (next_sc < 0) ? size : next_sc;
        int nal_len = nal_end - nal_start;
        if (nal_len > 0) {
            uint8_t nal_type = data[nal_start] & 0x1F;
            if (nal_type == 5) {
                has_idr = true;
            } else if (nal_type == 7 && nal_len + sc_len <= sizeof(s_bridge.sps_pps)) {
                // Add SPS
                s_bridge.sps_pps_len = 0;
                memset(s_bridge.sps_pps, 0, sc_len);
                s_bridge.sps_pps[sc_len - 1] = 1;
                memcpy(s_bridge.sps_pps + sc_len, data + nal_start, nal_len);
                s_bridge.sps_pps_len = nal_len + sc_len;
            } else if (nal_type == 8 && nal_len + s_bridge.sps_pps_len + sc_len <= sizeof(s_bridge.sps_pps)) {
                // Append PPS
                memset(s_bridge.sps_pps + s_bridge.sps_pps_len, 0, sc_len);
                s_bridge.sps_pps[s_bridge.sps_pps_len + sc_len - 1] = 1;
                s_bridge.sps_pps_len += sc_len;
                memcpy(s_bridge.sps_pps + s_bridge.sps_pps_len, data + nal_start, nal_len);
                s_bridge.sps_pps_len += nal_len;
                ESP_LOG_BUFFER_HEX(TAG, s_bridge.sps_pps, s_bridge.sps_pps_len);
            }
        }
        pos = nal_end;
    }
    if (has_idr && s_bridge.sps_pps_len > 0) {
        memcpy(fb_buf, s_bridge.sps_pps, s_bridge.sps_pps_len);
        printf("IDR add sps pps\n");
        return s_bridge.sps_pps_len;
    }
    return 0;
}


static int peer_video_data_handler(esp_peer_video_frame_t *frame, void *ctx)
{
    if (!s_bridge.uvc_inited) {
        return 0;
    }
    if (frame->size + sizeof(s_bridge.sps_pps) > WEBRTC_USB_FRAME_MAX_SIZE) {
        ESP_LOGW(TAG, "Too long video frame size %d over WEBRTC_USB_FRAME_MAX_SIZE", (int)frame->size);
        return 0;
    }
    int idx = -1;
    if (xQueueReceive(s_bridge.free_q, &idx, 0) != pdTRUE) {
        return 0;
    }
    uvc_fb_t *fb = &s_bridge.frames[idx];
    int head_filled = 0;
    if (s_bridge.stream_info.codec == ESP_PEER_VIDEO_CODEC_H264) {
        head_filled = peer_extract_append_sps_pps(frame->data, frame->size, fb->buf);
    }
    fb->len = frame->size + head_filled;
    memcpy(fb->buf + head_filled, frame->data, frame->size);
    fb->width = s_bridge.stream_info.width;
    fb->height = s_bridge.stream_info.height;
    fb->format = UVC_FORMAT_H264;

    int64_t now_us = esp_timer_get_time();
    fb->timestamp.tv_sec = now_us / 1000000;
    fb->timestamp.tv_usec = now_us % 1000000;

    if (xQueueSend(s_bridge.ready_q, &idx, 0) != pdTRUE) {
        xQueueSend(s_bridge.free_q, &idx, 0);
    }
    return 0;
}

static void pc_task(void *arg)
{
    while (peer_running) {
        esp_peer_main_loop(peer);
        media_lib_thread_sleep(10);
    }
    media_lib_thread_destroy(NULL);
}

static int signaling_ice_info_handler(esp_peer_signaling_ice_info_t* info, void* ctx)
{
    if (peer == NULL) {
        esp_peer_default_cfg_t peer_cfg = {
            .agent_recv_timeout = 500,
            .rtp_cfg.video_recv_jitter.pli_send_interval = 2000,
        };
        esp_peer_cfg_t cfg = {
            .server_lists = &info->server_info,
            .server_num = 1,
            .audio_dir = ESP_PEER_MEDIA_DIR_RECV_ONLY,
            .video_dir = ESP_PEER_MEDIA_DIR_RECV_ONLY,
            .video_info = {
                .codec = ESP_PEER_VIDEO_CODEC_H264,
            },
            .role = info->is_initiator ? ESP_PEER_ROLE_CONTROLLING : ESP_PEER_ROLE_CONTROLLED,
            .on_state = peer_state_handler,
            .on_msg = peer_msg_handler,
            .on_video_info = peer_video_info_handler,
            .on_audio_info = peer_audio_info_handler,
            .on_video_data = peer_video_data_handler,
            .on_audio_data = peer_audio_data_handler,
            .ctx = ctx,
            .extra_cfg = &peer_cfg,
            .extra_size = sizeof(esp_peer_default_cfg_t),
        };
        int ret = esp_peer_open(&cfg, esp_peer_get_default_impl(), &peer);
        if (ret != ESP_PEER_ERR_NONE) {
            return ret;
        }
        peer_running = true;
        media_lib_thread_create_from_scheduler(NULL, "pc_task", pc_task, NULL);
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
        esp_peer_disconnect(peer);
        esp_peer_new_connection(peer);
    } else if (msg->type == ESP_PEER_SIGNALING_MSG_SDP ||
              msg->type == ESP_PEER_SIGNALING_MSG_CANDIDATE) {
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

static int start_signaling(char *url)
{
    esp_peer_signaling_cfg_t cfg = {
        .signal_url = url,
        .on_ice_info = signaling_ice_info_handler,
        .on_connected = signaling_connected_handler,
        .on_msg = signaling_msg_handler,
        .on_close = signaling_close_handler,
    };
    return esp_peer_signaling_start(&cfg, esp_signaling_get_apprtc_impl(), &signaling);
}

int start_webrtc(char *url)
{
    if (!network_is_connected()) {
        ESP_LOGE(TAG, "Wifi not connected yet");
        return -1;
    }
    ESP_ERROR_CHECK(init_uvc_device());
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
