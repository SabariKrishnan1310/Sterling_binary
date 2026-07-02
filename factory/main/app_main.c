#include "config.h"
#include "network.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

static const char *TAG = "FACTORY_APP";

void app_main(void)
{
    // Initialize GPIO for LED
    gpio_config_t io_conf = {0};
    io_conf.pin_bit_mask = (1ULL << STATUS_LED);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);
    
    // LED blink 3 times = "I'm alive"
    for (int i = 0; i < 3; i++) {
        gpio_set_level(STATUS_LED, 1);
        vTaskDelay(200 / portTICK_PERIOD_MS);
        gpio_set_level(STATUS_LED, 0);
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    
    // Initialize network
    wifi_init();
    
    // Wait for WiFi
    for (int retry = 0; retry < 60; retry++) {
        if (wifi_is_connected()) break;
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    
    if (!wifi_is_connected()) {
        // SOS forever — no WiFi
        while (1) {
            gpio_set_level(STATUS_LED, 1); vTaskDelay(100/portTICK_PERIOD_MS);
            gpio_set_level(STATUS_LED, 0); vTaskDelay(100/portTICK_PERIOD_MS);
            gpio_set_level(STATUS_LED, 1); vTaskDelay(100/portTICK_PERIOD_MS);
            gpio_set_level(STATUS_LED, 0); vTaskDelay(100/portTICK_PERIOD_MS);
            gpio_set_level(STATUS_LED, 1); vTaskDelay(100/portTICK_PERIOD_MS);
            gpio_set_level(STATUS_LED, 0); vTaskDelay(700/portTICK_PERIOD_MS);
        }
    }
    
    // Fetch config + time
    wifi_fetch_config();  // sets time, stores networks
    
    // Force OTA — this is the WHOLE POINT of factory firmware
    while (1) {
        esp_err_t e = ota_force_update();
        if (e == ESP_OK) {
            // Reboot into ota_0
            esp_restart();
        }
        // Failed — SOS blink, retry in 60s
        for (int i = 0; i < 10; i++) {
            gpio_set_level(STATUS_LED, 1); vTaskDelay(100/portTICK_PERIOD_MS);
            gpio_set_level(STATUS_LED, 0); vTaskDelay(100/portTICK_PERIOD_MS);
            gpio_set_level(STATUS_LED, 1); vTaskDelay(100/portTICK_PERIOD_MS);
            gpio_set_level(STATUS_LED, 0); vTaskDelay(100/portTICK_PERIOD_MS);
            gpio_set_level(STATUS_LED, 1); vTaskDelay(100/portTICK_PERIOD_MS);
            gpio_set_level(STATUS_LED, 0); vTaskDelay(700/portTICK_PERIOD_MS);
        }
        vTaskDelay(60000 / portTICK_PERIOD_MS);  // wait 60s before retry
    }
}