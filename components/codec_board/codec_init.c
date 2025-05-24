#include <string.h>
#include <stdint.h>
#include "driver/i2c.h"
#include "esp_idf_version.h"
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "soc/soc_caps.h"
#else
#include "driver/i2s.h"
#endif
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev_os.h"
#include "codec_board.h"
#include "codec_init.h"
#include "esp_log.h"
#include "dummy_codec.h"
#include "driver/i2c_master.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"

#define TAG "CODEC_INIT"

typedef struct {
    bool                         inited;
    const audio_codec_data_if_t *data_if;
    const audio_codec_data_if_t *data_in_if;
    const audio_codec_gpio_if_t *gpio_if;
    const audio_codec_ctrl_if_t *in_ctrl_if;
    const audio_codec_ctrl_if_t *out_ctrl_if;
    const audio_codec_if_t      *out_codec_if;
    const audio_codec_if_t      *in_codec_if;
    esp_codec_dev_handle_t       play_dev;
    esp_codec_dev_handle_t       record_dev;
} codec_res_t;

#define USE_I2C_MASTER
typedef struct {
    bool inited;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    i2s_chan_handle_t tx_handle;
    i2s_chan_handle_t rx_handle;
#endif
} i2s_keep_t;

static bool                    i2c_inited[MAX_I2C_NUM];
static i2c_master_bus_handle_t i2c_bus_handle[MAX_I2C_NUM];
static i2s_keep_t             *i2s_keep[MAX_I2S_NUM];
static codec_res_t             codec_res;

static int _i2c_init(uint8_t port)
{
    if (port >= MAX_I2C_NUM) {
        return -1;
    }
    if (i2c_inited[port]) {
        return 0;
    }
    codec_i2c_pin_t i2c_pin;
    if (get_i2c_pin(port, &i2c_pin)) {
        ESP_LOGE(TAG, "Fail to get i2c pin");
        return -1;
    }
    // workaround to check i2c initialized already
    int ret = 0;
#ifdef USE_I2C_MASTER
    i2c_master_bus_config_t i2c_bus_config = { 0 };
    i2c_bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_bus_config.i2c_port = port;
    i2c_bus_config.scl_io_num = i2c_pin.scl;
    i2c_bus_config.sda_io_num = i2c_pin.sda;
    i2c_bus_config.glitch_ignore_cnt = 7;
    i2c_bus_config.flags.enable_internal_pullup = true;
    ret = i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle[port]);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize I2C master bus port %d", port);
        return -1;
    }
    ESP_LOGI(TAG, "Set mater handle %d %p", port, i2c_bus_handle[port]);
#else
    i2c_config_t i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    i2c_cfg.sda_io_num = i2c_pin.sda;
    i2c_cfg.scl_io_num = i2c_pin.scl;
    ret = i2c_param_config(port, &i2c_cfg);
    if (ret != ESP_OK) {
        return -1;
    }
    ret = i2c_driver_install(port, i2c_cfg.mode, 0, 0, 0);
#endif
    if (ret == 0) {
        i2c_inited[port] = true;
    }
    return ret;
}

void *get_i2c_bus_handle(uint8_t port)
{
    ESP_LOGI(TAG, "Get mater handle %d %p", port, i2c_bus_handle[port]);
    return i2c_bus_handle[port];
}

static int _i2c_deinit(uint8_t port)
{
    if (port >= MAX_I2C_NUM || i2c_inited[port] == false) {
        return -1;
    }
#ifdef USE_I2C_MASTER
    i2c_del_master_bus(i2c_bus_handle[port]);
#else
    i2c_driver_delete(port);
#endif
    i2c_bus_handle[port] = NULL;
    i2c_inited[port] = false;
    return 0;
}

