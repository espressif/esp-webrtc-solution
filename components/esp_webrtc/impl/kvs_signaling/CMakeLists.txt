include(${CMAKE_CURRENT_LIST_DIR}/sigv4/sigv4FilePaths.cmake)

set(srcs "https_client.c" "kvs_signaling.c")

message("###################    SIGv4 Variables ############")
message(${SIGV4_SOURCES})
message(${SIGV4_INCLUDE_PUBLIC_DIRS})

idf_component_register(
    SRCS ${srcs} ${SIGV4_SOURCES}
    INCLUDE_DIRS "./" ${SIGV4_INCLUDE_PUBLIC_DIRS}

    REQUIRES json esp_http_client esp_websocket_client webrtc_utils
)
