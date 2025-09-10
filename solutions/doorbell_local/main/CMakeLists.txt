
set(srcs "webrtc.c"  "main.c" "board.c" "media_sys.c" "webrtc_http_server.c")

if(CONFIG_DOORBELL_SUPPORT_PEDESTRIAN_DETECT)
    list(APPEND srcs "pedestrain_detect.cpp")
endif()

idf_component_register(SRCS ${srcs}
                       EMBED_TXTFILES "ring.aac" "open.aac" "join.aac" "webrtc_test.html"
                       "certs/servercert.pem" "certs/prvtkey.pem"
                       INCLUDE_DIRS ".")
