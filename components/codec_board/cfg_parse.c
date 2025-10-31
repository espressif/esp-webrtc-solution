#include "codec_board.h"

#define BOARD_SECTION           "Board:"

#define IN_STR(a, const_b, len) in_str(a, const_b, sizeof(const_b) - 1, len)

typedef struct _board_cfg_attr {
    const char             *attr;
    const char             *value;
    struct _board_cfg_attr *next;
} board_cfg_attr_t;

typedef struct {
    const char       *type;
    board_cfg_attr_t *attr;
} board_cfg_line_t;

static board_section_t *codec_section;

extern const char board_cfg_start[] asm("_binary_board_cfg_txt_start");
extern const char board_cfg_end[] asm("_binary_board_cfg_txt_end");

static bool is_word(char s)
{
    return ((s >= 'A' && s <= 'Z') || (s >= 'a' && s <= 'z') || (s >= '0' && s <= '9') || s == '_' || s == '-' ||
            s == '.');
}

bool str_same(const char *a, const char *b)
{
    while (*b && *a == *b) {
        a++;
        b++;
    }
    if (*b == 0) {
        if (is_word(*a) == false) {
            return true;
        }
    }
    return false;
}

static const char *in_str(const char *s, const char *org, int org_len, int len)
{
    while (len > org_len) {
        if (*s == *org && memcmp(s, org, org_len) == 0 && is_word(s[org_len]) == false) {
            return s;
        }
        s++;
        len--;
    }
    return NULL;
}

const char *get_section_data(const char *data, int size, const char *section_name)
{
    const char *s = data;
    while (1) {
        int left = size - (s - data);
        s = IN_STR(s, BOARD_SECTION, left);
        if (s == NULL) {
            break;
        }
        s += sizeof(BOARD_SECTION) - 1;
        while (!is_word(*s)) {
            s++;
        }
        if (str_same(s, section_name)) {
            while (*(s++) != '\n')
                ;
            return s;
        }
    }
    return NULL;
}

static int str_len(const char *s)
{
    int len = 0;
    while (is_word(*s)) {
        len++;
        s++;
    }
    return len;
}

static void print_cfg_line(board_cfg_line_t *cfg_line)
{
    printf("%.*s: {", str_len(cfg_line->type), cfg_line->type);
    board_cfg_attr_t *attr = cfg_line->attr;
    while (attr) {
        printf("%.*s: %.*s", str_len(attr->attr), attr->attr, str_len(attr->value), attr->value);
        if (attr->next) {
            printf(", ");
        }
        attr = attr->next;
    }
    printf("}\n");
}

static void free_cfg_line(board_cfg_line_t *cfg_line)
{
    if (cfg_line) {
        board_cfg_attr_t *attr = cfg_line->attr;
        while (attr) {
            board_cfg_attr_t *nxt = attr->next;
            free(attr);
            attr = nxt;
        }
        free(cfg_line);
    }
}

static board_cfg_line_t *parse_section(const char *s, int size, int *consume)
{
    board_cfg_line_t *cfg_line;
    const char *start = s;
    const char *end = s + size;
    const char *name = s;
    bool is_comment = false;
    while (size) {
        if (*name == '#') {
            is_comment = true;
        }
        if (is_comment) {
            if (*name == '\n') {
                is_comment = false;
            }
        } else {
            if (is_word(*name)) {
                break;
            }
        }
        size--;
        name++;
    }
    if (size == 0 || str_same(name, BOARD_SECTION)) {
        return NULL;
    }
    cfg_line = calloc(1, sizeof(board_cfg_line_t));
    if (cfg_line == NULL) {
        return NULL;
    }
    board_cfg_attr_t *tail = NULL;
    cfg_line->type = name;
    s = name;
    while (*(s++) != '{')
        ;
    int word_len = 0;
    const char *attr = NULL;

    while (s < end) {
        if (*s == '}') {
            *consume = (s + 1 - start);
            break;
        }
        if (is_word(*s)) {
            if (word_len == 0) {
                if (attr == NULL) {
                    attr = s;
                } else {
                    board_cfg_attr_t *cfg_attr = calloc(1, sizeof(board_cfg_attr_t));
                    if (cfg_attr) {
                        cfg_attr->attr = attr;
                        cfg_attr->value = s;
                        if (tail == NULL) {
                            cfg_line->attr = cfg_attr;
                        } else {
                            tail->next = cfg_attr;
                        }
                        tail = cfg_attr;
                    }
                    attr = NULL;
                }
            }
            word_len++;
        } else {
            word_len = 0;
        }
        s++;
    }
    return cfg_line;
}

