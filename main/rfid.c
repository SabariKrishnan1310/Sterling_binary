#include "rfid.h"
#include "storage.h"
#include "network.h"
#include "event_log.h"
#include "led.h"
#include "config.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_task_wdt.h"
#include "rc522.h"
#include "driver/rc522_spi.h"
#include <string.h>

static const char *TAG = "rfid";

static rc522_handle_t rc522 = NULL;
static QueueHandle_t tap_queue = NULL;

#define TAP_MSG_MAX_UID     64

typedef struct {
    char uid[TAP_MSG_MAX_UID];
    uint8_t uid_len;
} tap_message_t;

static void hex_encode(const uint8_t *src, size_t len, char *dst)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        dst[i * 2]     = hex[src[i] >> 4];
        dst[i * 2 + 1] = hex[src[i] & 0xF];
    }
    dst[len * 2] = '\0';
}

static void on_picc_state_changed(void *arg, esp_event_base_t base,
                                   int32_t event_id, void *data)
{
    rc522_picc_state_changed_event_t *evt = (rc522_picc_state_changed_event_t *)data;
    rc522_picc_t *picc = evt->picc;

    if (picc->state != RC522_PICC_STATE_ACTIVE && picc->state != RC522_PICC_STATE_ACTIVE_H) {
        return;
    }

    tap_message_t msg;
    msg.uid_len = picc->uid.length > 10 ? 10 : picc->uid.length;
    hex_encode(picc->uid.value, msg.uid_len, msg.uid);

    BaseType_t ret = xQueueSend(tap_queue, &msg, 0);
    if (ret != pdPASS) {
        ESP_LOGW(TAG, "Tap queue full, dropping card");
    }
}

esp_err_t rfid_init(void)
{
    tap_queue = xQueueCreate(TAP_QUEUE_LENGTH, sizeof(tap_message_t));
    if (!tap_queue) {
        ESP_LOGE(TAG, "Failed to create tap queue");
        return ESP_FAIL;
    }

    spi_bus_config_t bus_cfg = {
        .miso_io_num = RFID_MISO,
        .mosi_io_num = RFID_MOSI,
        .sclk_io_num = RFID_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    rc522_spi_config_t spi_cfg = {
        .host_id = SPI2_HOST,
        .bus_config = &bus_cfg,
        .dev_config = {
            .clock_speed_hz = 5000000,
            .spics_io_num = RFID_CS,
            .queue_size = 1,
        },
        .dma_chan = SPI_DMA_DISABLED,
        .rst_io_num = RFID_RST,
    };

    rc522_driver_handle_t spi_driver;
    esp_err_t err = rc522_spi_create(&spi_cfg, &spi_driver);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rc522_spi_create failed: %s", esp_err_to_name(err));
        return err;
    }

    err = rc522_driver_install(spi_driver);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rc522_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    rc522_config_t rc522_cfg = {
        .driver = spi_driver,
        .poll_interval_ms = 200,
        .task_stack_size = 4096,
        .task_priority = 5,
    };

    err = rc522_create(&rc522_cfg, &rc522);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rc522_create failed: %s", esp_err_to_name(err));
        return err;
    }

    err = rc522_register_events(rc522, RC522_EVENT_PICC_STATE_CHANGED,
                                on_picc_state_changed, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rc522_register_events failed: %s", esp_err_to_name(err));
        rc522_destroy(rc522);
        rc522 = NULL;
        return err;
    }

    err = rc522_start(rc522);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rc522_start failed: %s", esp_err_to_name(err));
        rc522_destroy(rc522);
        rc522 = NULL;
        return err;
    }

    ESP_LOGI(TAG, "RFID initialized (SPI2, MISO=%d MOSI=%d SCK=%d CS=%d RST=%d)",
             RFID_MISO, RFID_MOSI, RFID_SCK, RFID_CS, RFID_RST);
    return ESP_OK;
}

void rfid_task(void *pvParameters)
{
    ESP_LOGI(TAG, "[DBG] rfid_task: starting init...");

    esp_err_t err = rfid_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[DBG] rfid_task: init failed=%s, retry in 10s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "[DBG] rfid_task: retry init...");
        err = rfid_init();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "[DBG] rfid_task: retry failed, idling");
            while (1) {
                esp_task_wdt_reset();
                vTaskDelay(pdMS_TO_TICKS(60000));
            }
        }
    }

    ESP_LOGI(TAG, "[DBG] rfid_task: init OK on core %d", xPortGetCoreID());

    tap_message_t msg;

    while (1) {
        esp_task_wdt_reset();
        BaseType_t received = xQueueReceive(tap_queue, &msg, pdMS_TO_TICKS(1000));

        if (received == pdTRUE) {
            ESP_LOGI(TAG, "[DBG] rfid_task: CARD DETECTED uid=%s len=%d", msg.uid, msg.uid_len);

            EventBits_t bits = xEventGroupGetBits(wifi_event_group);
            if (bits & WIFI_CONNECTED_BIT) {
                ESP_LOGI(TAG, "[DBG] rfid_task: WiFi up, trying immediate upload");
                err = network_send_tap_single(msg.uid);
                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "[DBG] rfid_task: immediate UPLOAD SUCCESS uid=%s", msg.uid);
                    event_log_write(EVT_RFID_READ);
                    led_send(LED_PATTERN_TAG);                    continue;
                }
                ESP_LOGW(TAG, "[DBG] rfid_task: immediate upload FAILED, saving to local storage");
            } else {
                ESP_LOGI(TAG, "[DBG] rfid_task: WiFi DOWN, storing locally");
            }

            uint32_t seq;
            err = storage_append_tap(msg.uid, &seq);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "[DBG] rfid_task: stored locally seq=%lu uid=%s", (unsigned long)seq, msg.uid);
                event_log_write(EVT_RFID_READ);
                led_send(LED_PATTERN_TAG);
            } else {
                ESP_LOGE(TAG, "[DBG] rfid_task: storage failed=%s", esp_err_to_name(err));
                led_send(LED_PATTERN_FAILURE);
            }
        }
    }
}
