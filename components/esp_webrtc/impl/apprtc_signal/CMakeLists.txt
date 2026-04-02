
file(GLOB CODES "*.c")

idf_component_register(
    SRCS ${CODES}
    INCLUDE_DIRS ./
    REQUIRES json esp_http_client esp_websocket_client
)
