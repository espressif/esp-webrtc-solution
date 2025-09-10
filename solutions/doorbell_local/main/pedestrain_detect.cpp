/* Pedestrian detection

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "common.h"
#include "esp_capture_types.h"
#include "esp_log.h"
#include "pedestrian_detect.hpp"
#include "data_queue.h"
#include "media_lib_os.h"

#ifdef __cplusplus
extern "C" {
#endif

static const char *TAG = "PEDESTRIAN_DETECT";

typedef struct {
    pedestrian_detect_cfg_t cfg;
    bool                    filtering;
    bool                    detecting;
    bool                    filter_exited;
    bool                    detect_exited;
    data_queue_t           *q;
} detect_t;

static detect_t *detector;

static void detecting_task(void *arg)
{
    detect_t *detect = (detect_t *)arg;
    PedestrianDetect *detect_lib = new PedestrianDetect();
    do {
        if (detect_lib == NULL) {
            break;
        }
        while (detect->detecting) {
            void *data = NULL;
            int size = 0;
            data_queue_read_lock(detect->q, &data, &size);
            if (data == NULL) {
                break;
            }
            esp_capture_stream_frame_t *frame = (esp_capture_stream_frame_t *)data;
            dl::image::img_t img = {
                .data = (void *)frame->data,
                .width = DETECT_WIDTH,
                .height = DETECT_HEIGHT,
                .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565,
            };
            auto &detect_results = detect_lib->run(img);
            data_queue_read_unlock(detect->q);
            for (const auto &res : detect_results) {
                ESP_LOGI(TAG,
                         "[score: %f, x1: %d, y1: %d, x2: %d, y2: %d]",
                         res.score,
                         res.box[0],
                         res.box[1],
                         res.box[2],
                         res.box[3]);
                esp_capture_rgn_t rgn = {
                    .x = (uint16_t)res.box[0],
                    .y = (uint16_t)res.box[1],
                    .width = (uint16_t)(res.box[2] - res.box[0]),
                    .height = (uint16_t)(res.box[3] - res.box[1]),
                };
                // Notify for detected, currently only notify for first region
                if (detect->cfg.detected) {
                    detect->cfg.detected(&rgn, detect->cfg.ctx);
                }
                break;
            }
        }
    } while (0);
    delete detect_lib;
    detect->detect_exited = true;
    ESP_LOGI(TAG, "Detecting exited");
    vTaskDelete(NULL);
}

static void filter_task(void *arg)
{
    detect_t *detect = (detect_t *)arg;
    esp_capture_stream_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO;
    esp_capture_sink_handle_t sink = get_detect_sink();
    while (detect->filtering) {
        int ret = esp_capture_sink_acquire_frame(sink, &frame, false);
        if (ret != ESP_CAPTURE_ERR_OK) {
            break;
        }
        int size = sizeof(esp_capture_stream_frame_t) + frame.size;
        // Changed function name to match actual API
        void *data = data_queue_get_buffer(detect->q, size);
        if (data) {
            esp_capture_stream_frame_t *filter = (esp_capture_stream_frame_t *)data;
            *filter = frame;
            filter->data = (uint8_t *)data + sizeof(esp_capture_stream_frame_t);
            memcpy(filter->data, frame.data, frame.size);
            data_queue_send_buffer(detect->q, size);
        }
        esp_capture_sink_release_frame(sink, &frame);
        if (data == NULL) {
            ESP_LOGE(TAG, "Fail to get buffer");
            break;
        }
    }
    ESP_LOGI(TAG, "Detect filtering exited");
    detect->filter_exited = true;
    media_lib_thread_destroy(NULL);
}

int start_pedestrian_detection(pedestrian_detect_cfg_t *cfg)
{
    if (detector) {
        ESP_LOGW(TAG, "Detect already running");
        return 0;
    }
    detector = (detect_t *)calloc(1, sizeof(detect_t));
    do {
        // Create dual buffer to avoid block output too long
        int size = DETECT_WIDTH * DETECT_HEIGHT * 2 + 256;
        detector->q = data_queue_init(size);
        if (detector->q == NULL) {
            ESP_LOGE(TAG, "Fail to init queue");
            break;
        }
        detector->cfg = *cfg;
        detector->filtering = true;
        detector->detecting = true;
        int ret = media_lib_thread_create_from_scheduler(NULL, "detect_filter", filter_task, detector);
        if (ret != 0) {
            detector->filtering = false;
            break;
        }
        ret = media_lib_thread_create_from_scheduler(NULL, "detect", detecting_task, detector);
        if (ret != 0) {
            detector->detecting = false;
            break;
        }
        return 0;
    } while (0);
    stop_pedestrian_detection();
    return -1;
}

void stop_pedestrian_detection()
{
    if (detector == NULL) {
        return;
    }
    if (detector->detecting || detector->filtering) {
        bool detecting = detector->detecting;
        bool filtering = detector->filtering;
        detector->detecting = false;
        detector->filtering = false;
        data_queue_wakeup(detector->q);
        if (detecting) {
            while (!detector->detect_exited) {
                media_lib_thread_sleep(10);
            }
        }
        if (filtering) {
            while (!detector->filter_exited) {
                media_lib_thread_sleep(10);
            }
        }
    }
    if (detector->q) {
        data_queue_deinit(detector->q);
    }
    free(detector);
    detector = NULL;
}

#ifdef __cplusplus
}
#endif
