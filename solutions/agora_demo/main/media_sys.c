#include "media_sys.h"

#include <stdint.h>

#include "av_render.h"
#include "av_render_default.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_enc_default.h"
#include "esp_board_manager_defs.h"
#include "esp_board_manager_includes.h"
#include "esp_capture_defaults.h"
#include "esp_log.h"
#include "settings.h"

#define TAG "AGORA_MEDIA"
#define AUDIO_RENDER_FIFO_SIZE (16 * 1024)
#define AUDIO_PLAYOUT_THRESHOLD_MS 100

static esp_capture_handle_t capture;
static esp_capture_audio_src_if_t *audio_source;
static av_render_handle_t player;

static esp_codec_dev_handle_t get_record_handle(void)
{
    dev_audio_codec_handles_t *codec_handle = NULL;
    esp_err_t ret = esp_board_device_get_handle(
        ESP_BOARD_DEVICE_NAME_AUDIO_ADC, (void **)&codec_handle);
    if (ret != ESP_OK || codec_handle == NULL ||
        codec_handle->codec_dev == NULL) {
        ESP_LOGE(TAG, "Failed to get audio ADC handle");
        return NULL;
    }
    esp_codec_dev_set_in_gain(codec_handle->codec_dev, 32);
    return codec_handle->codec_dev;
}

static esp_codec_dev_handle_t get_playback_handle(void)
{
    dev_audio_codec_handles_t *codec_handle = NULL;
    esp_err_t ret = esp_board_device_get_handle(
        ESP_BOARD_DEVICE_NAME_AUDIO_DAC, (void **)&codec_handle);
    if (ret != ESP_OK || codec_handle == NULL ||
        codec_handle->codec_dev == NULL) {
        ESP_LOGE(TAG, "Failed to get audio DAC handle");
        return NULL;
    }
    esp_codec_dev_set_out_vol(codec_handle->codec_dev, 70);
    return codec_handle->codec_dev;
}

int media_sys_buildup(void)
{
    esp_audio_enc_register_default();
    esp_audio_dec_register_default();

    esp_codec_dev_handle_t record_handle = get_record_handle();
    if (record_handle == NULL) {
        return -1;
    }
    esp_capture_audio_dev_src_cfg_t source_cfg = {
        .record_handle = record_handle,
    };
    audio_source = esp_capture_new_audio_dev_src(&source_cfg);
    if (audio_source == NULL) {
        ESP_LOGE(TAG, "Failed to create the audio capture source");
        return -1;
    }

    esp_capture_cfg_t capture_cfg = {
        .sync_mode = ESP_CAPTURE_SYNC_MODE_NONE,
        .audio_src = audio_source,
        .video_src = NULL,
    };
    if (esp_capture_open(&capture_cfg, &capture) != ESP_CAPTURE_ERR_OK ||
        capture == NULL) {
        ESP_LOGE(TAG, "Failed to open audio capture");
        return -1;
    }

    esp_codec_dev_handle_t playback_handle = get_playback_handle();
    if (playback_handle == NULL) {
        return -1;
    }
    i2s_render_cfg_t render_cfg = {
        .fixed_clock = true,
        .play_handle = playback_handle,
    };
    audio_render_handle_t audio_render =
        av_render_alloc_i2s_render(&render_cfg);
    if (audio_render == NULL) {
        ESP_LOGE(TAG, "Failed to create the I2S audio renderer");
        return -1;
    }

    av_render_cfg_t player_cfg = {
        .audio_render = audio_render,
        .video_render = NULL,
        .audio_raw_fifo_size = 4 * 1024,
        .audio_render_fifo_size = AUDIO_RENDER_FIFO_SIZE,
        .allow_drop_data = false,
    };
    player = av_render_open(&player_cfg);
    if (player == NULL) {
        ESP_LOGE(TAG, "Failed to open the audio player");
        return -1;
    }

    uint32_t threshold = AUDIO_PLAYOUT_THRESHOLD_MS *
                         AGORA_DEMO_AUDIO_SAMPLE_RATE *
                         AGORA_DEMO_AUDIO_CHANNELS * sizeof(int16_t) / 1000;
    if (av_render_set_audio_threshold(player, threshold) != 0) {
        ESP_LOGE(TAG, "Failed to set the audio playout threshold");
        return -1;
    }

    ESP_LOGI(TAG, "Opus media ready: %d Hz mono, %d ms playout threshold",
             AGORA_DEMO_AUDIO_SAMPLE_RATE, AUDIO_PLAYOUT_THRESHOLD_MS);
    return 0;
}

int media_sys_get_provider(esp_webrtc_media_provider_t *provider)
{
    if (provider == NULL || capture == NULL || player == NULL) {
        return -1;
    }
    provider->capture = capture;
    provider->player = player;
    return 0;
}

int test_capture_to_player(void)
{
    return -1;
}

int play_music(const uint8_t *data, int size, int duration)
{
    (void)data;
    (void)size;
    (void)duration;
    return -1;
}

int stop_music(void)
{
    return -1;
}
