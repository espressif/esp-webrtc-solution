#include "codec_board.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "tca9554.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "esp_idf_version.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_st7789.h"
#else
#include "esp_lcd_panel_vendor.h"
#endif

#if CONFIG_IDF_TARGET_ESP32P4
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_ili9881c.h"
#include "esp_ldo_regulator.h"
#include "soc/mipi_dsi_bridge_struct.h"
#include "esp_lcd_ek79007.h"
#endif

#include "freertos/FreeRTOS.h"

#define TAG "LCD_INIT"

#define RETURN_ON_ERR(ret) if (ret != 0) {                     \
    ESP_LOGE(TAG, "Fail to run on %d ret %d", __LINE__, ret);  \
    return ret;                                                \
}

typedef struct {
    int (*init)(lcd_cfg_t *cfg);
    int (*set_dir)(int16_t gpio, bool output);
    int (*set_gpio)(int16_t gpio, bool high);
} extend_io_ops_t;

static extend_io_ops_t        extend_io_ops;
static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t g_io_handle = NULL;

static int tca9554_io_init(lcd_cfg_t *cfg)
{
    return tca9554_init(cfg->io_i2c_port);
}

static int tca9554_io_set_dir(int16_t gpio, bool output)
{
    gpio = (1 << gpio);
    tca9554_set_io_config(gpio, output ? TCA9554_IO_OUTPUT : TCA9554_IO_INPUT);
    return 0;
}

static int tca9554_io_set(int16_t gpio, bool high)
{
    gpio = (1 << gpio);
    return tca9554_set_output_state(gpio, high ? TCA9554_IO_HIGH : TCA9554_IO_LOW);
}

static void register_tca9554(void)
{
    extend_io_ops.init = tca9554_io_init;
    extend_io_ops.set_dir = tca9554_io_set_dir;
    extend_io_ops.set_gpio = tca9554_io_set;
}

static int init_extend_io(lcd_cfg_t *cfg)
{
    if (cfg->io_type == EXTENT_IO_TYPE_NONE) {
        return 0;
    }
    switch (cfg->io_type) {
        case EXTENT_IO_TYPE_TCA9554:
            register_tca9554();
            break;
        default:
            return -1;
    }
    return extend_io_ops.init(cfg);
}

static int set_pin_dir(int16_t pin, bool output)
{
    if (pin & BOARD_EXTEND_IO_START) {
        pin &= ~BOARD_EXTEND_IO_START;
        extend_io_ops.set_dir(pin, output);
    } else {
        gpio_config_t bk_gpio_config = {
            .mode = output ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT,
            .pin_bit_mask = pin > 0 ? 1ULL << pin : 0ULL,
        };
        gpio_config(&bk_gpio_config);
    }
    return 0;
}

static int set_pin_state(int16_t pin, bool high)
{
    if (pin & BOARD_EXTEND_IO_START) {
        extend_io_ops.set_gpio(pin, high);
    } else {
        gpio_set_level(pin, true);
    }
    return 0;
}

static int16_t get_hw_gpio(int16_t pin)
{
    if (pin == -1) {
        return pin;
    }
    if (pin & BOARD_EXTEND_IO_START) {
        return -1;
    }
    return pin;
}

