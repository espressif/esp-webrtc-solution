cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

if("${IDF_TARGET}" STREQUAL "esp32p4")
    set(EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/examples/ethernet/basic/components/ethernet_init")
endif()

list(APPEND EXTRA_COMPONENT_DIRS "../../components")
list(APPEND EXTRA_COMPONENT_DIRS "../common")

project(webrtc_usb_camera)
