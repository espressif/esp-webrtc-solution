cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

list(APPEND EXTRA_COMPONENT_DIRS "../common")

project(agora_demo)

# esp_asrc 1.0.1 has a uint32_t/%d diagnostic promoted to an error by IDF.
if(TARGET __idf_espressif__esp_asrc)
    target_compile_options(__idf_espressif__esp_asrc
                           PRIVATE -Wno-error=format)
endif()
