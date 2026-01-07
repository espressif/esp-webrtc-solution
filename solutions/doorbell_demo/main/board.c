/* Do simple board initialize

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include "esp_log.h"
#include "codec_init.h"
#include "codec_board.h"
#include "esp_codec_dev.h"
#include "sdkconfig.h"
#include "settings.h"
#if CONFIG_IDF_TARGET_ESP32P4
#include "driver/gpio.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "esp_cam_sensor_xclk.h"
#include <driver/rtc_io.h>
#include "esp_sleep.h"
#endif

static const char *TAG = "Board";

static int enable_p4_eye_camera(camera_cfg_t *cfg, bool enable)
{
    if (cfg->pwr == -1) {
        return 0;
    }
    int ret = 0;
#if CONFIG_IDF_TARGET_ESP32P4
    esp_cam_sensor_xclk_handle_t xclk_handle = NULL;
    if (enable) {
        esp_cam_sensor_xclk_config_t cam_xclk_config = {
            .esp_clock_router_cfg = {
                .xclk_pin = cfg->xclk,
                .xclk_freq_hz = 24 * 1000000,
            }
        };
        esp_cam_sensor_xclk_allocate(ESP_CAM_SENSOR_XCLK_ESP_CLOCK_ROUTER, &xclk_handle);
        esp_cam_sensor_xclk_start(xclk_handle, &cam_xclk_config);
    }
#endif
    return ret;
}

void init_board()
{
    ESP_LOGI(TAG, "Init board.");
    set_default_codec_board();
    // Notes when use playback and record at same time, must set reuse_dev = false
    codec_init_cfg_t cfg = {.reuse_dev = false};
    if (strcmp(CONFIG_CODEC_BOARD, "ESP32_P4_EYE") == 0) {
        cfg.in_mode = CODEC_I2S_MODE_PDM;
        init_i2c(0);
        camera_cfg_t camera_cfg = {};
        get_camera_cfg(&camera_cfg);
        enable_p4_eye_camera(&camera_cfg, true);
    }
    init_codec(&cfg);
}
