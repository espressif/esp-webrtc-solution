/* Media system

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "av_render.h"
#include "av_render_default.h"
#include "common.h"
#include "esp_log.h"
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

static bool           music_playing  = false;
static bool           music_stopping = false;
static const uint8_t *music_to_play;
static int            music_size;
static int            music_duration;

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
#if CONFIG_IDF_TARGET_ESP32S31
    esp_capture_audio_info_t aud_info = {
        .format_id = ESP_CAPTURE_FMT_ID_PCM,
        .sample_rate = 8000,
        .channel = 1,
        .bits_per_sample = 16,
    };
    capture_sys.aud_src->set_fixed_caps(capture_sys.aud_src, &aud_info);
#endif
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
        cfg->rgb_panel= true;
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
#if CONFIG_IDF_TARGET_ESP32S31
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 8000,
        .channel = 2,
        .bits_per_sample = 16,
    };
    av_render_audio_frame_info_t fixed_info = {
        .sample_rate = 8000,
        .channel = 2,
        .bits_per_sample = 16,
    };
    av_render_set_fixed_frame_info(player_sys.player, &fixed_info);
    esp_codec_dev_open(get_playback_handle(), &fs);
#endif
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

int test_capture_to_player(void)
{
    esp_capture_sink_cfg_t sink_cfg = {
        .audio_info = {
            .format_id = ESP_CAPTURE_FMT_ID_PCM,
            .sample_rate = 16000,
            .channel = 1,
            .bits_per_sample = 16,
        },
        .video_info = { .format_id = ESP_CAPTURE_FMT_ID_H264, .width = VIDEO_WIDTH, .height = VIDEO_HEIGHT, .fps = VIDEO_FPS },
    };
    // Create capture
    esp_capture_sink_handle_t capture_path = NULL;
    esp_capture_sink_setup(capture_sys.capture_handle, 0, &sink_cfg, &capture_path);
    esp_capture_sink_enable(capture_path, ESP_CAPTURE_RUN_MODE_ALWAYS);
    // Create player
    av_render_audio_info_t render_aud_info = {
        .codec = AV_RENDER_AUDIO_CODEC_PCM,
        .sample_rate = 16000,
        .channel = 1,
        .bits_per_sample = 16,
    };
    av_render_add_audio_stream(player_sys.player, &render_aud_info);

    av_render_video_info_t render_vid_info = {
        .codec = AV_RENDER_VIDEO_CODEC_H264,
    };
    av_render_add_video_stream(player_sys.player, &render_vid_info);
    uint32_t start_time = (uint32_t)(esp_timer_get_time() / 1000);
    esp_capture_start(capture_sys.capture_handle);
    uint32_t video_frame_num = 0;
    int audio_dumped = 0;
    while ((uint32_t)(esp_timer_get_time() / 1000) < start_time + 5000) {
        media_lib_thread_sleep(10);
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
            int16_t*pcm_data = (int16_t *)frame.data;
            printf("%d %d %d %d\n", pcm_data[0], pcm_data[1], pcm_data[2], pcm_data[3]);
            esp_capture_sink_release_frame(capture_path, &frame);
            if (audio_dumped == false) {
                esp_codec_dev_dump_reg(get_playback_handle());
                audio_dumped = true;
            }
        }
        frame.stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO;
        while (esp_capture_sink_acquire_frame(capture_path, &frame, true) == ESP_CAPTURE_ERR_OK) {
            av_render_video_data_t video_data = {
                .data = frame.data,
                .size = frame.size,
                .pts = frame.pts,
            };
            //av_render_add_video_data(player_sys.player, &video_data);
            esp_capture_sink_release_frame(capture_path, &frame);
            video_frame_num++;
        }
    }
    uint32_t end_time = (uint32_t) (esp_timer_get_time() / 1000);
    esp_capture_stop(capture_sys.capture_handle);
    av_render_reset(player_sys.player);
    uint32_t fps = video_frame_num * 1000 / (end_time - start_time);
    ESP_LOGI(TAG, "Capture video fps:%d", (int)fps);
    return 0;
}

static void music_play_thread(void *arg)
{
    // Suppose all music is AAC
    av_render_audio_info_t render_aud_info = {
        .codec = AV_RENDER_AUDIO_CODEC_AAC,
    };
    av_render_add_audio_stream(player_sys.player, &render_aud_info);
    int music_pos = 0;
    while (!music_stopping && music_duration >= 0) {
        uint32_t start_time = esp_timer_get_time() / 1000;
        int send_size = music_size - music_pos;
        const uint8_t *adts_header = music_to_play + music_pos;
        if (adts_header[0] != 0xFF) {
            send_size = 0;
        } else {
            int frame_size = ((adts_header[3] & 0x03) << 11) | (adts_header[4] << 3) | (adts_header[5] >> 5);
            if (frame_size < send_size) {
                send_size = frame_size;
            }
        }
        if (send_size) {
            av_render_audio_data_t audio_data = {
                .data = (uint8_t *)adts_header,
                .size = send_size,
            };
            int ret = av_render_add_audio_data(player_sys.player, &audio_data);
            if (ret != 0) {
                break;
            }
            music_pos += send_size;
        }
        if (music_pos >= music_size || send_size == 0) {
            music_pos = 0;
            // Play one loop only
            if (music_duration == 0) {
                av_render_fifo_stat_t stat = { 0 };
                while (!music_stopping) {
                    av_render_get_audio_fifo_level(player_sys.player, &stat);
                    if (stat.data_size > 0) {
                        media_lib_thread_sleep(50);
                        continue;
                    }
                    break;
                }
                break;
            }
        }
        uint32_t end_time = esp_timer_get_time() / 1000;
        if (music_duration) {
            music_duration -= end_time - start_time;
        }
    }
    av_render_reset(player_sys.player);
    music_stopping = false;
    music_playing = false;
    media_lib_thread_destroy(NULL);
}

int play_music(const uint8_t *data, int size, int duration)
{
    if (music_playing) {
        ESP_LOGE(TAG, "Music is playing, stop automatically");
        stop_music();
    }
    music_playing = true;
    music_to_play = data;
    music_size = size;
    music_duration = duration;
    media_lib_thread_handle_t thread;
    int ret = media_lib_thread_create_from_scheduler(&thread, "music_player", music_play_thread, NULL);
    if (ret != 0) {
        music_playing = false;
        ESP_LOGE(TAG, "Fail to create music_player thread");
        return ret;
    }
    return 0;
}

int stop_music()
{
    if (music_playing) {
        music_stopping = true;
        while (music_stopping) {
            media_lib_thread_sleep(20);
        }
    }
    return 0;
}
