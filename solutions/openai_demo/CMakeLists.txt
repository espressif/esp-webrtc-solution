# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

# include($ENV{ADF_PATH}/CMakeLists.txt)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

list(APPEND EXTRA_COMPONENT_DIRS "../common")

if(NOT DEFINED ENV{OPENAI_API_KEY})
  message(FATAL_ERROR "Env variable OPENAI_API_KEY must be set")
endif()

add_compile_definitions(OPENAI_API_KEY="$ENV{OPENAI_API_KEY}")

project(openai_demo)

# esp_asrc 1.0.1 uses a signed format specifier for allocation sizes. Keep the
# workaround scoped to that managed component until the registry release fixes it.
idf_component_get_property(esp_asrc_lib espressif__esp_asrc COMPONENT_LIB)
target_compile_options(${esp_asrc_lib} PRIVATE -Wno-error=format)