static int fill_i2c_cfg(board_cfg_attr_t *attr)
{
    if (codec_section->i2c_num >= MAX_I2C_NUM) {
        return -1;
    }
    codec_i2c_pin_t *i2c_cfg = &codec_section->i2c_pin[codec_section->i2c_num++];
    while (attr) {
        if (str_same(attr->attr, "sda")) {
            i2c_cfg->sda = atoi(attr->value);
        } else if (str_same(attr->attr, "scl")) {
            i2c_cfg->scl = atoi(attr->value);
        }
        attr = attr->next;
    }
    return 0;
}

static int fill_i2s_cfg(board_cfg_attr_t *attr)
{
    if (codec_section->i2s_num >= MAX_I2C_NUM) {
        return -1;
    }
    codec_i2s_pin_t *i2s_cfg = &codec_section->i2s_pin[codec_section->i2s_num++];
    i2s_cfg->din = i2s_cfg->dout = -1;
    while (attr) {
        if (str_same(attr->attr, "bclk")) {
            i2s_cfg->bclk = atoi(attr->value);
        } else if (str_same(attr->attr, "din")) {
            i2s_cfg->din = atoi(attr->value);
        } else if (str_same(attr->attr, "dout")) {
            i2s_cfg->dout = atoi(attr->value);
        } else if (str_same(attr->attr, "ws")) {
            i2s_cfg->ws = atoi(attr->value);
        } else if (str_same(attr->attr, "mclk")) {
            i2s_cfg->mclk = atoi(attr->value);
        }
        attr = attr->next;
    }
    return 0;
}

static extend_io_type_t lcd_get_io_type(const char* s)
{
    if (str_same(s, "tca9554")) {
        return EXTENT_IO_TYPE_TCA9554;
    }
    return EXTENT_IO_TYPE_NONE;
}

static lcd_controller_type_t lcd_get_controller(const char* s)
{
    if (str_same(s, "st7789")) {
        return LCD_CONTROLLER_TYPE_ST7789;
    }
    return LCD_CONTROLLER_TYPE_NONE;
}

static lcd_bus_type_t lcd_get_bus(const char* s)
{
    if (str_same(s, "spi")) {
        return LCD_BUS_TYPE_SPI;
    }
    if (str_same(s, "rgb")) {
        return LCD_BUS_TYPE_RGB;
    }
    if (str_same(s, "i80")) {
        return LCD_BUS_TYPE_I80;
    }
    if (str_same(s, "mipi")) {
        return LCD_BUS_TYPE_MIPI;
    }
    return LCD_BUS_TYPE_NONE;
}

static int16_t get_pin(const char* s)
{
    bool has_ext = (strncmp(s, "ext", 3) == 0);
    if (has_ext) {
        return 0x1000 + atoi(s + 3);
    }
    return (int16_t)atoi(s);
}

