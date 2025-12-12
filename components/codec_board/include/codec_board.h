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

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Maximum of supported I2C number
 */
#define MAX_I2C_NUM (2)

/**
 * @brief  Maximum of supported I2S number
 */
#define MAX_I2S_NUM (2)

/**
 * @brief  Maximum of supported codec number
 */
#define MAX_CODEC_NUM (2)

/**
 * @brief  Codec input and output direction
 */
#define CODEC_DIR_IN     (1 << 0)
#define CODEC_DIR_OUT    (1 << 1)
#define CODEC_DIR_IN_OUT (CODEC_DIR_IN | CODEC_DIR_OUT)

/**
 * @brief  Start of extended GPIO start
 */
#define BOARD_EXTEND_IO_START (0x1000)

/**
 * @brief  Codec type
 */
typedef enum {
    CODEC_TYPE_NONE,   /*!< Codec type none */
    CODEC_TYPE_ES8311, /*!< ES8311 codec type */
    CODEC_TYPE_ES7210, /*!< ES7210 codec type */
    CODEC_TYPE_ES7243, /*!< ES7243 codec type */
    CODEC_TYPE_ES8388, /*!< ES8388 codec type */
    CODEC_TYPE_DUMMY,  /*!< Dummy codec type (which have I2S interface only) */
} codec_type_t;

/**
 * @brief  Camera type
 */
typedef enum {
    CAMERA_TYPE_NONE, /*!< Camera type none */
    CAMERA_TYPE_DVP,  /*!< DVP camera type */
    CAMERA_TYPE_USB,  /*!< USB camera type */
    CAMERA_TYPE_MIPI  /*!< MIPI camera type */
} camera_type_t;

/**
 * @brief  I2C pin setting
 */
typedef struct {
    int16_t sda; /*!< GPIO for SDA */
    int16_t scl; /*!< GPIO for SCL */
} codec_i2c_pin_t;

/**
 * @brief  I2S pin setting
 */
typedef struct {
    int16_t mclk; /*!< GPIO for MCLK */
    int16_t bclk; /*!< GPIO for BCLK */
    int16_t ws;   /*!< GPIO for Word Selction */
    int16_t dout; /*!< GPIO for digital output */
    int16_t din;  /*!< GPIO for digital input */
} codec_i2s_pin_t;

/**
 * @brief  Codec configuration
 */
typedef struct {
    codec_type_t codec_type; /*!< Codec type */
    int16_t      pa_pin;     /*!< GPIO for PA control */
    float        pa_gain;    /*!< PA gain */
    uint8_t      i2c_addr;   /*!< I2C address */
    int8_t       i2c_port;   /*!< I2C port */
    int8_t       i2s_port;   /*!< I2S port */
    bool         use_mclk;   /*!< Whether codec need MCLK clock */
} codec_cfg_t;

/**
 * @brief  Sdcard configuration
 */
typedef struct {
    int16_t clk;   /*!< GPIO for clock */
    int16_t cmd;   /*!< GPIO for command */
    int16_t d0;    /*!< GPIO for d0 */
    int16_t d1;    /*!< GPIO for d1 (if only one line need set to -1) */
    int16_t d2;    /*!< GPIO for d2 (if only one line need set to -1) */
    int16_t d3;    /*!< GPIO for d3 (if only one line need set to -1) */
    int16_t power; /*!< GPIO for power */
} sdcard_cfg_t;

/**
 * @brief  Camera configuration
 */
typedef struct {
    camera_type_t type;     /*!< Camera type */
    int16_t       pwr;      /*!< GPIO for power */
    int16_t       reset;    /*!< GPIO for reset */
    int16_t       xclk;     /*!< GPIO for XCLK */
    int16_t       pclk;     /*!< GPIO for PCLK */
    int16_t       vsync;    /*!< GPIO for VSYNC */
    int16_t       de;       /*!< GPIO for DE */
    int16_t       href;     /*!< GPIO for HREF */
    int16_t       data[16]; /*!< GPIO for DATA */
} camera_cfg_t;

/**
 * @brief  Codec setting
 */
typedef struct {
    codec_cfg_t codec_cfg; /*!< Codec configuration */
    uint8_t     codec_dir; /*!< Codec direction */
} codec_setting_t;

/**
 * @brief  Extent IO board type
 */
typedef enum {
    EXTENT_IO_TYPE_NONE,    /*!< NOne extent IO */
    EXTENT_IO_TYPE_TCA9554, /*!< TCA9554 extent IO type */
} extend_io_type_t;

/**
 * @brief  LCD controller type
 */
typedef enum {
    LCD_CONTROLLER_TYPE_NONE,   /*!< None controller type */
    LCD_CONTROLLER_TYPE_ST7789, /*!< ST7789 controller type */
} lcd_controller_type_t;

/**
 * @brief  LCD bus type
 */
typedef enum {
    LCD_BUS_TYPE_NONE, /*!< None LCD bus type */
    LCD_BUS_TYPE_SPI,  /*!< SPI LCD bus type */
    LCD_BUS_TYPE_RGB,  /*!< RGB LCD bus type */
    LCD_BUS_TYPE_I80,  /*!< I80 LCD bus type */
    LCD_BUS_TYPE_MIPI, /*!< MIPI LCD bus type */
} lcd_bus_type_t;

/**
 * @brief  SPI LCD configuration
 */
