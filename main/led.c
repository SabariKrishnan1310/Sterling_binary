#include "led.h"
#include "config.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "led";

static QueueHandle_t led_queue = NULL;

#define WAVE_STEPS 40

static const uint8_t wave_pwm[WAVE_STEPS] = {
    0,  1,  3,  7, 12, 18, 25, 32, 39, 45,
   49, 52, 54, 54, 53, 50, 46, 40, 34, 27,
   20, 14,  8,  4,  2,  1,  0,  1,  3,  7,
   12, 18, 25, 32, 39, 45, 49, 52, 54, 54,
};

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
    led_on();  delay_ms(500);
    led_off(); delay_ms(300);
    led_on();  delay_ms(60);
    led_off(); delay_ms(40);
    led_on();  delay_ms(60);
    led_off();
}

static void play_pattern_tag(void)
{
    led_on();  delay_ms(200);
    led_off();
}

static void play_pattern_failure(void)
{
    led_on();  delay_ms(100);
    led_off(); delay_ms(100);
    led_on();  delay_ms(100);
    led_off();
}

static void play_wave_step(int step)
{
    int b = wave_pwm[step % WAVE_STEPS];
    int on_ms = (b * 48) / 54;
    if (on_ms > 0) {
        led_on();  delay_ms(on_ms);
        led_off(); delay_ms(48 - on_ms);
    } else {
        led_off(); delay_ms(48);
    }
}

void led_task(void *pvParameters)
{
    esp_err_t err = led_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LED init failed, deleting task");
        vTaskDelete(NULL);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(500));
    led_send(LED_PATTERN_BOOT);

    led_message_t msg;
    int wave_step = 0;
    bool wave_active = false;

    while (1) {
        TickType_t timeout = wave_active ? pdMS_TO_TICKS(50) : portMAX_DELAY;
        BaseType_t received = xQueueReceive(led_queue, &msg, timeout);

        if (received == pdTRUE) {
            switch (msg.pattern) {
                case LED_PATTERN_BOOT:
                    wave_active = false;
                    play_pattern_boot();
                    break;
                case LED_PATTERN_TAG:
                    play_pattern_tag();
                    break;
                case LED_PATTERN_FAILURE:
                    play_pattern_failure();
                    break;
                case LED_PATTERN_WAVE:
                    wave_active = true;
                    wave_step = 0;
                    break;
                case LED_PATTERN_IDLE:
                    wave_active = false;
                    led_off();
                    break;
            }
        } else if (wave_active) {
            play_wave_step(wave_step);
            wave_step = (wave_step + 1) % WAVE_STEPS;
        } else {
            led_off();
        }
    }
}