static int _i2s_init(uint8_t port, esp_codec_dev_type_t dev_type, codec_init_cfg_t *init_cfg)
{
    if (port >= MAX_I2S_NUM) {
        return -1;
    }
    if (i2s_keep[port]) {
        return 0;
    }
    codec_i2s_pin_t i2s_cfg;
    if (get_i2s_pin(port, &i2s_cfg)) {
        ESP_LOGE(TAG, "Fail to get i2s pin");
        return -1;
    }
    ESP_LOGI(TAG, "Init i2s %d type: %d mclk:%d bclk:%d ws:%d din:%d dout:%d",
             port, dev_type, i2s_cfg.mclk,
             i2s_cfg.bclk, i2s_cfg.ws, i2s_cfg.din, i2s_cfg.dout);
    i2s_keep[port] = (i2s_keep_t *)calloc(1, sizeof(i2s_keep_t));
    if (i2s_keep[port] == NULL) {
        return -1;
    }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(32, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = i2s_cfg.mclk,
            .bclk = i2s_cfg.bclk,
            .ws = i2s_cfg.ws,
            .dout = i2s_cfg.dout,
            .din = i2s_cfg.din,
        },
    };
    bool output = (dev_type & ESP_CODEC_DEV_TYPE_OUT) > 0;
    bool input = (dev_type & ESP_CODEC_DEV_TYPE_IN) > 0;
    // Same I2S have tx and rx but use rx or tx only
    if (output && init_cfg->out_mode == CODEC_I2S_MODE_NONE) {
        output = false;
    }
    if (input && init_cfg->in_mode == CODEC_I2S_MODE_NONE) {
        input = false;
    }
    if (input == false && output == false) {
        return 0;
    }
#ifdef SOC_I2S_SUPPORTS_TDM
    i2s_tdm_slot_mask_t slot_mask = I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3;
    i2s_tdm_config_t tdm_cfg = {
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(32, I2S_SLOT_MODE_STEREO, slot_mask),
        .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(16000),
        .gpio_cfg = {
            .mclk = i2s_cfg.mclk,
            .bclk = i2s_cfg.bclk,
            .ws = i2s_cfg.ws,
            .dout = i2s_cfg.dout,
            .din = i2s_cfg.din,
        },
    };
    tdm_cfg.slot_cfg.total_slot = 4;
#endif
    int ret = i2s_new_channel(&chan_cfg, output == false ? NULL : &i2s_keep[port]->tx_handle,
                              input == false ? NULL : &i2s_keep[port]->rx_handle);
    ESP_LOGI(TAG, "tx:%p rx:%p", i2s_keep[port]->tx_handle, i2s_keep[port]->rx_handle);
    if (i2s_keep[port]->tx_handle) {
        if (init_cfg->out_mode == CODEC_I2S_MODE_STD) {
            ret = i2s_channel_init_std_mode(i2s_keep[port]->tx_handle, &std_cfg);
            ESP_LOGI(TAG, "output init std ret %d", ret);
        }
#ifdef SOC_I2S_SUPPORTS_TDM
        else if (init_cfg->out_mode == CODEC_I2S_MODE_TDM) {
            ret = i2s_channel_init_tdm_mode(i2s_keep[port]->tx_handle, &tdm_cfg);
            ESP_LOGI(TAG, "output init tdm ret %d", ret);
        }
#endif
    }
    if (i2s_keep[port]->rx_handle) {
        if (init_cfg->in_mode == CODEC_I2S_MODE_STD) {
            ret = i2s_channel_init_std_mode(i2s_keep[port]->rx_handle, &std_cfg);
            ESP_LOGI(TAG, "Input init std ret %d", ret);
        }
#ifdef SOC_I2S_SUPPORTS_TDM
        else if (init_cfg->in_mode == CODEC_I2S_MODE_TDM) {
            ret = i2s_channel_init_tdm_mode(i2s_keep[port]->rx_handle, &tdm_cfg);
            ESP_LOGI(TAG, "Input init tdm ret %d", ret);
        }
#endif
    }
    // Enable I2S here for maybe some codec need I2S clock to set register correctly
    if (i2s_keep[port]->tx_handle) {
        i2s_channel_enable(i2s_keep[port]->tx_handle);
    }
    if (i2s_keep[port]->rx_handle) {
        i2s_channel_enable(i2s_keep[port]->rx_handle);
    }