static int fill_lcd_cfg(board_cfg_attr_t *attr)
{
    if (codec_section->lcd_num >= 1) {
        return -1;
    }
    lcd_cfg_t *lcd_cfg = &codec_section->lcd;
    lcd_cfg->reset_pin = lcd_cfg->ctrl_pin = -1;
    codec_section->lcd_num++;
    while (attr) {
        if (str_same(attr->attr, "bus")) {
            lcd_cfg->bus_type = lcd_get_bus(attr->value);
            if (lcd_cfg->bus_type == LCD_BUS_TYPE_SPI) {
                for (int i = 0; i < sizeof(lcd_cfg->spi_cfg.d)/sizeof(lcd_cfg->spi_cfg.d[0]); i++) {
                    lcd_cfg->spi_cfg.d[i] = -1;
                }
            }
        } else if (str_same(attr->attr, "controller")) {
            lcd_cfg->controller = lcd_get_controller(attr->value);
        }
        else if (str_same(attr->attr, "extend_io")) {
            lcd_cfg->io_type = lcd_get_io_type(attr->value);
        } else if (str_same(attr->attr, "width")) {
            lcd_cfg->width = atoi(attr->value);
        } else if (str_same(attr->attr, "height")) {
            lcd_cfg->height = atoi(attr->value);
        } else if (str_same(attr->attr, "mirror_x")) {
            lcd_cfg->mirror_x = atoi(attr->value) ? 1: 0;
        } else if (str_same(attr->attr, "mirror_y")) {
            lcd_cfg->mirror_y = atoi(attr->value) ? 1: 0;
        } else if (str_same(attr->attr, "swap_xy")) {
            lcd_cfg->swap_xy = atoi(attr->value) ? 1: 0;
        } else if (str_same(attr->attr, "color_inv")) {
            lcd_cfg->color_inv = atoi(attr->value) ? 1: 0;
        } else if (str_same(attr->attr, "ctrl")) {
            lcd_cfg->ctrl_pin = get_pin(attr->value);
        } else if (str_same(attr->attr, "rst")) {
            lcd_cfg->reset_pin = get_pin(attr->value);
        }
        else if (lcd_cfg->bus_type == LCD_BUS_TYPE_SPI) {
            // Bus configuration
            if (str_same(attr->attr, "spi_bus")) {
                lcd_cfg->spi_cfg.spi_bus = atoi(attr->value);
            } else if (str_same(attr->attr, "cs")) {
                lcd_cfg->spi_cfg.cs = get_pin(attr->value);
            } else if (str_same(attr->attr, "dc")) {
                lcd_cfg->spi_cfg.dc = get_pin(attr->value);
            } else if (str_same(attr->attr, "clk")) {
                lcd_cfg->spi_cfg.clk = get_pin(attr->value);
            } else if (str_same(attr->attr, "mosi")) {
                lcd_cfg->spi_cfg.mosi = get_pin(attr->value);
            } else if (str_same(attr->attr, "cmd_bits")) {
                lcd_cfg->spi_cfg.cmd_bits = (uint8_t)atoi(attr->value);
            } else if (str_same(attr->attr, "param_bits")) {
                lcd_cfg->spi_cfg.param_bits = (uint8_t)atoi(attr->value);
            }
        } else if (lcd_cfg->bus_type == LCD_BUS_TYPE_MIPI) {
            if (str_same(attr->attr, "ldo_chan")) {
                lcd_cfg->mipi_cfg.ldo_chan = (uint8_t)atoi(attr->value);
            }
            else if (str_same(attr->attr, "ldo_voltage")) {
                lcd_cfg->mipi_cfg.ldo_voltage = (uint16_t)atoi(attr->value);
            }
            else if (str_same(attr->attr, "lane_num")) {
                lcd_cfg->mipi_cfg.lane_num = (uint8_t)atoi(attr->value);
            }
            else if (str_same(attr->attr, "fb_num")) {
                lcd_cfg->mipi_cfg.fb_num = (uint8_t)atoi(attr->value);
            }
            else if (str_same(attr->attr, "bit_depth")) {
                lcd_cfg->mipi_cfg.bit_depth = (uint16_t)atoi(attr->value);
            }
            else if (str_same(attr->attr, "lane_bitrate")) {
                lcd_cfg->mipi_cfg.lane_bitrate = (uint32_t)atoi(attr->value);
            }
            else if (str_same(attr->attr, "dpi_clk")) {
                lcd_cfg->mipi_cfg.dpi_clk = (uint32_t)atoi(attr->value);
            }
            else if (str_same(attr->attr, "dsi_hsync")) {
                lcd_cfg->mipi_cfg.dsi_hsync = (uint8_t)atoi(attr->value);
            }
            else if (str_same(attr->attr, "dsi_vsync")) {
                lcd_cfg->mipi_cfg.dsi_vsync = (uint8_t)atoi(attr->value);
            }
            else if (str_same(attr->attr, "dsi_hbp")) {
                lcd_cfg->mipi_cfg.dsi_hbp = (uint8_t)atoi(attr->value);
            }
            else if (str_same(attr->attr, "dsi_hfp")) {
                lcd_cfg->mipi_cfg.dsi_hfp = (uint8_t)atoi(attr->value);
            }
            else if (str_same(attr->attr, "dsi_vbp")) {
                lcd_cfg->mipi_cfg.dsi_vbp = (uint8_t)atoi(attr->value);
            }
            else if (str_same(attr->attr, "dsi_vfp")) {
                lcd_cfg->mipi_cfg.dsi_vfp = (uint8_t)atoi(attr->value);
            }
        }
        attr = attr->next;
    }
    return 0;
}

