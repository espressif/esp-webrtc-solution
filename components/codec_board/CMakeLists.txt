
set(COMPONENT_PRIV_REQUIRE driver esp_codec_dev fatfs esp_lcd)

if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER_EQUAL "5.0")
list(APPEND COMPONENT_PRIV_REQUIRE esp_driver_i2s)
endif()

if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER_EQUAL "5.3")
list(APPEND COMPONENT_PRIV_REQUIRE esp_driver_i2c)
endif()

idf_component_register(SRCS  "cfg_parse.c" "codec_board.c" "codec_init.c" "dummy_codec.c"
                             "lcd_init.c" "drv/tca9554.c"
                       INCLUDE_DIRS ./include
                       PRIV_INCLUDE_DIRS drv
                       REQUIRES "${COMPONENT_PRIV_REQUIRE}"
                       EMBED_TXTFILES "board_cfg.txt")
