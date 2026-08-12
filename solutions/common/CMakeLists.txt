
set(component_srcdirs "./")

set(component_requires esp_netif nvs_flash esp_wifi)

if("${IDF_TARGET}" STREQUAL "esp32p4")
    list(APPEND component_requires ethernet_init esp_eth)
endif()

idf_component_register(
    SRC_DIRS ./
    INCLUDE_DIRS ./
    REQUIRES ${component_requires}
)