#else
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL2 | ESP_INTR_FLAG_IRAM,
        .dma_buf_count = 3,
        .dma_buf_len = 300,
        .use_apll = true,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
        //.mclk_multiple = 256,
    };
    int ret = i2s_driver_install(port, &i2s_config, 0, NULL);
    i2s_pin_config_t i2s_pin_cfg = {
        .mck_io_num = i2s_cfg.mclk,
        .bck_io_num = i2s_cfg.bclk,
        .ws_io_num = i2s_cfg.ws,
        .data_out_num = i2s_cfg.dout,
        .data_in_num = i2s_cfg.din,
    };
    i2s_set_pin(port, &i2s_pin_cfg);
#endif
    ESP_LOGI(TAG, "Init i2s %d ok", port);
    i2s_keep[port]->inited = true;
    return ret;
}

static int _i2s_deinit(uint8_t port)
{
    if (port >= MAX_I2S_NUM) {
        return -1;
    }
    if (i2s_keep[port] == NULL) {
        return 0;
    }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    if (i2s_keep[port]->tx_handle) {
        i2s_channel_disable(i2s_keep[port]->tx_handle);
    }
    if (i2s_keep[port]->rx_handle) {
        i2s_channel_disable(i2s_keep[port]->rx_handle);
    }
    if (i2s_keep[port]->tx_handle) {
        i2s_del_channel(i2s_keep[port]->tx_handle);
    }
    if (i2s_keep[port]->rx_handle) {
        i2s_del_channel(i2s_keep[port]->rx_handle);
    }
#else
    i2s_driver_uninstall(port);
#endif
    free(i2s_keep[port]);
    i2s_keep[port] = NULL;
    return 0;
}

