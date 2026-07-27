idf_component_register(
    SRCS
        "main.cpp"
        "espidf_runtime.cpp"
        "ble_fsd_receiver.cpp"
    INCLUDE_DIRS
        "../include"
        ".."
    REQUIRES
        app_update
        bt
        esp_http_server
        esp_netif
        esp_wifi
        nvs_flash
        spi_flash
        spiffs
        driver
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
