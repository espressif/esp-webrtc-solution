# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

# include($ENV{ADF_PATH}/CMakeLists.txt)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

if("${IDF_TARGET}" STREQUAL "esp32p4")
    set(EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/examples/ethernet/basic/components/ethernet_init")
endif()

list(APPEND EXTRA_COMPONENT_DIRS "../common")

project(doorbell_demo)
