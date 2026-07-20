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
#include <sdkconfig.h>
#include "color_convert.h"
#include "esp_log.h"
#include "esp_idf_version.h"
#include "esp_imgfx_color_convert.h"

#define TAG "CLR_CONVERT"

#define COLOR_LIMIT(a) (a > 255 ? 255 : a < 0 ? 0 \
                                              : a)

#define YUV2RO(C, D, E) COLOR_LIMIT((298 * (C) + 409 * (E) + 128) >> 8)
#define YUV2GO(C, D, E) COLOR_LIMIT((298 * (C)-100 * (D)-208 * (E) + 128) >> 8)
#define YUV2BO(C, D, E) COLOR_LIMIT((298 * (C) + 516 * (D) + 128) >> 8)
#define RGB565(r, g, b) (((((r) << 6) | (g)) << 5) | (b))

typedef struct {
    uint8_t                     *table;
    av_render_video_frame_type_t from;
    av_render_video_frame_type_t to;
    int                          width;
    int                          height;
#if CONFIG_IDF_TARGET_ESP32P4
    esp_imgfx_color_convert_handle_t convert_hd;
#endif
} color_convert_t;

static int get_table_size(av_render_video_frame_type_t from, av_render_video_frame_type_t to)
{
    if (from == AV_RENDER_VIDEO_RAW_TYPE_YUV420 && to == AV_RENDER_VIDEO_RAW_TYPE_RGB565_BE) {
        return 256 * 256 * 2;
    }
    if (from == AV_RENDER_VIDEO_RAW_TYPE_YUV420 && to == AV_RENDER_VIDEO_RAW_TYPE_RGB565) {
        return 256 * 256 * 2;
    }
    return 0;
}

static int init_table(color_convert_t *convert)
{
    uint16_t *table16 = (uint16_t *)convert->table;
    if (convert->from == AV_RENDER_VIDEO_RAW_TYPE_YUV420 && convert->to == AV_RENDER_VIDEO_RAW_TYPE_RGB565_BE) {
        for (int u0 = 0; u0 < 32; u0++) {
            for (int v0 = 0; v0 < 32; v0++) {
                for (int y0 = 0; y0 < 64; y0++) {
                    int idx = (y0 << 10) + (u0 << 5) + v0;
                    int y = (y0 << 2) + (y0 & 0x3);
                    int u = (u0 << 3) + (y0 & 0x7);
                    int v = (v0 << 3) + (v0 & 0x7);
                    y -= 16;
                    u -= 128;
                    v -= 128;
                    uint16_t r = (YUV2RO(y, u, v) >> 3) & 0x1f;
                    uint16_t g = (YUV2GO(y, u, v) >> 2) & 0x3f;
                    uint16_t b = (YUV2BO(y, u, v) >> 3) & 0x1f;
                    table16[idx] = RGB565(r, g, b);
                    table16[idx] = (table16[idx] >> 8) | (table16[idx] << 8);
                }
            }
        }
    }
    if (convert->from == AV_RENDER_VIDEO_RAW_TYPE_YUV420 && convert->to == AV_RENDER_VIDEO_RAW_TYPE_RGB565) {
        for (int u0 = 0; u0 < 32; u0++) {
            for (int v0 = 0; v0 < 32; v0++) {
                for (int y0 = 0; y0 < 64; y0++) {
                    int idx = (y0 << 10) + (u0 << 5) + v0;
                    int y = (y0 << 2) + (y0 & 0x3);
                    int u = (u0 << 3) + (y0 & 0x7);
                    int v = (v0 << 3) + (v0 & 0x7);
                    y -= 16;
                    u -= 128;
                    v -= 128;
                    uint16_t r = (YUV2RO(y, u, v) >> 3) & 0x1f;
                    uint16_t g = (YUV2GO(y, u, v) >> 2) & 0x3f;
                    uint16_t b = (YUV2BO(y, u, v) >> 3) & 0x1f;
                    table16[idx] = RGB565(r, g, b);
                }
            }
        }
    }
    return 0;
}

int convert_table_get_image_size(av_render_video_frame_type_t fmt, int width, int height)
{
    switch (fmt) {
        case AV_RENDER_VIDEO_RAW_TYPE_YUV420:
            return width * height * 3 / 2;
        case AV_RENDER_VIDEO_RAW_TYPE_RGB565:
        case AV_RENDER_VIDEO_RAW_TYPE_RGB565_BE:
            return width * height * 2;
        default:
            ESP_LOGE(TAG, "Not supported format %d", fmt);
            break;
    }
    return 0;
}