int init_codec(codec_init_cfg_t *cfg)
{
    if (cfg == NULL) {
        return -1;
    }
    if (codec_res.inited) {
        ESP_LOGI(TAG, "Already initialized");
        return 0;
    }
    codec_cfg_t out_cfg = { 0 };
    codec_cfg_t in_cfg = { 0 };
    bool has_out = false;
    bool has_in = false;
    // Get codec configuration
    if (get_out_codec_cfg(&out_cfg) == 0) {
        has_out = true;
    }
    if (get_in_codec_cfg(&in_cfg) == 0) {
        has_in = true;
    }
    if (has_out == false && has_in == false) {
        ESP_LOGE(TAG, "No codec device found");
        return -1;
    }
    // Force to init I2C firstly
    _i2c_init(0);
    // Init i2c and i2s
    bool same_i2s = (has_in && has_out && out_cfg.i2s_port == in_cfg.i2s_port);
    ESP_LOGI(TAG, "in:%d out:%d port: %d", has_in, has_out, out_cfg.i2s_port == in_cfg.i2s_port);
    if (has_out) {
        if (out_cfg.i2c_port >= 0 && _i2c_init(out_cfg.i2c_port)) {
            ESP_LOGE(TAG, "Fail to int i2c: %d", out_cfg.i2c_port);
            return -1;
        }
        ESP_LOGI(TAG, "Success to int i2c: %d", in_cfg.i2c_port);
        if (_i2s_init(out_cfg.i2s_port, same_i2s ? ESP_CODEC_DEV_TYPE_IN_OUT : ESP_CODEC_DEV_TYPE_OUT, cfg)) {
            ESP_LOGE(TAG, "Fail to init i2s: %d", out_cfg.i2s_port);
            return -1;
        }
        ESP_LOGI(TAG, "Success to init i2s: %d", in_cfg.i2s_port);
    }
    if (has_in) {
        if (in_cfg.i2c_port >= 0 && _i2c_init(in_cfg.i2c_port)) {
            ESP_LOGE(TAG, "Fail to int i2c: %d", in_cfg.i2c_port);
            return -1;
        }
        ESP_LOGI(TAG, "Success to int i2c: %d", in_cfg.i2c_port);
        if (_i2s_init(in_cfg.i2s_port, same_i2s ? ESP_CODEC_DEV_TYPE_IN_OUT : ESP_CODEC_DEV_TYPE_IN, cfg)) {
            ESP_LOGE(TAG, "Fail to init i2s: %d", in_cfg.i2s_port);
            return -1;
        }
        ESP_LOGI(TAG, "Success to init i2s: %d", in_cfg.i2s_port);
    }
    // Create gpio interface
    codec_res.gpio_if = audio_codec_new_gpio();

    bool same_codec = same_i2s && (in_cfg.codec_type == out_cfg.codec_type);
    if (has_out) {
        audio_codec_i2s_cfg_t i2s_out_cfg = {
            .port = out_cfg.i2s_port,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
            .tx_handle = i2s_keep[out_cfg.i2s_port]->tx_handle,
            .rx_handle = i2s_keep[out_cfg.i2s_port]->rx_handle,
#endif
        };
        ESP_LOGI(TAG, "Get out handle %p port %d", i2s_out_cfg.tx_handle, out_cfg.i2s_port);
        codec_res.data_if = audio_codec_new_i2s_data(&i2s_out_cfg);

        audio_codec_i2c_cfg_t i2c_cfg = {
            .port = out_cfg.i2c_port,
#ifdef USE_I2C_MASTER
            .bus_handle = i2c_bus_handle[out_cfg.i2c_port],
#endif
        };
        // TODO add other codec support
        switch (out_cfg.codec_type) {
            case CODEC_TYPE_ES8311: {
                i2c_cfg.addr = out_cfg.i2c_addr ? out_cfg.i2c_addr : ES8311_CODEC_DEFAULT_ADDR;
                codec_res.out_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
                es8311_codec_cfg_t es8311_cfg = {
                    .codec_mode = same_codec ? ESP_CODEC_DEV_WORK_MODE_BOTH : ESP_CODEC_DEV_WORK_MODE_DAC,
                    .ctrl_if = codec_res.out_ctrl_if,
                    .gpio_if = codec_res.gpio_if,
                    .pa_pin = out_cfg.pa_pin,
                    .use_mclk = out_cfg.use_mclk,
                    .hw_gain.pa_gain = out_cfg.pa_gain,
                };
                codec_res.out_codec_if = es8311_codec_new(&es8311_cfg);
            } break;
            case CODEC_TYPE_ES8388: {
                i2c_cfg.addr = out_cfg.i2c_addr ? out_cfg.i2c_addr : ES8388_CODEC_DEFAULT_ADDR;
                codec_res.out_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
                es8388_codec_cfg_t es8388_cfg = {
                    .codec_mode = same_codec ? ESP_CODEC_DEV_WORK_MODE_BOTH : ESP_CODEC_DEV_WORK_MODE_DAC,
                    .ctrl_if = codec_res.out_ctrl_if,
                    .gpio_if = codec_res.gpio_if,
                    .pa_pin = out_cfg.pa_pin,
                    .hw_gain.pa_gain = out_cfg.pa_gain,
                };
                es8388_cfg.adc_input_sel = out_cfg.adc_input_sel;
                codec_res.out_codec_if = es8388_codec_new(&es8388_cfg);
            } break;
            case CODEC_TYPE_DUMMY: {
                dummy_codec_cfg_t dummy_cfg = {
                    .gpio_if = codec_res.gpio_if,
                    .enable_gpio = out_cfg.pa_pin,
                };
                codec_res.out_codec_if = dummy_codec_new(&dummy_cfg);
            } break;
            default:
                ESP_LOGE(TAG, "TODO not supported output codec type %d", out_cfg.codec_type);
                break;
        }
        esp_codec_dev_cfg_t dev_cfg = {
            .codec_if = codec_res.out_codec_if,
            .data_if = codec_res.data_if,
            .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        };
        if (same_codec && cfg->reuse_dev) {
            dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN_OUT;
        }
        codec_res.play_dev = esp_codec_dev_new(&dev_cfg);
        if (same_codec) {
            if (cfg->reuse_dev) {
                codec_res.record_dev = codec_res.play_dev;
            } else {
                // separate record from playback so that can set different fs
                dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
                codec_res.record_dev = esp_codec_dev_new(&dev_cfg);
            }
        }
    }
    if (same_codec == false && has_in) {
        if (same_i2s == false) {
            audio_codec_i2s_cfg_t i2s_in_cfg = {
                .port = in_cfg.i2s_port,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
                .rx_handle = i2s_keep[in_cfg.i2s_port]->rx_handle,
#endif
            };
            ESP_LOGI(TAG, "Get in handle %p port %d", i2s_in_cfg.rx_handle, i2s_in_cfg.port);
            codec_res.data_in_if = audio_codec_new_i2s_data(&i2s_in_cfg);
        }
        audio_codec_i2c_cfg_t i2c_cfg = {
            .port = in_cfg.i2c_port,
#ifdef USE_I2C_MASTER
            .bus_handle = i2c_bus_handle[in_cfg.i2c_port],
#endif
        };
        // TODO add other codec support
        switch (in_cfg.codec_type) {
            case CODEC_TYPE_ES7210: {
                i2c_cfg.addr = in_cfg.i2c_addr ? in_cfg.i2c_addr : ES7210_CODEC_DEFAULT_ADDR;
                codec_res.in_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
                es7210_codec_cfg_t es7210_cfg = {
                    .ctrl_if = codec_res.in_ctrl_if,
                    .mic_selected = ES7120_SEL_MIC1 | ES7120_SEL_MIC3,
                };
                if (cfg->in_use_tdm || (cfg->in_mode == CODEC_I2S_MODE_TDM)) {
                    es7210_cfg.mic_selected |= ES7120_SEL_MIC2 | ES7120_SEL_MIC4;
                }
                codec_res.in_codec_if = es7210_codec_new(&es7210_cfg);
            } break;

            case CODEC_TYPE_ES7243: {
                i2c_cfg.addr = in_cfg.i2c_addr ? in_cfg.i2c_addr : ES7243_CODEC_DEFAULT_ADDR;
                codec_res.in_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
                es7243_codec_cfg_t es7243_cfg = {
                    .ctrl_if = codec_res.in_ctrl_if,
                };
                codec_res.in_codec_if = es7243_codec_new(&es7243_cfg);
            } break;

            case CODEC_TYPE_DUMMY: {
                dummy_codec_cfg_t dummy_cfg = {
                    .gpio_if = codec_res.gpio_if,
                    .enable_gpio = out_cfg.pa_pin,
                };
                codec_res.in_codec_if = dummy_codec_new(&dummy_cfg);
            }
            default:
                break;
        }
        esp_codec_dev_cfg_t dev_cfg = {
            .codec_if = codec_res.in_codec_if,
            .data_if = same_i2s ? codec_res.data_if : codec_res.data_in_if,
            .dev_type = ESP_CODEC_DEV_TYPE_IN,
        };
        codec_res.record_dev = esp_codec_dev_new(&dev_cfg);
    }
    int ret = esp_codec_dev_set_out_vol(codec_res.play_dev, 60.0);
    ret = esp_codec_dev_set_in_gain(codec_res.record_dev, 30.0);
    if (ret == 0) {
        codec_res.inited = true;
    }
    return ret;
}

