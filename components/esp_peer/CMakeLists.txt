idf_build_get_property(idf_ver_major IDF_VERSION_MAJOR)

set(COMPONENT_SRCS src/esp_peer.c src/media_lib_weak.c)

if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER_EQUAL "6.0")
  list(APPEND COMPONENT_SRCS "src/dtls_srtp_v6.c")
else()
  list(APPEND COMPONENT_SRCS "src/dtls_srtp.c")
endif()

idf_component_register(INCLUDE_DIRS ./include
                       SRCS ${COMPONENT_SRCS}
                       PRIV_REQUIRES mbedtls)

if(idf_ver_major GREATER_EQUAL 6)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
                               MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS)
endif()

get_filename_component(BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR} NAME)
add_prebuilt_library(${BASE_DIR} "${CMAKE_CURRENT_SOURCE_DIR}/libs/${IDF_TARGET}/libpeer_default.a"
                     PRIV_REQUIRES ${BASE_DIR} esp_timer)
target_link_libraries(${COMPONENT_LIB} PRIVATE "-L ${CMAKE_CURRENT_SOURCE_DIR}/libs/${IDF_TARGET}")
target_link_libraries(${COMPONENT_LIB} PRIVATE ${BASE_DIR})
