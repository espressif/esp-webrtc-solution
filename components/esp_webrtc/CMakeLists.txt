
set(component_srcdirs "src")

# AppRTC, Whip, Janus signalling
set(signalling_srcdirs  "impl/apprtc_signal" "impl/whip_signal" "impl/janus_signal")
set(signalling_incdirs  "impl/apprtc_signal" "impl/whip_signal/include" "impl/janus_signal/include")

list(APPEND component_srcdirs ${signalling_srcdirs})

idf_component_register(
    SRC_DIRS ${component_srcdirs}
    INCLUDE_DIRS ./include ${signalling_incdirs}
    REQUIRES json esp_http_client esp_websocket_client esp_netif webrtc_utils
)
