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

#include "video_decoder.h"
#include "media_lib_os.h"
#include "media_lib_err.h"
#include "esp_log.h"
#include "color_convert.h"
#include "esp_heap_caps.h"
#include "esp_video_dec.h"
#include "esp_video_codec_utils.h"

#define TAG "VID_DEC"

typedef struct {
    av_render_video_frame_info_t frame_info;
    esp_video_dec_handle_t       dec_handle;
    uint8_t                     *out_data;
    uint8_t                     *frame_data;
    int                          frame_data_size;
    uint32_t                     out_size;
    bool                         header_parsed;
    vdec_frame_cb                frame_cb;
    void                        *ctx;
    esp_video_codec_pixel_fmt_t  dec_out_fmt;
    uint8_t                      out_frame_align;
    bool                         need_clr_convert;
    color_convert_table_t        convert_table;
    uint8_t                     *raw_buffer;
    int                          raw_buffer_size;
    vdec_fb_cb_cfg_t             fb_cb;
} vdec_t;

static esp_video_codec_type_t get_codec_type(av_render_video_codec_t codec)
{
    switch (codec) {
        case AV_RENDER_VIDEO_CODEC_MJPEG:
            return ESP_VIDEO_CODEC_TYPE_MJPEG;
        case AV_RENDER_VIDEO_CODEC_H264:
            return ESP_VIDEO_CODEC_TYPE_H264;
        default:
            return ESP_VIDEO_CODEC_TYPE_NONE;
    }
}

static esp_video_codec_pixel_fmt_t get_out_fmt(av_render_video_frame_type_t out_type)
{
    switch (out_type) {
        case AV_RENDER_VIDEO_RAW_TYPE_YUV420:
            return ESP_VIDEO_CODEC_PIXEL_FMT_YUV420P;
        case AV_RENDER_VIDEO_RAW_TYPE_RGB565:
            return ESP_VIDEO_CODEC_PIXEL_FMT_RGB565_LE;
        case AV_RENDER_VIDEO_RAW_TYPE_RGB565_BE:
            return ESP_VIDEO_CODEC_PIXEL_FMT_RGB565_BE;
        default:
            return ESP_VIDEO_CODEC_TYPE_NONE;
    }
}

static av_render_video_frame_type_t get_frame_type(esp_video_codec_pixel_fmt_t fmt)
{
    switch (fmt) {
        case ESP_VIDEO_CODEC_PIXEL_FMT_RGB565_LE:
            return AV_RENDER_VIDEO_RAW_TYPE_RGB565;
        case ESP_VIDEO_CODEC_PIXEL_FMT_YUV420P:
            return AV_RENDER_VIDEO_RAW_TYPE_YUV420;
        case ESP_VIDEO_CODEC_PIXEL_FMT_RGB565_BE:
            return AV_RENDER_VIDEO_RAW_TYPE_RGB565_BE;
        default:
            return ESP_VIDEO_CODEC_TYPE_NONE;
    }
}

