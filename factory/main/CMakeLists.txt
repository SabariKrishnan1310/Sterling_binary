idf_component_register(
    SRCS "app_main.c" "softap.c" "network.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_event esp_netif esp_wifi
             esp_http_server esp_http_client esp_https_ota app_update
             esp_timer mbedtls driver json
)