static void sleep_ms(int ms)
{
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

static int _lcd_rest(lcd_cfg_t *cfg)
{
    if (cfg->reset_pin >= 0) {
        set_pin_state(cfg->reset_pin, false);
        sleep_ms(100);
        set_pin_state(cfg->reset_pin, true);
    }
    return 0;
}

static int _init_spi_lcd(lcd_cfg_t *cfg)
{
    int ret = 0;
    if (cfg->spi_cfg.cs & BOARD_EXTEND_IO_START) {
        set_pin_dir(cfg->spi_cfg.cs, true);
        sleep_ms(10);
        set_pin_dir(cfg->spi_cfg.cs, false);
        sleep_ms(10);
    }
    spi_bus_config_t buscfg = {
        .sclk_io_num = cfg->spi_cfg.clk,
        .mosi_io_num = cfg->spi_cfg.mosi,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = cfg->width * cfg->height * 2,
    };
#if SOC_SPI_SUPPORT_OCT
    if (cfg->spi_cfg.d[6] >= 0) {
        buscfg.data1_io_num = cfg->spi_cfg.d[0];
        buscfg.data2_io_num = cfg->spi_cfg.d[1];
        ;
        buscfg.data3_io_num = cfg->spi_cfg.d[2];
        ;
        buscfg.data4_io_num = cfg->spi_cfg.d[3];
        ;
        buscfg.data5_io_num = cfg->spi_cfg.d[4];
        ;
        buscfg.data6_io_num = cfg->spi_cfg.d[5];
        ;
        buscfg.data7_io_num = cfg->spi_cfg.d[6];
        ;
        buscfg.flags = SPICOMMON_BUSFLAG_OCTAL;
    }
#endif
    int bus_id = SPI1_HOST + (cfg->spi_cfg.spi_bus - 1);
    ret = spi_bus_initialize(bus_id, &buscfg, SPI_DMA_CH_AUTO);
    ESP_LOGI(TAG, "CLK %d MOSI %d CS:%d DC: %d Bus:%d",
             cfg->spi_cfg.clk, cfg->spi_cfg.mosi,
             get_hw_gpio(cfg->spi_cfg.cs), cfg->spi_cfg.dc,
             bus_id);
    RETURN_ON_ERR(ret);
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = cfg->spi_cfg.dc,
        .cs_gpio_num = get_hw_gpio(cfg->spi_cfg.cs),
        .pclk_hz = cfg->spi_cfg.pclk_clk ? cfg->spi_cfg.pclk_clk : 60 * 1000 * 1000,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = cfg->spi_cfg.cmd_bits ? cfg->spi_cfg.cmd_bits : 8,
        .lcd_param_bits = cfg->spi_cfg.param_bits ? cfg->spi_cfg.param_bits : 8,
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
    };
#if SOC_SPI_SUPPORT_OCT
    if (cfg->spi_cfg.d[6] >= 0) {
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0)
        io_config.flags.octal_mode = 1;
#endif
        io_config.spi_mode = 3;
    }
#endif
    esp_lcd_panel_io_handle_t io_handle;
    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)bus_id, &io_config, &io_handle);
    RETURN_ON_ERR(ret);
    g_io_handle = io_handle;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = get_hw_gpio(cfg->reset_pin),
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
#elif ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
        .rgb_ele_order = ESP_LCD_COLOR_SPACE_BGR,
#else
        .rgb_endian = LCD_RGB_ENDIAN_BGR,
#endif
        .bits_per_pixel = 16,
    };
    switch (cfg->controller) {
        default:
            return -1;
        case LCD_CONTROLLER_TYPE_ST7789:
            ret = esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle);
            RETURN_ON_ERR(ret);
            ESP_LOGI(TAG, "Init driver ST7789 finished");
            break;
    }
    return ret;
}

#if CONFIG_IDF_TARGET_ESP32P4
static int power_on_dsi(lcd_mipi_cfg_t *mipi_cfg)
{
    esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
    esp_ldo_channel_config_t ldo_mipi_phy_config = {
        .chan_id = mipi_cfg->ldo_chan,
        .voltage_mv = mipi_cfg->ldo_voltage,
    };
    return esp_ldo_acquire_channel(&ldo_mipi_phy_config, &ldo_mipi_phy);
}