typedef struct {
    uint8_t spi_bus;    /*!< SPI bus number */
    int     pclk_clk;   /*!< PCLK clock */
    uint8_t cmd_bits;   /*!< Command bit width */
    uint8_t param_bits; /*!< Parameter bit width */
    int16_t cs;         /*!< CS GPIO */
    int16_t dc;         /*!< DC GPIO */
    int16_t clk;        /*!< Clock GPIO */
    int16_t mosi;       /*!< MOSI GPIO */
    int16_t d[7];       /*!< Data GPIOs */
} lcd_spi_cfg_t;

/**
 * @brief  MIPI LCD configuration
 */
typedef struct {
    uint8_t  ldo_chan;
    uint16_t ldo_voltage;
    uint8_t  lane_num;
    uint32_t lane_bitrate; // Mbps
    uint32_t dpi_clk;      // MHz
    uint8_t  bit_depth;
    uint8_t  fb_num;
    uint8_t  dsi_hsync;
    uint8_t  dsi_vsync;
    uint8_t  dsi_hbp;
    uint8_t  dsi_hfp;
    uint8_t  dsi_vbp;
    uint8_t  dsi_vfp;
} lcd_mipi_cfg_t;

/**
 * @brief LCD configuration
 */
typedef struct {
    extend_io_type_t      io_type;
    uint8_t               io_i2c_port;
    lcd_bus_type_t        bus_type;
    lcd_controller_type_t controller;
    int                   width;
    int                   height;
    uint8_t               i2c_port;
    uint8_t               mirror_x  : 1;
    uint8_t               mirror_y  : 1;
    uint8_t               swap_xy   : 1;
    uint8_t               color_inv : 1;
    int16_t               ctrl_pin;
    int16_t               reset_pin;
    union {
        lcd_spi_cfg_t  spi_cfg;
        lcd_mipi_cfg_t mipi_cfg;
    };
} lcd_cfg_t;

/**
 * @brief Board section
 */
typedef struct {
    codec_i2c_pin_t i2c_pin[MAX_I2C_NUM];
    codec_i2s_pin_t i2s_pin[MAX_I2S_NUM];
    codec_setting_t codec[MAX_CODEC_NUM];
    lcd_cfg_t       lcd;
    sdcard_cfg_t    sdcard;
    camera_cfg_t    camera;
    uint8_t         i2c_num;
    uint8_t         i2s_num;
    uint8_t         codec_num;
    uint8_t         sdcard_num;
    uint8_t         lcd_num;
    uint8_t         camera_num;
} board_section_t;

/**
 * @brief  Parse board all configurations from string
 *
 * @note  Section string like following:
 *        "i2c: {sda: 2, scl: 1}
 *         i2s: {bclk: 40, ws: 39, dout: 41, din: 15, mclk: 42}
 *         out: {codec: ES8311, pa: 4, use_mclk: 1, pa_gain:6}
 *         in: {codec: ES7210}
 *         sdcard: {clk: 16, cmd: 38, d0: 17}
 *        "
 *
 * @param[in]  section  Board section string
 *
 * @return
 *       - 0   Parse OK
 *       - -1  Failed to parse
 *
 */
int codec_board_parse_all_config(const char *section);

/**
 * @brief  Set codec board type
 *
 * @param[in]  board_type  Board name selection
 */
void set_codec_board_type(const char *board_type);

/**
 * @brief  Set default codec board
 */
void set_default_codec_board(void);

/**
 * @brief  Get SDCard configuration
 *
 * @param[out]  card_cfg  SDCard configuration to store
 *
 * @return
 *       - 0   On success
 *       - -1  Not exists
 *
 */
int get_sdcard_config(sdcard_cfg_t *card_cfg);

/**
 * @brief  Get I2C pin setting by port
 *
 * @param[in]   port     I2C port
 * @param[out]  i2c_pin  I2C pin setting
 *
 * @return
 *       - 0   On success
 *       - -1  Not exists
 *
 */
int get_i2c_pin(uint8_t port, codec_i2c_pin_t *i2c_pin);

/**
 * @brief  Get I2S pin setting by port
 *
 * @param[in]   port     I2S port
 * @param[out]  i2s_pin  I2S pin setting
 *
 * @return
 *       - 0   On success
 *       - -1  Not exists
 */
int get_i2s_pin(uint8_t port, codec_i2s_pin_t *i2s_pin);

/**
 * @brief  Get output codec configuration
 *
 * @param[out]   out_cfg  Output codec configuration to store
 *
 * @return
 *       - 0   On success
 *       - -1  Not exists
 */
int get_out_codec_cfg(codec_cfg_t *out_cfg);

/**
 * @brief  Get input codec configuration
 *
 * @param[out]   in_cfg  Input codec configuration to store
 *
 * @return
 *       - 0   On success
 *       - -1  Not exists
 */
int get_in_codec_cfg(codec_cfg_t *in_cfg);

/**
 * @brief  Get LCD configuration
 *
 * @param[out]   lcd_cfg  LCD configuration to store
 *
 * @return
 *       - 0   On success
 *       - -1  Not exists
 */
int get_lcd_cfg(lcd_cfg_t *lcd_cfg);

/**
 * @brief  Get camera configuration
 *
 * @param[out]   cam_cfg  Camera configuration to store
 *
 * @return
 *       - 0   On success
 *       - -1  Not exists
 */
int get_camera_cfg(camera_cfg_t *cam_cfg);

#ifdef __cplusplus
}
#endif
