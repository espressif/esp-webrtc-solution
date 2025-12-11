#include <sdkconfig.h>
#include "codec_board.h"
#include "esp_log.h"

#define TAG "BOARD"

board_section_t *get_codec_section(const char *codec_type);
board_section_t *parse_codec_section(const char *section);

static board_section_t *codec;

#define RET_ON_NOT_INIT() if (codec == NULL) {   \
    return -1;                                   \
}

int codec_board_parse_all_config(const char *section)
{
    if (codec) {
        return 0;
    }
    codec = parse_codec_section(section);
    return codec == NULL ? -1 : 0;
}

void set_codec_board_type(const char *codec_type)
{
    if (codec) {
        return;
    }
    codec = get_codec_section(codec_type);
}

void set_default_codec_board(void)
{
    if (codec) {
        return;
    }
    set_codec_board_type(CONFIG_CODEC_BOARD);
}

int get_sdcard_config(sdcard_cfg_t *card_cfg)
{
    RET_ON_NOT_INIT();
    if (codec->sdcard_num == 0) {
        ESP_LOGE(TAG, "Sdcard not exits on board");
        return -1;
    }
    memcpy(card_cfg, &codec->sdcard, sizeof(sdcard_cfg_t));
    return 0;
}

int get_i2c_pin(uint8_t port, codec_i2c_pin_t *i2c_pin)
{
    RET_ON_NOT_INIT();
    if (port > codec->i2c_num) {
        ESP_LOGE(TAG, "I2C %d not exits on board", port);
        return -1;
    }
    memcpy(i2c_pin, &codec->i2c_pin[port], sizeof(codec_i2c_pin_t));
    return 0;
}

int get_i2s_pin(uint8_t port, codec_i2s_pin_t *i2s_pin)
{
    RET_ON_NOT_INIT();
    if (port > codec->i2s_num) {
        ESP_LOGE(TAG, "I2S %d not exits on board", port);
        return -1;
    }
    memcpy(i2s_pin, &codec->i2s_pin[port], sizeof(codec_i2s_pin_t));
    return 0;
}

int get_out_codec_cfg(codec_cfg_t *out_cfg)
{
    RET_ON_NOT_INIT();
    for (int i = 0; i < codec->codec_num; i++) {
        if (codec->codec[i].codec_dir & CODEC_DIR_OUT) {
            memcpy(out_cfg, &codec->codec[i].codec_cfg, sizeof(codec_cfg_t));
            return 0;
        }
    }
    ESP_LOGE(TAG, "Output codec not exits on board");
    return -1;
}

int get_in_codec_cfg(codec_cfg_t *in_cfg)
{
    RET_ON_NOT_INIT();
    for (int i = 0; i < codec->codec_num; i++) {
        if (codec->codec[i].codec_dir & CODEC_DIR_IN) {
            memcpy(in_cfg, &codec->codec[i].codec_cfg, sizeof(codec_cfg_t));
            return 0;
        }
    }
    ESP_LOGE(TAG, "Input codec not exits on board");
    return -1;
}

int get_camera_cfg(camera_cfg_t *cam_cfg)
{
    RET_ON_NOT_INIT();
    if (codec->camera_num == 0) {
        ESP_LOGE(TAG, "Camera not exits on board");
        return -1;
    }
    memcpy(cam_cfg, &codec->camera, sizeof(camera_cfg_t));
    return 0;
}

int get_lcd_cfg(lcd_cfg_t *lcd_cfg)
{
    RET_ON_NOT_INIT();
    if (codec->lcd_num) {
        memcpy(lcd_cfg, &codec->lcd, sizeof(lcd_cfg_t));
        return 0;
    }
    ESP_LOGE(TAG, "LCD not exits on board");
    return -1;
}
