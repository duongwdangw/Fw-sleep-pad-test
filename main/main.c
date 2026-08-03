#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include "sleep_pad.h"
#include "process_manager.h"
#include "ble.h"
#include "connect_wifi.h"
#include "nvs_config.h"
#include "drivers.h"
#include "mqtt_wifi.h"

#define TAG "APP_MAIN"

void main_task(void *pvParameter) {
    ESP_LOGI(TAG, "Buoc 1: Khoi tao NVS...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Buoc 2: Kiem tra NVS...");
    // Load config truc tiep vao bien toan cuc s_config cua BLE
    bool has_creds = nvs_config_load(&s_config);

    wifi_init();

    if (has_creds) {
        ESP_LOGI(TAG, "Da co thong tin, tien hanh ket noi WiFi: %s", s_config.wifi_ssid);
        wifi_connect_sta(s_config.wifi_ssid, s_config.wifi_password);
    } else {
        ESP_LOGI(TAG, "Chua co WiFi. Bat BLE chờ điện thoại Provisioning...");
        ble_init();
    }

    ESP_LOGI(TAG, "Buoc 3: Khoi tao UART cho sleep pad...");
    uart_sleep_pad_init();

    ESP_LOGI(TAG, "Buoc 4: Tao task doc UART va dong goi JSON...");
    xTaskCreate(sp_read_uart_data_task, "read_uart", 1024 * 5, NULL, 5, NULL);
    xTaskCreate(sp_data_process_task, "process_data", 1024 * 6, NULL, 5, NULL);

    ESP_LOGI(TAG, "Buoc 5: Ổn định phần cứng (Warm-up)...");
    vTaskDelay(pdMS_TO_TICKS(3000)); // Đợi dòng điện ổn định, tránh nhảy pdata ảo
    sp_set_sensor_sensitivity();
    vTaskDelay(pdMS_TO_TICKS(1000)); 

    ESP_LOGI(TAG, "Buoc 6: Gui lenh [TAOSG] bat dau doc data...");
    sp_request_send_data_once_a_second();

    while(1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "main_task: van dang chay. MQTT Connected: %d", mqtt_connected);
    }
}

void app_main(void) {
    xTaskCreate(main_task, "main_task", 1024 * 6, NULL, 10, NULL);
}