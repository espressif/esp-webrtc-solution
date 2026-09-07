/* Media system

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "av_render.h"
#include "av_render_default.h"
#include "common.h"
#include "settings.h"
#include "media_lib_os.h"
#include "esp_timer.h"
#include "esp_audio_enc_default.h"
#include "esp_video_enc_default.h"
#include "esp_video_dec_default.h"
#include "esp_audio_dec_default.h"
#include "esp_capture_defaults.h"
#include "esp_capture_sink.h"
#include "esp_board_manager_defs.h"
#include "esp_board_manager_includes.h"
#include "esp_log.h"

#define TAG "MEDIA_SYS"

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

static esp_capture_video_src_if_t *create_video_source(void)
{
#ifdef CONFIG_ESP_BOARD_DEV_CAMERA_SUPPORT
    dev_camera_handle_t *camera_handle = NULL;
    esp_err_t ret = esp_board_device_get_handle(ESP_BOARD_DEVICE_NAME_CAMERA, (void **)&camera_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get camera device");
        return NULL;
    }
    esp_capture_video_v4l2_src_cfg_t v4l2_cfg = {
        .buf_count = 2,
    };
    strncpy(v4l2_cfg.dev_name, camera_handle->dev_path, sizeof(v4l2_cfg.dev_name) - 1);
    return esp_capture_new_video_v4l2_src(&v4l2_cfg);
#else
    return NULL;
#endif
}

static esp_codec_dev_handle_t get_record_handle(void)
{
    dev_audio_codec_handles_t *codec_handle = NULL;
    esp_err_t ret = esp_board_device_get_handle(ESP_BOARD_DEVICE_NAME_AUDIO_ADC, (void **)&codec_handle);
    if (ret == ESP_OK) {
        esp_codec_dev_set_in_gain(codec_handle->codec_dev, 32);
        return codec_handle->codec_dev;
    }
    return NULL;
}

static esp_codec_dev_handle_t get_playback_handle(void)
{
    dev_audio_codec_handles_t *codec_handle = NULL;
    esp_err_t ret = esp_board_device_get_handle(ESP_BOARD_DEVICE_NAME_AUDIO_DAC, (void **)&codec_handle);
    if (ret == ESP_OK) {
        esp_codec_dev_set_out_vol(codec_handle->codec_dev, 70);
        return codec_handle->codec_dev;
    }
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

static int get_lcd_config(lcd_render_cfg_t *cfg)
{
    dev_display_lcd_config_t *dev_cfg = NULL;
    esp_board_manager_get_device_config(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, (void **)&dev_cfg);
    if (dev_cfg == NULL) {
        ESP_LOGE(TAG, "Failed to get display config");
        return -1;
    }
    dev_display_lcd_handles_t *lcd_handle = NULL;
    esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, (void **)&lcd_handle);
    if (lcd_handle == NULL || lcd_handle->panel_handle == NULL) {
        ESP_LOGE(TAG, "No display found");
        return -1;
    }
    cfg->lcd_handle = lcd_handle->panel_handle;
    if (strcmp(dev_cfg->sub_type, "rgb") == 0) {
        cfg->rgb_panel = true;
    } else if (strcmp(dev_cfg->sub_type, "dsi") == 0) {
        cfg->dsi_panel = true;
    }
    if (cfg->rgb_panel || cfg->dsi_panel) {
        dev_display_lcd_config_t override_cfg = *dev_cfg;
        if (cfg->rgb_panel) {
#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_RGB_SUPPORT
            override_cfg.sub_cfg.rgb.panel_config.num_fbs = 2;
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_RGB_SUPPORT */
        } else if (cfg->dsi_panel) {
#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_DSI_SUPPORT
            override_cfg.sub_cfg.dsi.dpi_config.num_fbs = 2;
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_DSI_SUPPORT */
        }
        // Turn on dual frame buffer for RGB or DSI panel to avoid tearing
        esp_board_device_override_config(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, &override_cfg, sizeof(dev_display_lcd_config_t));
        ESP_LOGI(TAG, "LCD configuration overridden");
    }
    return 0;
}

static int build_player_system()
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
    lcd_render_cfg_t lcd_cfg = {0};
    if (get_lcd_config(&lcd_cfg) < 0) {
        ESP_LOGE(TAG, "Fail to get lcd config");
    } else {
        player_sys.video_render = av_render_alloc_lcd_render(&lcd_cfg);
        if (player_sys.video_render == NULL) {
            ESP_LOGE(TAG, "Fail to create video render");
            // Allow not display
        }
    }
    av_render_cfg_t render_cfg = {
        .audio_render = player_sys.audio_render,
        .video_render = player_sys.video_render,
        .audio_raw_fifo_size = 4096,
        .audio_render_fifo_size = 6 * 1024,
        .video_raw_fifo_size = 500 * 1024,
        .allow_drop_data = false,
        //.video_render_fifo_size = 4*1024,
    };
    player_sys.player = av_render_open(&render_cfg);
    if (player_sys.player == NULL) {
        ESP_LOGE(TAG, "Fail to create player");
        return -1;
    }
    return 0;
}

int media_sys_buildup(void)
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

int media_sys_get_provider(esp_webrtc_media_provider_t *provide)
{
    provide->capture = capture_sys.capture_handle;
    provide->player = player_sys.player;
    return 0;
}
