#include "common.h"

#include "esp_board_manager.h"
#include "esp_board_manager_defs.h"
#include "esp_log.h"

#define TAG "AGORA_BOARD"

int init_board(void)
{
    esp_err_t ret = esp_board_device_init(ESP_BOARD_DEVICE_NAME_AUDIO_ADC);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize audio ADC: %s",
                 esp_err_to_name(ret));
        return -1;
    }

    ret = esp_board_device_init(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize audio DAC: %s",
                 esp_err_to_name(ret));
        return -1;
    }

    ESP_LOGI(TAG, "Audio ADC and DAC are ready");
    return 0;
}
