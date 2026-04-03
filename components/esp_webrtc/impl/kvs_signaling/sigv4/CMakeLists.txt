# Project information.
cmake_minimum_required( VERSION 3.10.0 )
project( "sigv4" LANGUAGES C )

# Allow the project to be organized into folders.
set_property( GLOBAL PROPERTY USE_FOLDERS ON )

set( CMAKE_C_STANDARD_REQUIRED ON )

# Do not allow in-source build.
if( ${PROJECT_SOURCE_DIR} STREQUAL ${PROJECT_BINARY_DIR} )
    message( FATAL_ERROR "In-source build is not allowed. Please build in a separate directory, such as ${PROJECT_SOURCE_DIR}/build." )
endif()

# Set global path variables.
get_filename_component( __MODULE_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE )
set( MODULE_ROOT_DIR ${__MODULE_ROOT_DIR} CACHE INTERNAL "SigV4 repository root." )


# Set output directories.
set( CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin )
set( CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib )
set( CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib )


# Include filepaths for source and include.
include( ${MODULE_ROOT_DIR}/sigv4FilePaths.cmake )

add_library(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE ${SIGV4_SOURCES})
target_include_directories(${PROJECT_NAME} PUBLIC
   $<BUILD_INTERFACE:${SIGV4_INCLUDE_PUBLIC_DIRS}>
   $<INSTALL_INTERFACE:include/${PROJECT_NAME}>
)

# `sigv4_config` target defines the path to sigv4_config.h and optionally other based config files
if( NOT TARGET sigv4_config )
	target_compile_definitions(${PROJECT_NAME} PRIVATE -DSIGV4_DO_NOT_USE_CUSTOM_CONFIG )
endif()

include( GNUInstallDirs )

install( TARGETS ${PROJECT_NAME}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME}
  PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME}
)

install( DIRECTORY ${SIGV4_INCLUDE_PUBLIC_DIRS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME} )
