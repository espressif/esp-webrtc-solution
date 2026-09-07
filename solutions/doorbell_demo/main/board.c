/* Do simple board initialize

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include "esp_log.h"
#include "esp_board_manager.h"
#include "esp_board_manager_defs.h"

static const char *TAG = "Board";

void init_board()
{
    ESP_LOGI(TAG, "Init board.");
    // Initialize for camera and audio devices
    esp_err_t ret;
    ret = esp_board_device_init(ESP_BOARD_DEVICE_NAME_AUDIO_ADC);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init audio ADC device");
        return;
    }
    ret = esp_board_device_init(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init audio DAC device");
    }
    ret = esp_board_device_init(ESP_BOARD_DEVICE_NAME_CAMERA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init camera device");
        return;
    }
}
