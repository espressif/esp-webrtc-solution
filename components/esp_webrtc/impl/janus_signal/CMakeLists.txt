
idf_component_register(
    SRCS "janus_signaling.c"
    INCLUDE_DIRS ./include
    REQUIRES esp_http_client esp_timer
)
