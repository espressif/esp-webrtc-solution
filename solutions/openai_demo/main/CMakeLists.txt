
set(KCONFIG_PROJBUILD "${CMAKE_CURRENT_SOURCE_DIR}/Kconfig.projbuild")

idf_component_register(SRCS "webrtc.c"  "main.c" "board.c"  "media_sys.c"
                      "openai_signaling.c"
                       INCLUDE_DIRS ".")