static int _init_mipi_lcd(lcd_cfg_t *cfg)
{
    int ret = 0;
    lcd_mipi_cfg_t *mipi_cfg = &cfg->mipi_cfg;
    power_on_dsi(mipi_cfg);
    // create MIPI DSI bus first, it will initialize the DSI PHY as well
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = mipi_cfg->lane_num,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = mipi_cfg->lane_bitrate,
    };
    ret = esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus);
    RETURN_ON_ERR(ret);
    ESP_LOGI(TAG, "Install MIPI DSI LCD control panel");
    esp_lcd_panel_io_handle_t mipi_dbi_io;
    // we use DBI interface to send LCD commands and parameters
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ret = esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io);
    RETURN_ON_ERR(ret);
    g_io_handle = mipi_dbi_io;
    esp_lcd_dpi_panel_config_t dpi_config = {
        .num_fbs = mipi_cfg->fb_num,
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = mipi_cfg->dpi_clk,
        .pixel_format = mipi_cfg->bit_depth == 24 ? LCD_COLOR_PIXEL_FORMAT_RGB888 : LCD_COLOR_PIXEL_FORMAT_RGB565,
        .video_timing = {
            .h_size = cfg->width,
            .v_size = cfg->height,
            .hsync_back_porch = mipi_cfg->dsi_hbp,
            .hsync_pulse_width = mipi_cfg->dsi_hsync,
            .hsync_front_porch = mipi_cfg->dsi_hfp,
            .vsync_back_porch = mipi_cfg->dsi_vbp,
            .vsync_pulse_width = mipi_cfg->dsi_vsync,
            .vsync_front_porch = mipi_cfg->dsi_vfp,
        },
        .flags.use_dma2d = true,
    };
    esp_lcd_panel_dev_config_t panel_config = {
        .bits_per_pixel = mipi_cfg->bit_depth,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .reset_gpio_num = cfg->reset_pin,
    };
    if (cfg->width == 1024 && cfg->height == 600) {
        ESP_LOGI(TAG, "Install EK79007 LCD control panel");
        esp_lcd_dpi_panel_config_t dpi_config = EK79007_1024_600_PANEL_60HZ_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
        dpi_config.num_fbs = mipi_cfg->fb_num;
        ek79007_vendor_config_t vendor_config = {
            .mipi_config = {
                .dsi_bus = mipi_dsi_bus,
                .dpi_config = &dpi_config,
            },
        };
        panel_config.vendor_config = &vendor_config;
        ret = esp_lcd_new_panel_ek79007(mipi_dbi_io, &panel_config, &panel_handle);
        RETURN_ON_ERR(ret);
    } else {
        ili9881c_vendor_config_t vendor_config = {
            .mipi_config = {
                .dsi_bus = mipi_dsi_bus,
                .dpi_config = &dpi_config,
                .lane_num = mipi_cfg->lane_num,
            },
        };
        panel_config.vendor_config = &vendor_config;
        ret = esp_lcd_new_panel_ili9881c(mipi_dbi_io, &panel_config, &panel_handle);
    }
    RETURN_ON_ERR(ret);
    esp_lcd_panel_reset(panel_handle);
    ESP_LOGI(TAG, "Install MIPI DSI LCD data panel");
    return ret;
}
#else

static int _init_mipi_lcd(lcd_cfg_t *cfg)
{
    return -1;
}

#endif

static int _init_lcd(lcd_cfg_t *cfg)
{
    int ret = 0;
    if (cfg->io_type != EXTENT_IO_TYPE_NONE) {
        ret = init_extend_io(cfg);
        if (ret != 0) {
            return ret;
        }
    }
    // Config reset and ctrl gpio dir
    if (cfg->reset_pin >= 0) {
        set_pin_dir(cfg->reset_pin, true);
    }
    if (cfg->ctrl_pin >= 0) {
        set_pin_dir(cfg->ctrl_pin, true);
    }
    if (cfg->bus_type == LCD_BUS_TYPE_SPI) {
        if (cfg->spi_cfg.cs >= 0) {
            set_pin_dir(cfg->spi_cfg.cs, true);
        }
    }
    _lcd_rest(cfg);
    if (cfg->ctrl_pin >= 0) {
        set_pin_dir(cfg->ctrl_pin, true);
    }
    if (cfg->bus_type == LCD_BUS_TYPE_SPI) {
        ret = _init_spi_lcd(cfg);
    } else if (cfg->bus_type == LCD_BUS_TYPE_MIPI) {
        ret = _init_mipi_lcd(cfg);
    }
    if (panel_handle) {
        ret = esp_lcd_panel_init(panel_handle);
        RETURN_ON_ERR(ret);
        if (cfg->color_inv) {
            ret = esp_lcd_panel_invert_color(panel_handle, cfg->color_inv);
        }
        // ret = esp_lcd_panel_set_gap(panel_handle, 0, 0);
        if (cfg->swap_xy) {
            ret = esp_lcd_panel_swap_xy(panel_handle, cfg->swap_xy);
        }
        if (cfg->mirror_x || cfg->mirror_y) {
            ret = esp_lcd_panel_mirror(panel_handle, cfg->mirror_x, cfg->mirror_y);
        }
        ret = esp_lcd_panel_disp_on_off(panel_handle, true);
    }
    return ret;
}

int board_lcd_init(void)
{
    lcd_cfg_t cfg = { 0 };
    int ret = get_lcd_cfg(&cfg);
    if (ret != 0) {
        return ret;
    }
    return _init_lcd(&cfg);
}

void *board_get_lcd_handle(void)
{
    if (panel_handle) {
        return panel_handle;
    }
    return NULL;
}

void *board_get_lcd_io_handle(void)
{
    return g_io_handle;
}

