/* RTMP pusher test

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include "esp_log.h"
#include "esp_capture.h"
#include "esp_capture_sink.h"
#include "esp_capture_advance.h"
#include "esp_gmf_video_enc.h"
#include "esp_rtmp_push.h"
#include "media_lib_os.h"
#include "common.h"
#include "rtmp.h"

static const char *TAG = "RTMP_PUSH";

static rtmp_push_handle_t       rtmp_handle;
static esp_capture_sink_handle_t rtmp_sink;
static volatile bool             rtmp_running;
static volatile bool             rtmp_task_started;
static media_lib_sema_handle_t   rtmp_task_exit;

static esp_rtmp_audio_codec_t get_rtmp_audio_codec(uint32_t format_id)
{
    switch (format_id) {
        case ESP_CAPTURE_FMT_ID_AAC:
            return RTMP_AUDIO_CODEC_AAC;
        case ESP_CAPTURE_FMT_ID_G711A:
            return RTMP_AUDIO_CODEC_G711A;
        case ESP_CAPTURE_FMT_ID_G711U:
            return RTMP_AUDIO_CODEC_G711U;
        case ESP_CAPTURE_FMT_ID_PCM:
            return RTMP_AUDIO_CODEC_PCM;
        default:
            return RTMP_AUDIO_CODEC_NONE;
    }
}

static esp_rtmp_video_codec_t get_rtmp_video_codec(uint32_t format_id)
{
    switch (format_id) {
        case ESP_CAPTURE_FMT_ID_H264:
            return RTMP_VIDEO_CODEC_H264;
        case ESP_CAPTURE_FMT_ID_MJPEG:
            return RTMP_VIDEO_CODEC_MJPEG;
        default:
            return RTMP_VIDEO_CODEC_NONE;
    }
}

static bool is_h264_key_frame(const uint8_t *data, int size)
{
    for (int i = 2; i + 1 < size; i++) {
        int start_code_len = 0;
        if (data[i - 2] == 0 && data[i - 1] == 0 && data[i] == 1) {
            start_code_len = 3;
        } else if (i >= 3 && data[i - 3] == 0 && data[i - 2] == 0 && data[i - 1] == 0 && data[i] == 1) {
            start_code_len = 4;
        }
        if (start_code_len == 0 || i + 1 >= size) {
            continue;
        }
        uint8_t nal_type = data[i + 1] & 0x1f;
        if (nal_type == 5 || nal_type == 7 || nal_type == 8) {
            return true;
        }
    }
    return false;
}

static bool is_video_key_frame(const esp_capture_stream_frame_t *frame)
{
    if (VIDEO_FORMAT == ESP_CAPTURE_FMT_ID_MJPEG) {
        return true;
    }
    if (VIDEO_FORMAT == ESP_CAPTURE_FMT_ID_H264) {
        return is_h264_key_frame(frame->data, frame->size);
    }
    return false;
}

static int rtmp_event_handler(esp_rtmp_event_t event, void *ctx)
{
    if (event == RTMP_EVENT_CLOSED_BY_SERVER) {
        ESP_LOGW(TAG, "RTMP server closed connection");
        rtmp_running = false;
    }
    return 0;
}

static void push_audio_frame(rtmp_push_handle_t handle)
{
    esp_capture_stream_frame_t frame = {
        .stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO,
    };
    if (esp_capture_sink_acquire_frame(rtmp_sink, &frame, true) != ESP_CAPTURE_ERR_OK) {
        return;
    }
    esp_rtmp_audio_data_t audio_data = {
        .pts = frame.pts,
        .data = frame.data,
        .size = frame.size,
    };
    esp_media_err_t ret = esp_rtmp_push_audio(handle, &audio_data);
    if (ret != ESP_MEDIA_ERR_OK) {
        ESP_LOGW(TAG, "Fail to push audio frame: %d", ret);
    }
    esp_capture_sink_release_frame(rtmp_sink, &frame);
}

static void push_video_frame(rtmp_push_handle_t handle)
{
    esp_capture_stream_frame_t frame = {
        .stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO,
    };
    if (esp_capture_sink_acquire_frame(rtmp_sink, &frame, true) != ESP_CAPTURE_ERR_OK) {
        return;
    }
    esp_rtmp_video_data_t video_data = {
        .pts = frame.pts,
        .key_frame = is_video_key_frame(&frame),
        .data = frame.data,
        .size = frame.size,
    };
    esp_media_err_t ret = esp_rtmp_push_video(handle, &video_data);
    if (ret != ESP_MEDIA_ERR_OK) {
        ESP_LOGW(TAG, "Fail to push video frame: %d", ret);
    }
    esp_capture_sink_release_frame(rtmp_sink, &frame);
}

static void rtmp_push_task(void *arg)
{
    while (rtmp_running) {
        rtmp_push_handle_t handle = rtmp_handle;
        if (handle == NULL || rtmp_sink == NULL) {
            break;
        }
        push_audio_frame(handle);
        push_video_frame(handle);
        media_lib_thread_sleep(10);
    }
    if (rtmp_task_exit) {
        media_lib_sema_unlock(rtmp_task_exit);
    }
    media_lib_thread_destroy(NULL);
}

static void wait_rtmp_push_task(void)
{
    if (rtmp_task_started && rtmp_task_exit) {
        media_lib_sema_lock(rtmp_task_exit, MEDIA_LIB_MAX_LOCK_TIME);
        rtmp_task_started = false;
    }
    if (rtmp_task_exit) {
        media_lib_sema_destroy(rtmp_task_exit);
        rtmp_task_exit = NULL;
    }
}

static esp_capture_err_t capture_event_hdlr(esp_capture_event_t event, void *ctx)
{
    switch (event) {
        case ESP_CAPTURE_EVENT_VIDEO_PIPELINE_BUILT: {
            if (VIDEO_FORMAT == ESP_CAPTURE_FMT_ID_H264) {
                esp_gmf_element_handle_t venc_hd = NULL;
                esp_capture_sink_get_element_by_tag(rtmp_sink, ESP_CAPTURE_STREAM_TYPE_VIDEO, "vid_enc", &venc_hd);
                if (venc_hd) {
                    esp_gmf_video_enc_set_gop(venc_hd, VIDEO_FPS * 2);
                }
            }
            break;
        }
        default:
            break;
    }
    return ESP_CAPTURE_ERR_OK;
}

int start_rtmp(const char *url)
{
    if (rtmp_handle) {
        ESP_LOGI(TAG, "Already started");
        return 0;
    }
    if (url == NULL || url[0] == '\0') {
        ESP_LOGE(TAG, "RTMP push URL is empty");
        return -1;
    }

    esp_rtmp_audio_codec_t audio_codec = get_rtmp_audio_codec(AUDIO_FORMAT);
    esp_rtmp_video_codec_t video_codec = get_rtmp_video_codec(VIDEO_FORMAT);
    if (audio_codec == RTMP_AUDIO_CODEC_NONE || video_codec == RTMP_VIDEO_CODEC_NONE) {
        ESP_LOGE(TAG, "Unsupported RTMP codec audio=%" PRIu32 " video=%" PRIu32, AUDIO_FORMAT, VIDEO_FORMAT);
        return -1;
    }

    esp_webrtc_media_provider_t provider = {};
    media_sys_get_provider(&provider);
    if (provider.capture == NULL) {
        ESP_LOGE(TAG, "Capture provider is not ready");
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
    if (esp_capture_sink_setup(provider.capture, 0, &sink_cfg, &rtmp_sink) != ESP_CAPTURE_ERR_OK) {
        ESP_LOGE(TAG, "Fail to setup capture sink");
        return -1;
    }
    esp_capture_sink_enable(rtmp_sink, ESP_CAPTURE_RUN_MODE_DISABLE);
    esp_capture_set_event_cb(provider.capture, capture_event_hdlr, NULL);
    if (esp_capture_start(provider.capture) != 0) {
        ESP_LOGE(TAG, "Fail to start capture");
        return -1;
    }

    rtmp_push_cfg_t push_cfg = {
        .url = (char *)url,
        .chunk_size = RTMP_PUSH_CHUNK_SIZE,
        .thread_cfg = {
            .stack_size = 6 * 1024,
            .priority = 20,
        },
        .event_cb = rtmp_event_handler,
    };
    rtmp_handle = esp_rtmp_push_open(&push_cfg);
    if (rtmp_handle == NULL) {
        ESP_LOGE(TAG, "Fail to open RTMP push");
        esp_capture_stop(provider.capture);
        return -1;
    }

    esp_rtmp_audio_info_t audio_info = {
        .codec = audio_codec,
        .channel = AUDIO_CHANNEL,
        .bits_per_sample = 16,
        .sample_rate = AUDIO_SAMPLE_RATE,
    };
    esp_rtmp_video_info_t video_info = {
        .codec = video_codec,
        .width = VIDEO_WIDTH,
        .height = VIDEO_HEIGHT,
        .fps = VIDEO_FPS,
    };
    if (esp_rtmp_push_set_audio_info(rtmp_handle, &audio_info) != ESP_MEDIA_ERR_OK ||
        esp_rtmp_push_set_video_info(rtmp_handle, &video_info) != ESP_MEDIA_ERR_OK ||
        esp_rtmp_push_connect(rtmp_handle) != ESP_MEDIA_ERR_OK) {
        ESP_LOGE(TAG, "Fail to connect RTMP push");
        esp_rtmp_push_close(rtmp_handle);
        rtmp_handle = NULL;
        esp_capture_stop(provider.capture);
        return -1;
    }

    esp_capture_sink_enable(rtmp_sink, ESP_CAPTURE_RUN_MODE_ALWAYS);
    if (media_lib_sema_create(&rtmp_task_exit) != 0) {
        ESP_LOGE(TAG, "Fail to create RTMP push task semaphore");
        esp_rtmp_push_close(rtmp_handle);
        rtmp_handle = NULL;
        esp_capture_stop(provider.capture);
        return -1;
    }
    rtmp_running = true;
    rtmp_task_started = true;
    if (media_lib_thread_create_from_scheduler(NULL, "rtmp_push", rtmp_push_task, NULL) != 0) {
        ESP_LOGE(TAG, "Fail to create RTMP push task");
        rtmp_running = false;
        rtmp_task_started = false;
        wait_rtmp_push_task();
        esp_rtmp_push_close(rtmp_handle);
        rtmp_handle = NULL;
        esp_capture_stop(provider.capture);
        return -1;
    }
    ESP_LOGI(TAG, "RTMP push started: %s", url);
    return 0;
}

int stop_rtmp(void)
{
    rtmp_running = false;
    if (rtmp_sink) {
        esp_capture_sink_enable(rtmp_sink, ESP_CAPTURE_RUN_MODE_DISABLE);
    }
    wait_rtmp_push_task();
    if (rtmp_handle) {
        esp_rtmp_push_close(rtmp_handle);
        rtmp_handle = NULL;
    }
    esp_webrtc_media_provider_t provider = {};
    media_sys_get_provider(&provider);
    if (provider.capture) {
        esp_capture_stop(provider.capture);
    }
    rtmp_sink = NULL;
    return 0;
}
