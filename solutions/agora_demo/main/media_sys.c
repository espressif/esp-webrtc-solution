#include "media_sys.h"

#include <stdint.h>
#include <string.h>

#include "av_render.h"
#include "av_render_default.h"
#include "codec/audio_codec_adc_label.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_enc_default.h"
#include "esp_board_manager_defs.h"
#include "esp_board_manager_includes.h"
#include "esp_capture_defaults.h"
#include "esp_codec_dev.h"
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

static void fill_aec_source_cfg(esp_codec_dev_handle_t record_handle,
                                esp_capture_audio_aec_src_cfg_t *source_cfg)
{
    dev_audio_codec_config_t *adc_cfg = NULL;
    static char aec_mic_layout[9];
    const char *label = "FL,RE";
    uint16_t mask = 0x3;
    uint8_t channels = 2;
    if (esp_board_manager_get_device_config(ESP_BOARD_DEVICE_NAME_AUDIO_ADC, (void **)&adc_cfg) == ESP_OK &&
        adc_cfg != NULL && adc_cfg->codec_adc_cfg.label != NULL &&
        audio_codec_adc_label_parse(adc_cfg->codec_adc_cfg.label, &mask, &channels) == ESP_CODEC_DEV_OK &&
        channels > 0 && channels <= sizeof(aec_mic_layout) - 1 && mask > 0 && mask <= 0xFF) {
        label = adc_cfg->codec_adc_cfg.label;
    } else {
        mask = 0x3;
        channels = 2;
    }

    char data_label[32] = {0};
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = channels,
        .channel_mask = mask,
        .sample_rate = AGORA_DEMO_AUDIO_SAMPLE_RATE,
    };
    if (esp_codec_dev_open(record_handle, &fs) == ESP_CODEC_DEV_OK) {
        esp_codec_dev_get_data_layout_label(record_handle, data_label, sizeof(data_label));
        esp_codec_dev_close(record_handle);
    }
    const char *src = data_label[0] ? data_label : label;
    int n = 0;
    for (const char *p = src; *p && n < (int)sizeof(aec_mic_layout) - 1;) {
        while (*p == ' ' || *p == ',') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        char ch = strncmp(p, "NA", 2) == 0 ? 'N' : strncmp(p, "RE", 2) == 0 ? 'R' : 'M';
        if (ch != 'N') {
            aec_mic_layout[n++] = ch;
        }
        while (*p && *p != ',') {
            p++;
        }
    }
    aec_mic_layout[n] = '\0';
    source_cfg->mic_layout = aec_mic_layout;
    source_cfg->channel = channels;
    source_cfg->channel_mask = (uint8_t)mask;
    ESP_LOGI(TAG, "AEC mic_layout %s, channel %u, mask 0x%x", aec_mic_layout, channels, mask);
}

int media_sys_buildup(void)
{
    esp_audio_enc_register_default();
    esp_audio_dec_register_default();

    esp_codec_dev_handle_t record_handle = get_record_handle();
    if (record_handle == NULL) {
        return -1;
    }
    esp_capture_audio_aec_src_cfg_t source_cfg = {
        .record_handle = record_handle,
    };
    fill_aec_source_cfg(record_handle, &source_cfg);
    audio_source = esp_capture_new_audio_aec_src(&source_cfg);
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

    // Align playback I2S sample rate with record I2S
    av_render_audio_frame_info_t playback_info = {
        .sample_rate = AGORA_DEMO_AUDIO_SAMPLE_RATE,
        .channel = 2,
        .bits_per_sample = 16,
    };
    av_render_set_fixed_frame_info(player, &playback_info);

    uint32_t threshold = AUDIO_PLAYOUT_THRESHOLD_MS *
                         playback_info.sample_rate * playback_info.channel *
                         (playback_info.bits_per_sample / 8) / 1000;
    if (av_render_set_audio_threshold(player, threshold) != 0) {
        ESP_LOGE(TAG, "Failed to set the audio playout threshold");
        return -1;
    }

    ESP_LOGI(TAG, "Record media ready: %d Hz mono, AEC, %d ms playout threshold",
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

void media_sys_query(void)
{
    if (player != NULL) {
        av_render_query(player);
    }
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
