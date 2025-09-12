/* Door Bell Demo

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "sdkconfig.h"
#include "esp_timer.h"
#include "media_lib_adapter.h"
#include "media_lib_os.h"
#include "esp_codec_dev.h"
#include "codec_init.h"
#include "codec_board.h"
#if CONFIG_IDF_TARGET_ESP32P4
#include "esp_video_init.h"
#endif
#include "av_render.h"
#include "av_render_default.h"
#include "esp_audio_enc_default.h"
#include "esp_video_enc_default.h"
#include "esp_video_dec_default.h"
#include "esp_audio_dec_default.h"
#include "esp_capture_defaults.h"
#include "esp_capture_sink.h"
#include "settings.h"
#include "esp_log.h"

static const char *TAG = "Render Test";

#define RET_ON_NULL(ptr, v) do {                                \
    if (ptr == NULL) {                                          \
        ESP_LOGE(TAG, "Memory allocate fail on %d", __LINE__);  \
        return v;                                               \
    }                                                           \
} while (0)

typedef struct {
    esp_capture_sink_handle_t   capture_handle;
    esp_capture_video_src_if_t *vid_src;
    esp_capture_audio_src_if_t *aud_src;
} capture_system_t;

typedef struct {
    audio_render_handle_t audio_render;
    video_render_handle_t video_render;
    av_render_handle_t    player;
} player_system_t;

static capture_system_t capture_sys;
static player_system_t  player_sys;

static void init_board()
{
    ESP_LOGI(TAG, "Init board.");
    set_codec_board_type(TEST_BOARD_NAME);
    codec_init_cfg_t cfg = {.reuse_dev = false};
    init_codec(&cfg);
    board_lcd_init();
}

static void thread_scheduler(const char *thread_name, media_lib_thread_cfg_t *schedule_cfg)
{
    if (strcmp(thread_name, "venc_0") == 0) {
        // For H264 may need huge stack if use hardware encoder can set it to small value
        schedule_cfg->priority = 10;
#if CONFIG_IDF_TARGET_ESP32S3
        schedule_cfg->stack_size = 20 * 1024;
#endif
    }
    else if (strcmp(thread_name, "AUD_SRC") == 0) {
        schedule_cfg->priority = 15;
    }
}

static void capture_scheduler(const char *name, esp_capture_thread_schedule_cfg_t *schedule_cfg)
{
    media_lib_thread_cfg_t cfg = {
        .stack_size = schedule_cfg->stack_size,
        .priority = schedule_cfg->priority,
        .core_id = schedule_cfg->core_id,
    };
    schedule_cfg->stack_in_ext = true;
    thread_scheduler(name, &cfg);
    schedule_cfg->stack_size = cfg.stack_size;
    schedule_cfg->priority = cfg.priority;
    schedule_cfg->core_id = cfg.core_id;
}

static esp_capture_video_src_if_t *create_video_source(void)
{
    camera_cfg_t cam_pin_cfg = {};
    int ret = get_camera_cfg(&cam_pin_cfg);
    if (ret != 0) {
        return NULL;
    }
#if CONFIG_IDF_TARGET_ESP32P4
    esp_video_init_csi_config_t csi_config = { 0 };
    esp_video_init_dvp_config_t dvp_config = { 0 };
    esp_video_init_config_t cam_config = { 0 };
    if (cam_pin_cfg.type == CAMERA_TYPE_MIPI) {
        csi_config.sccb_config.i2c_handle = get_i2c_bus_handle(0);
        csi_config.sccb_config.freq = 100000;
        csi_config.reset_pin = cam_pin_cfg.reset;
        csi_config.pwdn_pin = cam_pin_cfg.pwr;
        ESP_LOGI(TAG, "Use i2c handle %p", csi_config.sccb_config.i2c_handle);
        cam_config.csi = &csi_config;
    } else if (cam_pin_cfg.type == CAMERA_TYPE_DVP) {
        dvp_config.reset_pin = cam_pin_cfg.reset;
        dvp_config.pwdn_pin = cam_pin_cfg.pwr;
        dvp_config.dvp_pin.data_width = CAM_CTLR_DATA_WIDTH_8;
        dvp_config.dvp_pin.data_io[0] = cam_pin_cfg.data[0];
        dvp_config.dvp_pin.data_io[1] = cam_pin_cfg.data[1];
        dvp_config.dvp_pin.data_io[2] = cam_pin_cfg.data[2];
        dvp_config.dvp_pin.data_io[3] = cam_pin_cfg.data[3];
        dvp_config.dvp_pin.data_io[4] = cam_pin_cfg.data[4];
        dvp_config.dvp_pin.data_io[5] = cam_pin_cfg.data[5];
        dvp_config.dvp_pin.data_io[6] = cam_pin_cfg.data[6];
        dvp_config.dvp_pin.data_io[7] = cam_pin_cfg.data[7];
        dvp_config.dvp_pin.vsync_io = cam_pin_cfg.vsync;
        dvp_config.dvp_pin.pclk_io = cam_pin_cfg.pclk;
        dvp_config.dvp_pin.xclk_io = cam_pin_cfg.xclk;
        dvp_config.dvp_pin.de_io = cam_pin_cfg.de;
        dvp_config.xclk_freq = 20000000;
        cam_config.dvp = &dvp_config;
    }
    ret = esp_video_init(&cam_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", ret);
        return NULL;
    }
    esp_capture_video_v4l2_src_cfg_t v4l2_cfg = {
        .dev_name = "/dev/video0",
        .buf_count = 2,
    };
    return esp_capture_new_video_v4l2_src(&v4l2_cfg);
#endif

#if CONFIG_IDF_TARGET_ESP32S3
    if (cam_pin_cfg.type == CAMERA_TYPE_DVP) {
        esp_capture_video_dvp_src_cfg_t dvp_config = { 0 };
        dvp_config.buf_count = 2;
        dvp_config.reset_pin = cam_pin_cfg.reset;
        dvp_config.pwr_pin = cam_pin_cfg.pwr;
        dvp_config.data[0] = cam_pin_cfg.data[0];
        dvp_config.data[1] = cam_pin_cfg.data[1];
        dvp_config.data[2] = cam_pin_cfg.data[2];
        dvp_config.data[3] = cam_pin_cfg.data[3];
        dvp_config.data[4] = cam_pin_cfg.data[4];
        dvp_config.data[5] = cam_pin_cfg.data[5];
        dvp_config.data[6] = cam_pin_cfg.data[6];
        dvp_config.data[7] = cam_pin_cfg.data[7];
        dvp_config.vsync_pin = cam_pin_cfg.vsync;
        dvp_config.href_pin = cam_pin_cfg.href;
        dvp_config.pclk_pin = cam_pin_cfg.pclk;
        dvp_config.xclk_pin = cam_pin_cfg.xclk;
        dvp_config.xclk_freq = 20000000;
        return esp_capture_new_video_dvp_src(&dvp_config);
    }
#endif
    return NULL;
}

static int build_capture_system(void)
{
    capture_sys.vid_src = create_video_source();
    RET_ON_NULL(capture_sys.vid_src, -1);

    esp_capture_audio_dev_src_cfg_t codec_cfg = {
        .record_handle = get_record_handle(),
    };
    capture_sys.aud_src = esp_capture_new_audio_dev_src(&codec_cfg);
    RET_ON_NULL(capture_sys.aud_src, -1);
    // Create capture system
    esp_capture_cfg_t cfg = {
        .sync_mode = ESP_CAPTURE_SYNC_MODE_AUDIO,
        .audio_src = capture_sys.aud_src,
        .video_src = capture_sys.vid_src,
    };
    esp_capture_open(&cfg, &capture_sys.capture_handle);
    return 0;
}

static int build_player_system(void)
{
    i2s_render_cfg_t i2s_cfg = {
        .fixed_clock = true,
        .play_handle = get_playback_handle(),
    };
    player_sys.audio_render = av_render_alloc_i2s_render(&i2s_cfg);
    if (player_sys.audio_render == NULL) {
        ESP_LOGE(TAG, "Fail to create audio render");
        return -1;
    }
    lcd_render_cfg_t lcd_cfg = {
        .lcd_handle = board_get_lcd_handle(),
    };
    player_sys.video_render = av_render_alloc_lcd_render(&lcd_cfg);
    if (player_sys.video_render == NULL) {
        ESP_LOGE(TAG, "Fail to create video render");
        return -1;
    }
    av_render_cfg_t render_cfg = {
        .audio_render = player_sys.audio_render,
        .video_render = player_sys.video_render,
        .audio_raw_fifo_size = 4096,
        .audio_render_fifo_size = 6 * 1024,
        .video_raw_fifo_size = 500 * 1024,
        .allow_drop_data = false,
    };
    player_sys.player = av_render_open(&render_cfg);
    if (player_sys.player == NULL) {
        ESP_LOGE(TAG, "Fail to create player");
        return -1;
    }
    return 0;
}

static int media_sys_buildup(void)
{
    // Register for default audio and video codecs
    esp_video_enc_register_default();
    esp_audio_enc_register_default();
    esp_video_dec_register_default();
    esp_audio_dec_register_default();
    // Build capture system
    build_capture_system();
    // Build player system
    build_player_system();
    return 0;
}

esp_capture_sink_handle_t setup_capture_sink(void)
{
    esp_capture_sink_cfg_t sink_cfg = {
        .audio_info = {
            .format_id = ESP_CAPTURE_FMT_ID_G711A,
            .sample_rate = 8000,
            .channel = 1,
            .bits_per_sample = 16,
        },
        .video_info = {
            .format_id = ESP_CAPTURE_FMT_ID_MJPEG,
            .width = VIDEO_WIDTH,
            .height = VIDEO_HEIGHT,
            .fps = VIDEO_FPS
        },
    };
    // Create capture
    esp_capture_sink_handle_t capture_path = NULL;
    esp_capture_sink_setup(capture_sys.capture_handle, 0, &sink_cfg, &capture_path);
    esp_capture_sink_enable(capture_path, ESP_CAPTURE_RUN_MODE_ALWAYS);
    return capture_path;
}

static int test_capture_to_player(int duration)
{
    // Create capture sink
    esp_capture_sink_handle_t capture_path = setup_capture_sink();

    // Create player
    av_render_audio_info_t render_aud_info = {
        .codec = AV_RENDER_AUDIO_CODEC_G711A,
        .sample_rate = 8000,
        .channel = 1,
    };
    av_render_add_audio_stream(player_sys.player, &render_aud_info);

    av_render_video_info_t render_vid_info = {
        .codec = AV_RENDER_VIDEO_CODEC_MJPEG,
    };
    av_render_add_video_stream(player_sys.player, &render_vid_info);

    uint32_t start_time = (uint32_t)(esp_timer_get_time() / 1000);
    esp_capture_start(capture_sys.capture_handle);
    while ((uint32_t)(esp_timer_get_time() / 1000) < start_time + duration) {
        media_lib_thread_sleep(20);
        esp_capture_stream_frame_t frame = {
            .stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO,
        };
        while (esp_capture_sink_acquire_frame(capture_path, &frame, true) == ESP_CAPTURE_ERR_OK) {
            av_render_audio_data_t audio_data = {
                .data = frame.data,
                .size = frame.size,
                .pts = frame.pts,
            };
            av_render_add_audio_data(player_sys.player, &audio_data);
            esp_capture_sink_release_frame(capture_path, &frame);
        }
        frame.stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO;
        while (esp_capture_sink_acquire_frame(capture_path, &frame, true) == ESP_CAPTURE_ERR_OK) {
            av_render_video_data_t video_data = {
                .data = frame.data,
                .size = frame.size,
                .pts = frame.pts,
            };
            av_render_add_video_data(player_sys.player, &video_data);
            esp_capture_sink_release_frame(capture_path, &frame);
        }
    }
    esp_capture_stop(capture_sys.capture_handle);
    av_render_reset(player_sys.player);
    return 0;
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    // Install for media lib and set task scheduler
    media_lib_add_default_adapter();
    esp_capture_set_thread_scheduler(capture_scheduler);
    media_lib_thread_set_schedule_cb(thread_scheduler);
    // Initialize board and related codec lcd driver
    init_board();
    // Setup for media system
    media_sys_buildup();
    test_capture_to_player(30000);
}
