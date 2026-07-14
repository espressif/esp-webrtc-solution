# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

# include($ENV{ADF_PATH}/CMakeLists.txt)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

list(APPEND EXTRA_COMPONENT_DIRS "../../components")

list(APPEND EXTRA_COMPONENT_DIRS "../common")

if(NOT DEFINED ENV{OPENAI_API_KEY})
  message(FATAL_ERROR "Env variable OPENAI_API_KEY must be set")
endif()

add_compile_definitions(OPENAI_API_KEY="$ENV{OPENAI_API_KEY}")

project(openai_demo)
