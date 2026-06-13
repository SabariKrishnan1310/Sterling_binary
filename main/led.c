#include "led.h"
#include "config.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "led";

static QueueHandle_t led_queue = NULL;

esp_err_t led_init(void)
{
    gpio_reset_pin(STATUS_LED);
    gpio_set_direction(STATUS_LED, GPIO_MODE_OUTPUT);
    gpio_set_level(STATUS_LED, 0);

    led_queue = xQueueCreate(LED_QUEUE_LENGTH, sizeof(led_message_t));
    if (!led_queue) {
        ESP_LOGE(TAG, "Failed to create LED queue");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LED initialized on GPIO %d", STATUS_LED);
    return ESP_OK;
}

esp_err_t led_send(led_pattern_t pattern)
{
    if (!led_queue) return ESP_FAIL;

    led_message_t msg = { .pattern = pattern };
    BaseType_t ret = xQueueSend(led_queue, &msg, 0);
    if (ret != pdPASS) {
        ESP_LOGW(TAG, "LED queue full, dropping pattern %d", pattern);
        return ESP_FAIL;
    }
    return ESP_OK;
}

QueueHandle_t led_get_queue(void)
{
    return led_queue;
}

static inline void led_on(void)
{
    gpio_set_level(STATUS_LED, 1);
}

static inline void led_off(void)
{
    gpio_set_level(STATUS_LED, 0);
}

static void delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static void play_pattern_boot(void)
{
    led_on();  delay_ms(200);
    led_off(); delay_ms(100);
    led_on();  delay_ms(200);
    led_off(); delay_ms(100);
    led_on();  delay_ms(600);
    led_off();
}

static void play_pattern_success(void)
{
    led_on();  delay_ms(300);
    led_off();
}

static void play_pattern_failure(void)
{
    led_on();  delay_ms(100);
    led_off(); delay_ms(100);
    led_on();  delay_ms(100);
    led_off();
}

static void play_pattern_offline(void)
{
    led_on();  delay_ms(LED_OFFLINE_PULSE_MS);
    led_off();
}

void led_task(void *pvParameters)
{
    esp_err_t err = led_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LED init failed, deleting task");
        vTaskDelete(NULL);
        return;
    }

    led_send(LED_PATTERN_BOOT);

    led_message_t msg;
    TickType_t idle_wake = pdMS_TO_TICKS(LED_OFFLINE_PERIOD_MS);

    while (1) {
        BaseType_t received = xQueueReceive(led_queue, &msg, idle_wake);

        if (received == pdTRUE) {
            switch (msg.pattern) {
                case LED_PATTERN_BOOT:
                    play_pattern_boot();
                    break;
                case LED_PATTERN_SUCCESS:
                    play_pattern_success();
                    break;
                case LED_PATTERN_FAILURE:
                    play_pattern_failure();
                    break;
                case LED_PATTERN_OFFLINE:
                    play_pattern_offline();
                    idle_wake = pdMS_TO_TICKS(LED_OFFLINE_PERIOD_MS);
                    break;
                case LED_PATTERN_IDLE:
                    led_off();
                    idle_wake = pdMS_TO_TICKS(LED_OFFLINE_PERIOD_MS);
                    break;
            }
        } else {
            play_pattern_offline();
        }
    }
}
