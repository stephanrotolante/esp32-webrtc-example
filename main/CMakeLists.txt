idf_component_register(
    SRCS "main.c" "wifi.c"
    INCLUDE_DIRS "/app/main/"
    REQUIRES esp_wifi esp_psram peer esp_http_client nvs_flash esp-tls)


idf_component_get_property(lib peer COMPONENT_LIB)
target_compile_options(${lib} PRIVATE -Wno-error=restrict)
target_compile_options(${lib} PRIVATE -Wno-error=stringop-truncation)

idf_component_get_property(lib srtp COMPONENT_LIB)
target_compile_options(${lib} PRIVATE -Wno-error=incompatible-pointer-types)