// TODO add other codec
static codec_type_t get_codec_type(const char *type)
{
    if (str_same(type, "ES8311")) {
        return CODEC_TYPE_ES8311;
    }
    if (str_same(type, "ES8388")) {
        return CODEC_TYPE_ES8388;
    }
    if (str_same(type, "ES7243")) {
        return CODEC_TYPE_ES7243;
    }
    if (str_same(type, "ES7210")) {
        return CODEC_TYPE_ES7210;
    }
    if (str_same(type, "DUMMY")) {
        return CODEC_TYPE_DUMMY;
    }
    return CODEC_TYPE_NONE;
}

static int fill_codec_cfg(board_cfg_attr_t *attr, uint8_t codec_dir)
{
    if (codec_section->codec_num >= MAX_CODEC_NUM) {
        return -1;
    }
    codec_section->codec[codec_section->codec_num].codec_dir = codec_dir;
    codec_cfg_t *codec_cfg = &codec_section->codec[codec_section->codec_num].codec_cfg;
    while (attr) {
        if (str_same(attr->attr, "i2c_port")) {
            codec_cfg->i2c_port = atoi(attr->value);
            if (codec_cfg->i2c_port >= codec_section->i2c_num) {
                return -1;
            }
        } else if (str_same(attr->attr, "i2s_port")) {
            codec_cfg->i2s_port = atoi(attr->value);
            if (codec_cfg->i2s_port >= codec_section->i2s_num) {
                return -1;
            }
        } else if (str_same(attr->attr, "pa")) {
            codec_cfg->pa_pin = atoi(attr->value);
        } else if (str_same(attr->attr, "pa_gain")) {
            codec_cfg->pa_gain = atof(attr->value);
        } else if (str_same(attr->attr, "use_mclk")) {
            codec_cfg->use_mclk = (bool) atoi(attr->value);
        } else if (str_same(attr->attr, "codec")) {
            codec_cfg->codec_type = get_codec_type(attr->value);
            if (codec_cfg->codec_type == CODEC_TYPE_NONE) {
                return -1;
            }
        } else if (str_same(attr->attr, "i2c_addr")) {
            codec_cfg->i2c_addr = (uint8_t) atoi(attr->value);
        }
        attr = attr->next;
    }
    codec_section->codec_num++;
    return 0;
}

static int fill_sdcard_cfg(board_cfg_attr_t *attr)
{
    if (codec_section->sdcard_num >= 1) {
        return -1;
    }
    sdcard_cfg_t *sdcard_cfg = &codec_section->sdcard;
    sdcard_cfg->power = -1;
    while (attr) {
        if (str_same(attr->attr, "clk")) {
            sdcard_cfg->clk = (int16_t) atoi(attr->value);
        } else if (str_same(attr->attr, "cmd")) {
            sdcard_cfg->cmd = (int16_t) atoi(attr->value);
        } else if (str_same(attr->attr, "d0")) {
            sdcard_cfg->d0 = (int16_t) atoi(attr->value);
        } else if (str_same(attr->attr, "d1")) {
            sdcard_cfg->d1 = (int16_t) atoi(attr->value);
        } else if (str_same(attr->attr, "d2")) {
            sdcard_cfg->d2 = (int16_t) atoi(attr->value);
        } else if (str_same(attr->attr, "d3")) {
            sdcard_cfg->d3 = (int16_t) atoi(attr->value);
        } else if (str_same(attr->attr, "power")) {
            sdcard_cfg->power = (int16_t) atoi(attr->value);
        }
        attr = attr->next;
    }
    codec_section->sdcard_num++;
    return 0;
}

static camera_type_t get_camera_type(const char* s) {
    if (str_same(s, "dvp")) {
        return CAMERA_TYPE_DVP;
    }
    if (str_same(s, "usb")) {
        return CAMERA_TYPE_USB;
    }
    if (str_same(s, "mipi")) {
        return CAMERA_TYPE_MIPI;
    }
    return CAMERA_TYPE_NONE;
}

