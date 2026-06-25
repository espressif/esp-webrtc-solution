set(COMPONENT_SRCS main.c rtmp.c board.c media_sys.c)
set(COMPONENT_ADD_INCLUDEDIRS .)

register_component()

if(DEFINED ENV{RTMP_PUSH_URL})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RTMP_PUSH_URL=\"$ENV{RTMP_PUSH_URL}\")
endif()