esp_codec_dev_handle_t get_playback_handle(void)
{
    return codec_res.play_dev;
}

esp_codec_dev_handle_t get_record_handle(void)
{
    return codec_res.record_dev;
}

void deinit_codec(void)
{
    if (codec_res.play_dev) {
        esp_codec_dev_delete(codec_res.play_dev);
        if (codec_res.record_dev == codec_res.play_dev) {
            codec_res.record_dev = NULL;
        }
        codec_res.play_dev = NULL;
    }
    if (codec_res.record_dev) {
        esp_codec_dev_delete(codec_res.record_dev);
        codec_res.record_dev = NULL;
    }
    // Delete codec interface
    if (codec_res.in_codec_if) {
        audio_codec_delete_codec_if(codec_res.in_codec_if);
        codec_res.in_codec_if = NULL;
    }
    if (codec_res.out_codec_if) {
        audio_codec_delete_codec_if(codec_res.out_codec_if);
        codec_res.out_codec_if = NULL;
    }
    // Delete codec control interface
    if (codec_res.in_ctrl_if) {
        audio_codec_delete_ctrl_if(codec_res.in_ctrl_if);
        codec_res.in_ctrl_if = NULL;
    }
    if (codec_res.out_ctrl_if) {
        audio_codec_delete_ctrl_if(codec_res.out_ctrl_if);
        codec_res.out_ctrl_if = NULL;
    }
    if (codec_res.data_if) {
        audio_codec_delete_data_if(codec_res.data_if);
        codec_res.data_if = NULL;
    }
    if (codec_res.data_in_if) {
        audio_codec_delete_data_if(codec_res.data_in_if);
        codec_res.data_in_if = NULL;
    }
    if (codec_res.gpio_if) {
        audio_codec_delete_gpio_if(codec_res.gpio_if);
        codec_res.gpio_if = NULL;
    }
    for (int i = 0; i < MAX_I2C_NUM; i++) {
        _i2c_deinit(i);
        _i2s_deinit(i);
    }
    codec_res.inited = false;
}

