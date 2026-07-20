
set(component_srcdirs "./")

set(component_requires esp_netif nvs_flash esp_wifi)

idf_component_register(
    SRC_DIRS ./
    INCLUDE_DIRS ./
    REQUIRES ${component_requires}
)
