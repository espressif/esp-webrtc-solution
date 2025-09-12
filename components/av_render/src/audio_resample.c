/**
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2025 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include "audio_resample.h"
#include "esp_ae_ch_cvt.h"
#include "esp_ae_rate_cvt.h"
#include "esp_ae_bit_cvt.h"
#include "media_lib_os.h"
#include "esp_log.h"

#define TAG "RESAMPLE"

#define ELEMS(a) (sizeof(a) / sizeof(a[0]))

#define SAMPLE_SIZE(info) (info.channel * (info.bits_per_sample >> 3))

typedef struct {
    uint8_t *data;
    int      size;
    bool     used;
} work_buf_t;

typedef enum {
    RESAMPLE_OPS_NONE,
    RESAMPLE_OPS_CH_CVT,
    RESAMPLE_OPS_BIT_CVT,
    RESAMPLE_OPS_RATE_CVT,
} resample_ops_t;

typedef struct {
    audio_resample_cfg_t     cfg;
    esp_ae_ch_cvt_handle_t   ch_cvt_handle;
    esp_ae_rate_cvt_handle_t rate_cvt_handle;
    esp_ae_bit_cvt_handle_t  bit_cvt_handle;
    resample_ops_t           ops[3];
    work_buf_t               work_buf[2];
} resample_t;

static int add_bits_resample(resample_t *resample, audio_resample_cfg_t *cfg, int i)
{
    if (cfg->input_info.bits_per_sample > cfg->output_info.bits_per_sample) {
        resample->ops[i++] = RESAMPLE_OPS_BIT_CVT;
    }
    if (cfg->input_info.sample_rate != cfg->output_info.sample_rate) {
        resample->ops[i++] = RESAMPLE_OPS_RATE_CVT;
    }
    if (cfg->input_info.bits_per_sample < cfg->output_info.bits_per_sample) {
        resample->ops[i++] = RESAMPLE_OPS_BIT_CVT;
    }
    return i;
}

static void sort_resample_ops(resample_t *resample, audio_resample_cfg_t *cfg)
{
    int i = 0;
    if (cfg->input_info.channel > cfg->output_info.channel) {
        resample->ops[i++] = RESAMPLE_OPS_CH_CVT;
    }
    i = add_bits_resample(resample, cfg, i);
    if (cfg->input_info.channel < cfg->output_info.channel) {
        resample->ops[i++] = RESAMPLE_OPS_CH_CVT;
    }
}

static work_buf_t *alloc_work_buf(resample_t *resample, int size)
{
    for (int i = 0; i < ELEMS(resample->work_buf); i++) {
        if (resample->work_buf[i].used == false) {
            if (size > resample->work_buf[i].size) {
                uint8_t *new_buf = media_lib_realloc(resample->work_buf[i].data, size);
                if (new_buf == NULL) {
                    return NULL;
                }
                resample->work_buf[i].data = new_buf;
                resample->work_buf[i].size = size;
            }
            resample->work_buf[i].used = true;
            return &resample->work_buf[i];
        }
    }
    return NULL;
}

static void release_work_buf(work_buf_t *buf)
{
    if (buf) {
        buf->used = false;
    }
}

static int get_need_size(resample_t *resample, resample_ops_t op, uint32_t *sample, av_render_audio_frame_info_t *info)
{
    if (op == RESAMPLE_OPS_CH_CVT) {
        return (*sample * resample->cfg.output_info.channel * (info->bits_per_sample >> 3));
    }
    if (op == RESAMPLE_OPS_RATE_CVT) {
        uint32_t out_sample = 0;
        esp_ae_rate_cvt_get_max_out_sample_num(resample->rate_cvt_handle, *sample, &out_sample);
        *sample = out_sample;
        return out_sample * info->channel * (info->bits_per_sample >> 3);
    }
    if (op == RESAMPLE_OPS_BIT_CVT) {
        return (*sample * info->channel * (resample->cfg.output_info.bits_per_sample >> 3));
    }
    return 0;
}

audio_resample_handle_t audio_resample_open(audio_resample_cfg_t *cfg)
{
    resample_t *resample = (resample_t *)media_lib_calloc(1, sizeof(resample_t));
    do {
        if (resample == NULL) {
            break;
        }
        sort_resample_ops(resample, cfg);
        av_render_audio_frame_info_t cur_info = cfg->input_info;
        esp_ae_err_t ret = ESP_AE_ERR_OK;
        for (int i = 0; i < ELEMS(resample->ops); i++) {
            if (resample->ops[i] == RESAMPLE_OPS_NONE) {
                break;
            }
            if (resample->ops[i] == RESAMPLE_OPS_CH_CVT) {
                esp_ae_ch_cvt_cfg_t ch_cfg = {
                    .sample_rate = cur_info.sample_rate,
                    .bits_per_sample = cur_info.bits_per_sample,
                    .src_ch = cur_info.channel,
                    .dest_ch = cfg->output_info.channel,
                };
                ret = esp_ae_ch_cvt_open(&ch_cfg, &resample->ch_cvt_handle);
                if (ret != ESP_AE_ERR_OK) {
                    break;
                }
                cur_info.channel = cfg->output_info.channel;
            } else if (resample->ops[i] == RESAMPLE_OPS_RATE_CVT) {
                esp_ae_rate_cvt_cfg_t rate_cfg = {
                    .src_rate = cur_info.sample_rate,
                    .dest_rate = cfg->output_info.sample_rate,
                    .channel = cur_info.channel,
                    .bits_per_sample = cur_info.bits_per_sample,
                    .complexity = 2,
                    .perf_type = ESP_AE_RATE_CVT_PERF_TYPE_SPEED,
                };
                ret = esp_ae_rate_cvt_open(&rate_cfg, &resample->rate_cvt_handle);
                if (ret != ESP_AE_ERR_OK) {
                    break;
                }
                cur_info.sample_rate = cfg->output_info.sample_rate;
            } else if (resample->ops[i] == RESAMPLE_OPS_BIT_CVT) {
                esp_ae_bit_cvt_cfg_t bit_cfg = {
                    .channel = cur_info.channel,
                    .sample_rate = cur_info.sample_rate,
                    .src_bits = cur_info.bits_per_sample,
                    .dest_bits = cfg->output_info.bits_per_sample,
                };
                ret = esp_ae_bit_cvt_open(&bit_cfg, &resample->bit_cvt_handle);
                if (ret != ESP_AE_ERR_OK) {
                    break;
                }
                cur_info.bits_per_sample = cfg->output_info.bits_per_sample;
            }
        }
        if (ret != ESP_AE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to open AE for resample");
            break;
        }
        resample->cfg = *cfg;
        return resample;
    } while (0);
    audio_resample_close(resample);
    return NULL;
}

int audio_resample_write(audio_resample_handle_t h, av_render_audio_frame_t *data)
{
    resample_t *resample = (resample_t *)h;
    // Bypass or size is 0
    if (data->size == 0 || resample->ops[0] == RESAMPLE_OPS_NONE) {
        resample->cfg.resample_cb(data, resample->cfg.ctx);
        return ESP_MEDIA_ERR_OK;
    }
    av_render_audio_frame_info_t cur_info = resample->cfg.input_info;
    work_buf_t *cur = NULL;
    work_buf_t *last = NULL;
    uint32_t sample_num = data->size / SAMPLE_SIZE(cur_info);
    int need_size = 0;
    for (int i = 0; i < ELEMS(resample->ops); i++) {
        if (resample->ops[i] == RESAMPLE_OPS_NONE) {
            break;
        }
        uint32_t out_sample = sample_num;
        need_size = get_need_size(resample, resample->ops[i], &out_sample, &cur_info);
        cur = alloc_work_buf(resample, need_size);
        if (cur == NULL) {
            release_work_buf(last);
            return ESP_MEDIA_ERR_NO_MEM;
        }
        esp_ae_sample_t in_sample = (esp_ae_sample_t)(last ? last->data : data->data);
        if (resample->ops[i] == RESAMPLE_OPS_CH_CVT) {
            esp_ae_ch_cvt_process(resample->ch_cvt_handle, sample_num, in_sample, (esp_ae_sample_t)cur->data);
            cur_info.channel = resample->cfg.output_info.channel;
        } else if (resample->ops[i] == RESAMPLE_OPS_RATE_CVT) {
            esp_ae_rate_cvt_process(resample->rate_cvt_handle, in_sample, sample_num, (esp_ae_sample_t)cur->data, &out_sample);
            need_size = out_sample * SAMPLE_SIZE(cur_info);
            cur_info.sample_rate = resample->cfg.output_info.sample_rate;
            sample_num = out_sample;
        } else if (resample->ops[i] == RESAMPLE_OPS_BIT_CVT) {
            esp_ae_bit_cvt_process(resample->bit_cvt_handle, sample_num, in_sample, (esp_ae_sample_t)cur->data);
            cur_info.bits_per_sample = resample->cfg.output_info.bits_per_sample;
        }
        if (last) {
            release_work_buf(last);
        }
        last = cur;
    }
    release_work_buf(cur);
    av_render_audio_frame_t new_frame = *data;
    new_frame.data = cur->data;
    new_frame.size = need_size;
    resample->cfg.resample_cb(&new_frame, resample->cfg.ctx);
    return ESP_MEDIA_ERR_OK;
}

void audio_resample_close(audio_resample_handle_t h)
{
    resample_t *resample = (resample_t *)h;
    if (resample == NULL) {
        return;
    }
    if (resample->bit_cvt_handle) {
        esp_ae_bit_cvt_close(resample->bit_cvt_handle);
        resample->bit_cvt_handle = NULL;
    }
    if (resample->ch_cvt_handle) {
        esp_ae_ch_cvt_close(resample->ch_cvt_handle);
        resample->ch_cvt_handle = NULL;
    }
    if (resample->rate_cvt_handle) {
        esp_ae_rate_cvt_close(resample->rate_cvt_handle);
        resample->rate_cvt_handle = NULL;
    }
    for (int i = 0; i < ELEMS(resample->work_buf); i++) {
        if (resample->work_buf[i].data) {
            media_lib_free(resample->work_buf[i].data);
            resample->work_buf[i].data = NULL;
        }
    }
    media_lib_free(resample);
}
