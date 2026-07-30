// Author: Duong Hai Dang
// Student ID: DT060206

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <stdio.h>
#include "esp_sntp.h"
#include <time.h>
#include <sys/time.h>
#include "sleep_pad.h"
#include "process_manager.h"
#include "drivers.h"
#include "config_parameter.h"

#define TAG "APP_MAIN"
void sync_time_from_ntp(void);
void main_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Buoc 1: Ket noi WiFi...");
    wifi_init_sta();
    ESP_LOGI(TAG, "Buoc 1 xong.");

    ESP_LOGI(TAG, "Buoc 2: Ket noi WebSocket toi %s ...", WEBSOCKET_URI);
    websocket_app_start();
    ESP_LOGI(TAG, "Buoc 2 xong.");

    ESP_LOGI(TAG, "Buoc 3: Khoi tao UART cho sleep pad...");
    uart_sleep_pad_init();
    ESP_LOGI(TAG, "Buoc 3 xong: UART%d da san sang.", SLEEP_PAD_UART_NUM);

    // Ví dụ trong app_main():
    ESP_LOGI("APP_MAIN", "Buoc 1 xong.");

    // GỌI HÀM ĐỒNG BỘ THỜI GIAN Ở ĐÂY
    sync_time_from_ntp();

    ESP_LOGI("APP_MAIN", "Buoc 2: Ket noi WebSocket...");

    ESP_LOGI(TAG, "Buoc 4: Tao task doc UART va task xu ly/gui WS...");
    xTaskCreate(sp_read_uart_data_task, "read_uart", 1024 * 5, NULL, 5, NULL);
    xTaskCreate(sp_data_process_task, "process_data", 1024 * 6, NULL, 5, NULL);
    ESP_LOGI(TAG, "Buoc 4 xong.");

    ESP_LOGI(TAG, "Buoc 4.5: Cau hinh lai nguong ap luc va do nhay cam bien...");
    sp_set_sensor_sensitivity();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "Buoc 4.5 xong.");

    ESP_LOGI(TAG, "Buoc 5: Gui lenh [TAOSG] de bat dau nhan du lieu...");
    sp_request_send_data_once_a_second();
    ESP_LOGI(TAG, "Buoc 5 xong. Du lieu se duoc gui len WebSockets lien tuc.");

    while (1)
    {
        vTaskDelay(10000 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "main_task: van dang chay binh thuong");
    }
}

void sync_time_from_ntp(void)
{
    ESP_LOGI("NTP", "Dang dong bo thoi gian thuc tu Internet...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org"); // Server thời gian chuẩn quốc tế
    esp_sntp_init();

    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;

    // Đợi tối đa 15 lần (30 giây) để tải thời gian về
    while (timeinfo.tm_year < (2024 - 1900) && ++retry < 15)
    {
        vTaskDelay(pdMS_TO_TICKS(2000));
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    // Đặt múi giờ Việt Nam (UTC+7)
    setenv("TZ", "UTC-7", 1);
    tzset();

    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    ESP_LOGI("NTP", "Dong bo xong! Thoi gian hien tai: %s", strftime_buf);
}

void app_main(void)
{
    xTaskCreate(main_task, "main_task", 1024 * 6, NULL, 10, NULL);
}