static int do_decode(vdec_t *vdec, av_render_video_data_t *data, av_render_video_frame_t *out_frame)
{
    int ret = -1;
    esp_video_dec_in_frame_t in_frame = {
        .pts = data->pts,
        .data = data->data,
        .size = data->size,
    };
    esp_video_dec_out_frame_t decoded_frame = {};
    if (vdec->header_parsed == false) {
        vdec->out_size = 64;
        vdec->out_data = esp_video_codec_align_alloc(vdec->out_frame_align, vdec->out_size, &vdec->out_size);
        decoded_frame.data = vdec->out_data;
        decoded_frame.size = vdec->out_size;
        ret = esp_video_dec_process(vdec->dec_handle, &in_frame, &decoded_frame);
        if (ret != ESP_VC_ERR_BUF_NOT_ENOUGH) {
            return ESP_MEDIA_ERR_BAD_DATA;
        }
        esp_video_codec_free(vdec->out_data);
        vdec->out_data = NULL;
        esp_video_codec_frame_info_t frame_info = {};
        ret = esp_video_dec_get_frame_info(vdec->dec_handle, &frame_info);
        if (ret != ESP_VC_ERR_OK) {
            return ESP_MEDIA_ERR_NOT_SUPPORT;
        }
        vdec->frame_info.width = frame_info.res.width;
        vdec->frame_info.height = frame_info.res.height;
        ESP_LOGI(TAG, "Video resolution %dx%d fmt:%d", (int)frame_info.res.width, (int)frame_info.res.height, vdec->dec_out_fmt);
        vdec->out_size = esp_video_codec_get_image_size(vdec->dec_out_fmt, &frame_info.res);
        /* Only allocate out_data if:
         * 1. No frame_data is set AND no fb_fetch callback is available, OR
         * 2. Color conversion is needed BUT frame_data is not set (frame_data can be used for decoder output even with color conversion)
         * This allows pre-allocated frame_data from SPIRAM to be used instead of allocating from internal RAM
         */
        if ((vdec->frame_data == NULL && vdec->fb_cb.fb_fetch == NULL) || (vdec->need_clr_convert && vdec->frame_data == NULL)) {
            // TODO this middle buffer not needed if can get decoder output frame directly
            vdec->out_data = esp_video_codec_align_alloc(vdec->out_frame_align, vdec->out_size, &vdec->out_size);
            if (vdec->out_data == NULL) {
                ESP_LOGE(TAG, "No memory for decode output size %d", (int)vdec->out_size);
                return ESP_MEDIA_ERR_NO_MEM;
            }
        }
        if (vdec->need_clr_convert) {
            color_convert_cfg_t color_cfg = {
                .width = frame_info.res.width,
                .height = frame_info.res.height,
                .from = get_frame_type(vdec->dec_out_fmt),
                .to = vdec->frame_info.type,
            };
            vdec->convert_table = init_convert_table(&color_cfg);
            if (vdec->convert_table == NULL) {
                ESP_LOGE(TAG, "No memory for color convert from %d to %d", color_cfg.from, color_cfg.to);
                return -1;
            }
            vdec->raw_buffer_size = esp_video_codec_get_image_size(get_out_fmt(vdec->frame_info.type), &frame_info.res);
            /* Always allocate raw_buffer as fallback - fb_fetch may fail if data queue is too small */
            /* This ensures we have a buffer even if fb_fetch returns NULL */
            if (vdec->raw_buffer == NULL) {
                /* Use SPIRAM for raw_buffer allocation - RGB565 buffer is too large for internal RAM */
                /* Cache-align for potential cache sync operations (64-byte alignment) */
                const size_t cache_line_size = 64;
                size_t aligned_size = ((vdec->raw_buffer_size + cache_line_size - 1) / cache_line_size) * cache_line_size;
                vdec->raw_buffer = (uint8_t *)heap_caps_aligned_alloc(cache_line_size, aligned_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (vdec->raw_buffer == NULL) {
                    ESP_LOGE(TAG, "Fail to allocate cache-aligned convert output from SPIRAM (size: %d, aligned: %zu)",
                             (int)vdec->raw_buffer_size, aligned_size);
                    return ESP_MEDIA_ERR_NO_MEM;
                }
                vdec->raw_buffer_size = aligned_size;  /* Use aligned size for cache sync */
            }
        }
        vdec->header_parsed = true;
    }
    if (vdec->need_clr_convert == false) {
        if (vdec->fb_cb.fb_fetch) {
            decoded_frame.data = vdec->fb_cb.fb_fetch(vdec->out_frame_align, vdec->out_size, vdec->fb_cb.ctx);
        } else {
            decoded_frame.data = vdec->frame_data ? vdec->frame_data : vdec->out_data;
        }
    } else {
        decoded_frame.data = vdec->out_data;
    }
    decoded_frame.size = vdec->out_size;
    ret = esp_video_dec_process(vdec->dec_handle, &in_frame, &decoded_frame);
    if (ret != ESP_VC_ERR_OK) {
        printf("Data size %d ", (int)data->size);
#define CC(a) a[0], a[1], a[2], a[3]
        printf("%02x %02x %02x %02x\n", CC(data->data));
#define CC1(a, size) a[size - 4], a[size - 3], a[size - 2], a[size - 1]
        printf("Tail %02x %02x %02x %02x\n", CC1(data->data, data->size));
        ESP_LOGE(TAG, "Failed to decode data: ret %d (header_parsed=%d, pts=%" PRIu32 ", key_frame=%d)",
                 ret, vdec->header_parsed, data->pts, data->key_frame);
        /* Check if this is likely due to missing SPS/PPS or corrupted decoder state */
        if (!vdec->header_parsed) {
            ESP_LOGE(TAG, "Decoder header not parsed - SPS/PPS may be missing or invalid");
        } else {
            ESP_LOGE(TAG, "Decoder header was parsed but decode failed - decoder state may be corrupted");
        }
        if (vdec->fb_cb.fb_fetch && vdec->need_clr_convert == false) {
            vdec->fb_cb.fb_return(decoded_frame.data, true, vdec->fb_cb.ctx);
        }
        return ret;
    }
    if (vdec->need_clr_convert == false) {
        out_frame->pts = data->pts;
        out_frame->data = decoded_frame.data;
        out_frame->size = vdec->out_size;
        return ESP_MEDIA_ERR_OK;
    }

    uint8_t *out_data = vdec->frame_data ? vdec->frame_data : vdec->raw_buffer;
    bool using_fb_buffer = false;  /* Track if we're using a buffer from fb_fetch */
    if (vdec->fb_cb.fb_fetch) {
        uint8_t *fb_data = vdec->fb_cb.fb_fetch(vdec->out_frame_align, vdec->raw_buffer_size, vdec->fb_cb.ctx);
        /* Fallback to raw_buffer if fb_fetch fails (e.g., data queue too small) */
        if (fb_data == NULL) {
            ESP_LOGW(TAG, "fb_fetch returned NULL, falling back to raw_buffer");
            out_data = vdec->raw_buffer;
            if (out_data == NULL) {
                ESP_LOGE(TAG, "raw_buffer is also NULL - cannot decode");
                return ESP_MEDIA_ERR_NO_MEM;
            }
            using_fb_buffer = false;  /* Using raw_buffer, not fb_fetch */
        } else {
            out_data = fb_data;
            using_fb_buffer = true;  /* Using buffer from fb_fetch */
        }
    }
    ret = convert_color(vdec->convert_table, decoded_frame.data, decoded_frame.decoded_size,
                        out_data, vdec->raw_buffer_size);
    if (ret != 0) {
        ESP_LOGE(TAG, "Fail to convert color");
        /* Only call fb_return if we're using a buffer from fb_fetch */
        if (using_fb_buffer && vdec->fb_cb.fb_return) {
            vdec->fb_cb.fb_return(out_data, true, vdec->fb_cb.ctx);
        }
        return ret;
    }
    out_frame->pts = decoded_frame.pts;
    out_frame->data = out_data;
    out_frame->size = vdec->raw_buffer_size;
    return ESP_MEDIA_ERR_OK;
}

static int check_input_format_support(vdec_t *vdec, esp_video_dec_cfg_t *dec_cfg, av_render_video_frame_type_t out_fmt)
{
    // Check whether RGB565 supported by decoder
    esp_video_codec_query_t query = {
        .codec_type = dec_cfg->codec_type,
    };
    esp_video_dec_caps_t caps = {};
    esp_video_dec_query_caps(&query, &caps);
    if (caps.out_fmt_num == 0) {
        return ESP_MEDIA_ERR_NOT_SUPPORT;
    }
    esp_video_codec_pixel_fmt_t dec_out_fmt = get_out_fmt(out_fmt);
    for (int i = 0; i < caps.out_fmt_num; i++) {
        if (caps.out_fmts[i] == dec_out_fmt) {
            dec_cfg->out_fmt = dec_out_fmt;
            return ESP_MEDIA_ERR_OK;
        }
    }
    vdec->need_clr_convert = true;
    // Prefer to use first one
    dec_cfg->out_fmt = caps.out_fmts[0];
    ESP_LOGI(TAG, "Need color covert to covert from %d to %d", dec_cfg->out_fmt, get_out_fmt(out_fmt));
    return ESP_MEDIA_ERR_OK;
}

int vdec_get_output_formats(av_render_video_codec_t codec, av_render_video_frame_type_t* fmts, uint8_t* num)
{
     esp_video_codec_query_t query = {
        .codec_type = get_codec_type(codec),
    };
    esp_video_dec_caps_t caps = {};
    esp_video_dec_query_caps(&query, &caps);
    if (caps.out_fmt_num == 0) {
        return ESP_MEDIA_ERR_NOT_SUPPORT;
    }
    if (*num < caps.out_fmt_num) {
        return ESP_MEDIA_ERR_EXCEED_LIMIT;
    }
    for (int i = 0; i < caps.out_fmt_num; i++) {
        fmts[i] = get_frame_type(caps.out_fmts[i]);
    }
    *num = caps.out_fmt_num;
    return ESP_MEDIA_ERR_OK;
}

vdec_handle_t vdec_open(vdec_cfg_t *cfg)
{
    if (cfg == NULL) {
        return NULL;
    }
    vdec_t *vdec = (vdec_t *)media_lib_calloc(1, sizeof(vdec_t));
    if (vdec == NULL) {
        return NULL;
    }
    // Save input arguments
    vdec->frame_info.width = cfg->video_info.width;
    vdec->frame_info.height = cfg->video_info.height;
    vdec->frame_info.fps = cfg->video_info.fps;
    vdec->frame_cb = cfg->frame_cb;
    vdec->ctx = cfg->ctx;

    av_render_video_frame_type_t output_type = cfg->out_type;
    if (output_type == AV_RENDER_VIDEO_RAW_TYPE_NONE) {
        output_type = AV_RENDER_VIDEO_RAW_TYPE_RGB565;
    }
    esp_video_dec_cfg_t dec_cfg = {
        .codec_type = get_codec_type(cfg->video_info.codec),
    };
    do {
        int ret = check_input_format_support(vdec, &dec_cfg, output_type);
        if (ret != ESP_MEDIA_ERR_OK) {
            ESP_LOGE(TAG, "Format %d not support by %d", get_out_fmt(output_type), dec_cfg.codec_type);
            break;
        }
        ret = esp_video_dec_open(&dec_cfg, &vdec->dec_handle);
        if (ret == 0 && vdec->dec_handle) {
            uint8_t in_align = 0;
            esp_video_dec_get_frame_align(vdec->dec_handle, &in_align, &vdec->out_frame_align);
            vdec->frame_info.type = output_type;
            vdec->dec_out_fmt = dec_cfg.out_fmt;
            return vdec;
        }
    } while (0);
    vdec_close(vdec);
    return NULL;
}

int vdec_set_fb_cb(vdec_handle_t h, vdec_fb_cb_cfg_t *cfg)
{
    vdec_t *vdec = (vdec_t *)h;
    if (vdec == NULL || cfg->fb_fetch == NULL || cfg->fb_return == NULL) {
        return ESP_MEDIA_ERR_INVALID_ARG;
    }
    vdec->fb_cb = *cfg;
    return ESP_MEDIA_ERR_OK;
}

int vdec_decode(vdec_handle_t h, av_render_video_data_t *data)
{
    vdec_t *vdec = (vdec_t *)h;
    if (vdec == NULL || data == NULL) {
        return ESP_MEDIA_ERR_INVALID_ARG;
    }
    av_render_video_frame_t out_frame = {
        .eos = data->eos,
        .pts = data->pts
    };
    int ret = do_decode(vdec, data, &out_frame);
    if (ret == 0 && vdec->frame_cb) {
        vdec->header_parsed = true;
        vdec->frame_cb(&out_frame, vdec->ctx);
        /* Only call fb_return if the buffer came from fb_fetch, not from raw_buffer or frame_data */
        /* Check if out_frame->data is NOT raw_buffer and NOT frame_data to determine if it came from fb_fetch */
        if (vdec->fb_cb.fb_fetch && out_frame.data &&
            out_frame.data != vdec->raw_buffer &&
            out_frame.data != vdec->frame_data) {
            vdec->fb_cb.fb_return(out_frame.data, false, vdec->fb_cb.ctx);
        }
    }
    return ret;
}

int vdec_set_frame_buffer(vdec_handle_t h, av_render_frame_buffer_t *buffer)
{
    vdec_t *vdec = (vdec_t *)h;
    if (vdec == NULL || buffer == NULL) {
        return ESP_MEDIA_ERR_INVALID_ARG;
    }
    vdec->frame_data = buffer->data;
    // Notes make sure frame buffer size big than JPEG image size
    if (buffer->size > 0) {
        vdec->frame_data_size = buffer->size;
    }
    return ESP_MEDIA_ERR_OK;
}

int vdec_get_frame_info(vdec_handle_t h, av_render_video_frame_info_t *frame_info)
{
    vdec_t *vdec = (vdec_t *)h;
    if (vdec == NULL || frame_info == NULL) {
        return ESP_MEDIA_ERR_INVALID_ARG;
    }
    if (vdec->header_parsed == false) {
        return ESP_MEDIA_ERR_NOT_SUPPORT;
    }
    memcpy(frame_info, &vdec->frame_info, sizeof(av_render_video_frame_info_t));
    return ESP_MEDIA_ERR_OK;
}

int vdec_close(vdec_handle_t h)
{
    vdec_t *vdec = (vdec_t *)h;
    if (vdec == NULL) {
        return ESP_MEDIA_ERR_INVALID_ARG;
    }
    if (vdec->dec_handle) {
        esp_video_dec_close(vdec->dec_handle);
        vdec->dec_handle = NULL;
    }
    if (vdec->out_data) {
        free(vdec->out_data);
        vdec->out_data = NULL;
    }
    if (vdec->convert_table) {
        deinit_convert_table(vdec->convert_table);
        vdec->convert_table = NULL;
    }
    if (vdec->raw_buffer) {
        heap_caps_free(vdec->raw_buffer);
        vdec->raw_buffer = NULL;
    }
    media_lib_free(vdec);
    return ESP_MEDIA_ERR_OK;
}
