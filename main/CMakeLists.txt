idf_component_register(
    SRCS
        "app_main.c"

    REQUIRES
        nvs_flash
        esp_event
        esp_netif
        esp_wifi
        esp_http_client
        esp_https_ota
        app_update
        bootloader_support
        spi_flash
        mbedtls
        lwip
        esp_timer
)