color_convert_table_t init_convert_table(color_convert_cfg_t *cfg)
{
    color_convert_t *convert = (color_convert_t *)calloc(1, sizeof(color_convert_t));
    do {
        if (convert == NULL) {
            break;
        }
        convert->from = cfg->from;
        convert->to = cfg->to;
        convert->width = cfg->width;
        convert->height = cfg->height;
#if CONFIG_IDF_TARGET_ESP32P4
        if (convert->from == AV_RENDER_VIDEO_RAW_TYPE_YUV420 && convert->to == AV_RENDER_VIDEO_RAW_TYPE_RGB565) {
            esp_imgfx_color_convert_cfg_t convert_cfg = {
                .in_res = {
                    .width = cfg->width,
                    .height = cfg->height,
                },
                .in_pixel_fmt = ESP_IMGFX_PIXEL_FMT_I420,
                .out_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB565_LE,
                .color_space_std = ESP_IMGFX_COLOR_SPACE_STD_BT709,
            };
            esp_imgfx_color_convert_open(&convert_cfg, &convert->convert_hd);
            if (convert->convert_hd == NULL) {
                break;
            }
            return (color_convert_table_t)convert;
        }
#endif
        int table_size = get_table_size(cfg->from, cfg->to);
        if (table_size) {
            convert->table = (uint8_t *)malloc(table_size);
            if (convert->table == NULL) {
                break;
            }
        }
        init_table(convert);
        return (color_convert_table_t)convert;
    } while (0);
    deinit_convert_table(convert);
    return NULL;
}

static void yuv420_to_rgb565(color_convert_t *convert, uint8_t *src, uint8_t *dst)
{
    uint8_t *y_plane = src;
    uint8_t *u_plane = src + (convert->width * convert->height);
    uint8_t *v_plane = src + (convert->width * convert->height * 5 / 4);
    int y_pos = 0, u_pos = 0, rgb_idx = 0;
    uint16_t *rgb565 = (uint16_t *)dst;
    uint16_t *table16 = (uint16_t *)convert->table;
    for (int i = 0; i < convert->height; i++) {
        for (int j = 0; j < convert->width; j += 2) {
            int y = y_plane[y_pos];
            int u = u_plane[u_pos];
            int v = v_plane[u_pos];
            int y_idx = (y >> 2) << 10;
            int uv_idx = ((u >> 3) << 5) + (v >> 3);

            rgb565[rgb_idx++] = table16[y_idx + uv_idx];

            y = y_plane[y_pos + 1];
            y_idx = (y >> 2) << 10;
            rgb565[rgb_idx++] = table16[y_idx + uv_idx];
            y_pos += 2;
            u_pos++;
        }
        // odd line not increase
        if ((i & 1) == 0) {
            u_pos -= convert->width >> 1;
        }
    }
}

int convert_color(color_convert_table_t table, uint8_t *src, int src_size, uint8_t *dst, int dst_size)
{
    color_convert_t *convert = (color_convert_t *)table;
    if (convert->from == AV_RENDER_VIDEO_RAW_TYPE_YUV420 && convert->to == AV_RENDER_VIDEO_RAW_TYPE_RGB565) {
#if CONFIG_IDF_TARGET_ESP32P4
        esp_imgfx_data_t from_image = {
            .data = src,
            .data_len = src_size,
        };
        esp_imgfx_data_t to_image = {
            .data = dst,
            .data_len = dst_size,
        };
        esp_imgfx_color_convert_process(convert->convert_hd, &from_image, &to_image);
        return 0;
#else
        int src_need = convert->width * convert->height * 3 / 2;
        int dst_need = convert->width * convert->height * 2;
        if (src_size != src_need || dst_size < dst_need) {
            ESP_LOGE(TAG, "size dismatch");
            return -1;
        }
        yuv420_to_rgb565(convert, src, dst);
        return 0;
#endif
    }

    switch (convert->from) {
        case AV_RENDER_VIDEO_RAW_TYPE_YUV420: {
            int src_need = convert->width * convert->height * 3 / 2;
            int dst_need = convert->width * convert->height * 2;
            if (src_size != src_need || dst_size < dst_need) {
                ESP_LOGE(TAG, "size dismatch");
                return -1;
            }
            switch (convert->to) {
                case AV_RENDER_VIDEO_RAW_TYPE_RGB565:
                case AV_RENDER_VIDEO_RAW_TYPE_RGB565_BE:
                    yuv420_to_rgb565(convert, src, dst);
                    break;
                default:
                    ESP_LOGE(TAG, "Bad format to %d", convert->to);
                    return -1;
            }
        } break;
        default:
            ESP_LOGE(TAG, "Bad format from %d", convert->from);
            *(int *)0 = 0;
            return -1;
    }
    return 0;
}

void deinit_convert_table(color_convert_table_t t)
{
    color_convert_t *convert = (color_convert_t *)t;
    if (convert) {
#if CONFIG_IDF_TARGET_ESP32P4
        if (convert->convert_hd) {
            esp_imgfx_color_convert_close(convert->convert_hd);
            convert->convert_hd = NULL;
        }
#endif
        if (convert->table) {
            free(convert->table);
            convert->table = NULL;
        }
        free(convert);
    }
}
