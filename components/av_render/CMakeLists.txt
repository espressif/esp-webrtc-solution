
list (APPEND COMPONENT_SRCDIRS ./src ./render_impl)

set(COMPONENT_ADD_INCLUDEDIRS ./include)

set(COMPONENT_PRIV_REQUIRES esp_timer esp_lcd driver)

register_component()