static sdmmc_card_t *card = NULL;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
#if CONFIG_IDF_TARGET_ESP32P4
#include "esp_ldo_regulator.h"
#endif
#endif

static void enable_mmc_phy_power(void)
{
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
#if CONFIG_IDF_TARGET_ESP32P4
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = 4,
        .voltage_mv = 3300,
    };
    esp_ldo_channel_handle_t ldo_phy_chan;
    esp_ldo_acquire_channel(&ldo_cfg, &ldo_phy_chan);
#endif
#endif
}

int mount_sdcard(void)
{
    sdcard_cfg_t cfg = { 0 };
    enable_mmc_phy_power();
    int ret = get_sdcard_config(&cfg);
    if (ret != 0) {
        return ret;
    }
#if SOC_SDMMC_HOST_SUPPORTED
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
#if CONFIG_IDF_TARGET_ESP32P4
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
#endif
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = cfg.d3 ? 4 : 1;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    slot_config.d4 = -1;
    slot_config.d5 = -1;
    slot_config.d6 = -1;
    slot_config.d7 = -1;
    slot_config.cd = -1;
    slot_config.wp = -1;
    slot_config.clk = cfg.clk;
    slot_config.cmd = cfg.cmd;
    slot_config.d0 = cfg.d0;
    slot_config.d1 = cfg.d1 ? cfg.d1 : -1;
    slot_config.d2 = cfg.d2 ? cfg.d2 : -1;
    slot_config.d3 = cfg.d3 ? cfg.d3 : -1;
    printf("use %d %d %d %d\n", cfg.d0, cfg.d1, cfg.d2, cfg.d3);
    return esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
#else
    return -1;
#endif
}

void unmount_sdcard()
{
    if (card) {
        esp_vfs_fat_sdcard_unmount("/sdcard", card);
        card = NULL;
    }
}