static int fill_camera_cfg(board_cfg_attr_t *attr)
{
    if (codec_section->camera_num >= 1) {
        return -1;
    }
    camera_cfg_t *camera_cfg = &codec_section->camera;
    for (int i = 0; i < sizeof(camera_cfg->data)/sizeof(camera_cfg->data[0]); i++) {
        camera_cfg->data[i] = -1;
    }
    camera_cfg->pwr = -1;
    camera_cfg->reset = -1;
    camera_cfg->xclk = -1;
    while (attr) {
        if (str_same(attr->attr, "type")) {
            camera_cfg->type = get_camera_type(attr->value);
        }
        if (str_same(attr->attr, "xclk")) {
            camera_cfg->xclk = (int16_t) atoi(attr->value);
        } else if (str_same(attr->attr, "pclk")) {
            camera_cfg->pclk = (int16_t) atoi(attr->value);
        } else if (str_same(attr->attr, "pwr")) {
            camera_cfg->pwr = (int16_t) atoi(attr->value);
        } else if (str_same(attr->attr, "reset")) {
            camera_cfg->reset = (int16_t) atoi(attr->value);
        } else if (str_same(attr->attr, "vsync")) {
            camera_cfg->vsync = (int16_t) atoi(attr->value);
        } else if (str_same(attr->attr, "href")) {
            camera_cfg->href = (int16_t) atoi(attr->value);
        }  else if (str_same(attr->attr, "de")) {
            camera_cfg->de = (int16_t) atoi(attr->value);
        } else if (attr->attr[0] == 'd' && attr->attr[1] >= '0' && attr->attr[1] <= '9') {
            int n = atoi(attr->attr + 1);
            if (n < sizeof(camera_cfg->data)/sizeof(camera_cfg->data[0])) {
                camera_cfg->data[n] = (int16_t) atoi(attr->value);
            }
        }
        attr = attr->next;
    }
    codec_section->camera_num++;
    return 0;
}

static int fill_cfg(board_cfg_line_t *cfg_line)
{
    board_cfg_attr_t *attr = cfg_line->attr;
    if (str_same(cfg_line->type, "i2c")) {
        if (fill_i2c_cfg(attr) != 0) {
            return -1;
        }
    } else if (str_same(cfg_line->type, "i2s")) {
        if (fill_i2s_cfg(attr) != 0) {
            return -1;
        }
    } else if (str_same(cfg_line->type, "sdcard")) {
        if (fill_sdcard_cfg(attr) != 0) {
            return -1;
        }
    } else if (str_same(cfg_line->type, "in_out")) {
        if (fill_codec_cfg(attr, CODEC_DIR_IN_OUT) != 0) {
            return -1;
        }
    } else if (str_same(cfg_line->type, "out")) {
        if (fill_codec_cfg(attr, CODEC_DIR_OUT) != 0) {
            return -1;
        }
    } else if (str_same(cfg_line->type, "in")) {
        if (fill_codec_cfg(attr, CODEC_DIR_IN) != 0) {
            return -1;
        }
    }  else if (str_same(cfg_line->type, "lcd")) {
        if (fill_lcd_cfg(attr) != 0) {
            return -1;
        }
    } else if (str_same(cfg_line->type, "camera")) {
        if (fill_camera_cfg(attr) != 0) {
            return -1;
        }
    }
    return 0;
}

static int parse_cfg(const char *section, int size)
{
    int consume = 0;
    while (1) {
        board_cfg_line_t *cfg_line = parse_section(section, size, &consume);
        if (cfg_line == NULL) {
            break;
        }
        size -= consume;
        section += consume;
        print_cfg_line(cfg_line);
        int ret = fill_cfg(cfg_line);
        free_cfg_line(cfg_line);
        if (ret != 0) {
            printf("Fail to parse cfg line\n");
            return ret;
        }
    }
    return 0;
}

board_section_t *parse_codec_section(const char *section)
{
    if (codec_section) {
        free(codec_section);
    }
    codec_section = calloc(1, sizeof(board_section_t));
    if (codec_section == NULL) {
        return NULL;
    }
    int size = strlen(section);
    if (parse_cfg(section, size) != 0) {
        free(codec_section);
        return NULL;
    }
    return codec_section;
}

board_section_t *get_codec_section(const char *codec_type)
{
    if (codec_type == NULL) {
        return NULL;
    }
    if (codec_section) {
        free(codec_section);
    }
    codec_section = calloc(1, sizeof(board_section_t));
    if (codec_section == NULL) {
        return NULL;
    }

    int cfg_size = board_cfg_end - board_cfg_start;
    do {
        const char *section = get_section_data(board_cfg_start, cfg_size, codec_type);
        if (section == NULL) {
            break;
        }
        int left_size = cfg_size - (section - board_cfg_start);
        if (parse_cfg(section, left_size) != 0) {
            break;
        }
        return codec_section;
    } while (0);
    free(codec_section);
    return NULL;
}
