
list (APPEND COMPONENT_SRCDIRS ./src ./render_impl)

set(COMPONENT_ADD_INCLUDEDIRS ./include)

set(COMPONENT_PRIV_REQUIRES esp_timer esp_lcd driver)

register_component()

# Add color convert ASM optimized lib
IF (${IDF_TARGET} STREQUAL "esp32p4")
set(TARGET_LIB_NAME "${CMAKE_CURRENT_SOURCE_DIR}/libs/${IDF_TARGET}/libesp_image_i420_2_rgb565le.a")
target_link_libraries(${COMPONENT_TARGET} "-Wl,--start-group" ${TARGET_LIB_NAME} "-Wl,--end-group")
ENDIF ()
