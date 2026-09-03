#include "common.h"

#include "codec_board.h"
#include "codec_init.h"
#include "esp_log.h"
#include "sdkconfig.h"

#define TAG "AGORA_BOARD"

int init_board(void)
{
    set_codec_board_type(CONFIG_CODEC_BOARD);

    codec_init_cfg_t cfg = {
        .reuse_dev = false,
    };
    int ret = init_codec(&cfg);
    if (ret != 0 || get_record_handle() == NULL ||
        get_playback_handle() == NULL) {
        ESP_LOGE(TAG, "Codec initialization failed for board=%s ret=%d",
                 CONFIG_CODEC_BOARD, ret);
        return -1;
    }

    ESP_LOGI(TAG, "Codec ready: board=%s record=%p playback=%p",
             CONFIG_CODEC_BOARD, get_record_handle(), get_playback_handle());
    return 0;
}
