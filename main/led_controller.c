#include "led_controller.h"
#include "config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "led";

#define PIN_STATUS_LED GPIO_NUM_13

static QueueHandle_t s_queue = NULL;
static bool wave_active = false;
static int wave_step_val = 0;

static const uint8_t wave_pwm[40] = {
    0,1,3,7,12,18,25,32,39,45,49,52,54,54,53,50,46,40,34,27,
    20,14,8,4,2,1,0,1,3,7,12,18,25,32,39,45,49,52,54,54
};

static inline void led_on(void) { gpio_set_level(PIN_STATUS_LED, 0); }
static inline void led_off(void) { gpio_set_level(PIN_STATUS_LED, 1); }

static void blink_ms(int on_ms, int off_ms)
{
    led_on();
    vTaskDelay(pdMS_TO_TICKS(on_ms));
    led_off();
    vTaskDelay(pdMS_TO_TICKS(off_ms));
}

static void wave_step(int step)
{
    int b = wave_pwm[step % 40];
    int on_us = (b * 2000) / 54;
    if (on_us > 0) {
        led_on();
        esp_rom_delay_us(on_us);
        led_off();
        esp_rom_delay_us(2000 - on_us);
    } else {
        led_off();
        esp_rom_delay_us(2000);
    }
}

esp_err_t led_controller_init(void)
{
    gpio_reset_pin(PIN_STATUS_LED);
    gpio_set_direction(PIN_STATUS_LED, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_STATUS_LED, 1);
    s_queue = xQueueCreate(8, sizeof(led_pattern_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "Failed to create queue");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t led_controller_play(led_pattern_t pattern)
{
    if (s_queue) {
        xQueueSend(s_queue, &pattern, 0);
    }
    return ESP_OK;
}

void led_controller_task(void *pvParameters)
{
    if (led_controller_init() != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(LED_BOOT_DELAY_MS));
    led_controller_play(LED_PATTERN_BOOT);

    led_pattern_t pattern;
    while (1) {
        if (xQueueReceive(s_queue, &pattern, pdMS_TO_TICKS(50)) == pdTRUE) {
            wave_active = false;
            switch (pattern) {
                case LED_PATTERN_BOOT:
                    blink_ms(500, 300);
                    blink_ms(60, 40);
                    blink_ms(60, 0);
                    led_off();
                    break;
                case LED_PATTERN_TAG:
                    led_on();
                    vTaskDelay(pdMS_TO_TICKS(LED_TAG_MS));
                    led_off();
                    break;
                case LED_PATTERN_FAILURE:
                    for (int i = 0; i < 3; i++) {
                        blink_ms(LED_FAILURE_BLINK_MS, LED_FAILURE_BLINK_MS);
                    }
                    break;
                case LED_PATTERN_WAVE:
                    wave_active = true;
                    wave_step_val = 0;
                    break;
                case LED_PATTERN_PROVISIONING:
                    while (1) {
                        led_on();
                        vTaskDelay(pdMS_TO_TICKS(LED_PROVISIONING_BLINK_MS));
                        led_off();
                        vTaskDelay(pdMS_TO_TICKS(LED_PROVISIONING_BLINK_MS));
                        if (uxQueueMessagesWaiting(s_queue) > 0) break;
                    }
                    break;
                case LED_PATTERN_WG_CONNECTING:
                    while (1) {
                        blink_ms(200, 200);
                        if (uxQueueMessagesWaiting(s_queue) > 0) break;
                    }
                    break;
                case LED_PATTERN_MQTT_CONNECTED:
                    led_on();
                    break;
                case LED_PATTERN_IDLE:
                    led_off();
                    break;
                case LED_PATTERN_INDICATOR:
                    led_on();
                    vTaskDelay(pdMS_TO_TICKS(BUZZER_INDICATOR_MS));
                    led_off();
                    break;
                case LED_PATTERN_OFF:
                    led_off();
                    break;
                default:
                    break;
            }
        }

        if (wave_active) {
            wave_step(wave_step_val++);
        }
    }
}
