
idf_component_register(
    REQUIRES soc nvs_flash ulp driver esp_http_server app_update esp_event esp_wifi esp32-wifi-manager blackmagic frogfs
    SRC_DIRS "."
    INCLUDE_DIRS "."
                 "include"
                 "../components/blackmagic"
)
component_compile_options(-Wno-error=format= -Wno-format)
