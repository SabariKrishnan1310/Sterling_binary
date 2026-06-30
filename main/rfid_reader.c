#include "rfid_reader.h"
#include "config.h"
#include "esp_log.h"
#include "rc522.h"
#include "rc522_picc.h"
#include "driver/rc522_spi.h"
#include <string.h>

static const char *TAG = "rfid";

static rc522_handle_t s_rc522 = NULL;
static rc522_picc_t s_current_picc;
static bool s_tag_present = false;

extern int32_t wifi_manager_get_rssi(void);

static void picc_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    rc522_picc_state_changed_event_t *evt = (rc522_picc_state_changed_event_t *)event_data;
    rc522_picc_t *picc = evt->picc;

    if (evt->old_state == RC522_PICC_STATE_IDLE &&
        (picc->state == RC522_PICC_STATE_READY || picc->state == RC522_PICC_STATE_ACTIVE)) {
        /* Tag entered field */
        memcpy(&s_current_picc, picc, sizeof(s_current_picc));
        s_tag_present = true;
        ESP_LOGI(TAG, "Tag detected, UID len=%d", picc->uid.length);
    } else if (picc->state == RC522_PICC_STATE_IDLE) {
        /* Tag left field */
        s_tag_present = false;
        memset(&s_current_picc, 0, sizeof(s_current_picc));
    }
}

esp_err_t rfid_reader_init(void)
{
    /* Configure SPI bus */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_RFID_MOSI,
        .miso_io_num = PIN_RFID_MISO,
        .sclk_io_num = PIN_RFID_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Configure SPI device interface */
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 5 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = PIN_RFID_CS,
        .queue_size = 1,
    };

    /* Create RC522 SPI driver */
    rc522_spi_config_t spi_drv_cfg = {
        .host_id = SPI2_HOST,
        .bus_config = &bus_cfg,
        .dev_config = dev_cfg,
        .rst_io_num = PIN_RFID_RST,
    };

    rc522_driver_handle_t driver = NULL;
    ret = rc522_spi_create(&spi_drv_cfg, &driver);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI driver create failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Create RC522 instance */
    rc522_config_t rc522_cfg = {
        .driver = driver,
        .poll_interval_ms = 50,
        .task_stack_size = 4096,
        .task_priority = 5,
    };

    ret = rc522_create(&rc522_cfg, &s_rc522);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rc522_create failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Register event handler */
    ret = rc522_register_events(s_rc522, RC522_EVENT_PICC_STATE_CHANGED,
                                 picc_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rc522_register_events failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Start polling */
    ret = rc522_start(s_rc522);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rc522_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "RFID reader initialized (event-driven)");
    return ESP_OK;
}

bool rfid_reader_read_tag(rfid_tag_t *tag)
{
    if (!tag || !s_tag_present) return false;

    /* Copy UID bytes */
    int copy_len = s_current_picc.uid.length;
    if (copy_len > (int)sizeof(tag->uid_bytes)) {
        copy_len = (int)sizeof(tag->uid_bytes);
    }
    memcpy(tag->uid_bytes, s_current_picc.uid.value, copy_len);
    tag->uid_len = copy_len;

    /* Convert to hex string */
    size_t pos = 0;
    for (int i = 0; i < copy_len && pos < sizeof(tag->hex_uid) - 2; i++) {
        pos += sprintf(tag->hex_uid + pos, "%02X", tag->uid_bytes[i]);
    }
    tag->hex_uid[pos] = '\0';

    /* Get RSSI from WiFi */
    tag->rssi = wifi_manager_get_rssi();

    /* Clear flag so same tag is only reported once */
    s_tag_present = false;

    return true;
}

void rfid_reader_set_power(uint8_t power)
{
    if (power > 0) {
        rc522_start(s_rc522);
        ESP_LOGI(TAG, "RFID reader power ON");
    } else {
        rc522_pause(s_rc522);
        ESP_LOGI(TAG, "RFID reader power OFF");
    }
}
