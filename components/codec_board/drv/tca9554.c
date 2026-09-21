/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2021 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
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

#include <string.h>
#include "esp_log.h"
#include "tca9554.h"
#include "codec_init.h"
#include "audio_codec_ctrl_if.h"
#include "esp_codec_dev_defaults.h"

static char *TAG = "TCA9554";

#define SET_BITS(_m, _s, _v)  ((_v) ? (_m)|((_s)) : (_m)&~((_s)))
#define GET_BITS(_m, _s)      (((_m) & (_s)) ? true : false)

#define TCA9554_INPUT_PORT              0x00
#define TCA9554_OUTPUT_PORT             0x01
#define TCA9554_POLARITY_INVERSION_PORT 0x02
#define TCA9554_CONFIGURATION_PORT      0x03

typedef struct {
    uint8_t addr;
    char *name;
} tca9554_dev_t;

static tca9554_dev_t dev_list[] = {
    { 0x70, "TCA9554A"},
    { 0x40, "TCA9554"},
};

const static audio_codec_ctrl_if_t *i2c_ctrl;

static esp_err_t expander_dev_prob(uint8_t port)
{
    audio_codec_i2c_cfg_t i2c_cfg = { 0 };
    for (size_t i = 0; i < sizeof(dev_list) / sizeof(dev_list[0]); i++) {
        i2c_cfg.addr = dev_list[i].addr;
        i2c_cfg.bus_handle = get_i2c_bus_handle(port);
        i2c_ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
        uint8_t data = 0;
        if (i2c_ctrl->read_reg(i2c_ctrl, TCA9554_OUTPUT_PORT, 1, &data, 1) == 0) {
            ESP_LOGI(TAG, "Detected IO expander device at 0x%02X, name is: %s",
                     dev_list[i].addr, dev_list[i].name);
            return ESP_OK;
        }
        audio_codec_delete_ctrl_if(i2c_ctrl);
        i2c_ctrl = NULL;
    }
    ESP_LOGE(TAG, "IO expander device has not detected");
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t tca9554_write_reg(uint8_t reg_addr, uint8_t data)
{
    return i2c_ctrl->write_reg(i2c_ctrl, reg_addr, 1, &data, 1);
}

static char tca9554_read_reg(uint8_t reg_addr)
{
    uint8_t data;
    i2c_ctrl->read_reg(i2c_ctrl, reg_addr, 1, &data, 1);
    return data;
}


esp_tca9554_io_level_t tca9554_get_input_state(esp_tca9554_gpio_num_t gpio_num)
{
    char data = 0;
    if (gpio_num < TCA9554_GPIO_NUM_MAX) {
        data = tca9554_read_reg(TCA9554_INPUT_PORT);
    } else {
        ESP_LOGE(TAG, "gpio num is error, current gpio: %d", gpio_num);
        return TCA9554_LEVEL_ERROR;
    }
    return GET_BITS(data, gpio_num);
}

esp_tca9554_io_level_t tca9554_get_output_state(esp_tca9554_gpio_num_t gpio_num)
{
    char data = 0;
    if (gpio_num < TCA9554_GPIO_NUM_MAX) {
        data = tca9554_read_reg(TCA9554_OUTPUT_PORT);
    } else {
        ESP_LOGE(TAG, "gpio num is error, current gpio: %d", gpio_num);
        return TCA9554_LEVEL_ERROR;
    }

    return GET_BITS(data, gpio_num);
}

esp_err_t tca9554_set_output_state(esp_tca9554_gpio_num_t gpio_num, esp_tca9554_io_level_t level)
{
    char data;
    esp_err_t res = ESP_FAIL;
    if (gpio_num < TCA9554_GPIO_NUM_MAX) {
        data = tca9554_read_reg(TCA9554_OUTPUT_PORT);
        res = tca9554_write_reg(TCA9554_OUTPUT_PORT, SET_BITS(data, gpio_num, level));
    } else {
        ESP_LOGE(TAG, "gpio num is error, current gpio: %d", gpio_num);
    }
    return res;
}

esp_err_t tca9554_set_polarity_inversion(esp_tca9554_gpio_num_t gpio_num, esp_tca9554_io_polarity_t polarity)
{
    char data;
    esp_err_t res = ESP_FAIL;
    if (gpio_num < TCA9554_GPIO_NUM_MAX) {
        data = tca9554_read_reg(TCA9554_POLARITY_INVERSION_PORT);
        res = tca9554_write_reg(TCA9554_POLARITY_INVERSION_PORT, SET_BITS(data, gpio_num, polarity));
    } else {
        ESP_LOGE(TAG, "gpio num is error, current gpio: %d", gpio_num);
    }
    return res;
}

esp_tca9554_io_config_t tca9554_get_io_config(esp_tca9554_gpio_num_t gpio_num)
{
    char data = 0;
    if (gpio_num < TCA9554_GPIO_NUM_MAX) {
        data = tca9554_read_reg(TCA9554_CONFIGURATION_PORT);
    } else {
        ESP_LOGE(TAG, "gpio num is error, current gpio: %d", gpio_num);
        return TCA9554_LEVEL_ERROR;
    }

    return GET_BITS(data, gpio_num);
}

esp_err_t tca9554_set_io_config(esp_tca9554_gpio_num_t gpio_num, esp_tca9554_io_config_t io_config)
{
    char data;
    esp_err_t res = ESP_FAIL;
    if (gpio_num < TCA9554_GPIO_NUM_MAX) {
        data = tca9554_read_reg(TCA9554_CONFIGURATION_PORT);
        res = tca9554_write_reg(TCA9554_CONFIGURATION_PORT, SET_BITS(data, gpio_num, io_config));
    } else {
        ESP_LOGE(TAG, "gpio num is error, current gpio: %d", gpio_num);
    }
    return res;
}

esp_err_t tca9554_init(uint8_t port)
{
    return expander_dev_prob(port);
}

esp_err_t tca9554_deinit()
{
    if (i2c_ctrl) {
        audio_codec_delete_ctrl_if(i2c_ctrl);
        i2c_ctrl = NULL;
    }
    return ESP_OK;
}
