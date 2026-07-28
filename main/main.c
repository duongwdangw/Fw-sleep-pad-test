// Author: Duong Hai Dang
// Student ID: DT060206

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <stdio.h>

#include "sleep_pad.h"
#include "process_manager.h"
#include "drivers.h"
#include "config_parameter.h"

#define TAG "APP_MAIN"

void main_task(void *pvParameter){
    ESP_LOGI(TAG, "Buoc 1: Ket noi WiFi...");
    wifi_init_sta();
    ESP_LOGI(TAG, "Buoc 1 xong.");

    ESP_LOGI(TAG, "Buoc 2: Ket noi MQTT toi %s ...", MQTT_BROKER_URI);
    mqtt_app_start();
    ESP_LOGI(TAG, "Buoc 2 xong (dang ket noi nen, se bao trang thai qua log MQTT).");

    ESP_LOGI(TAG, "Buoc 3: Khoi tao UART cho sleep pad...");
    uart_sleep_pad_init();
    ESP_LOGI(TAG, "Buoc 3 xong: UART%d da san sang.", SLEEP_PAD_UART_NUM);

    ESP_LOGI(TAG, "Buoc 4: Tao task doc UART va task xu ly/publish MQTT...");
    xTaskCreate(sp_read_uart_data_task, "read_uart", 1024 * 5, NULL, 5, NULL);
    xTaskCreate(sp_data_process_task, "process_data", 1024 * 6, NULL, 5, NULL);
    ESP_LOGI(TAG, "Buoc 4 xong.");

    // Theo dung tai lieu "UART Hardware Spec V106" muc 4.4: lenh [TAOSG] dua
    // thiet bi vao trang thai lam viec va yeu cau tra ve du lieu moi giay
    // (66 byte, bao gom ca song ap luc tho de tinh sdata/pdata).
    ESP_LOGI(TAG, "Buoc 5: Gui lenh [TAOSG] de bat dau nhan du lieu moi giay...");
    sp_request_send_data_once_a_second();
    ESP_LOGI(TAG, "Buoc 5 xong. Tu day ve sau du lieu se duoc publish len MQTT moi giay/phut.");
    ESP_LOGI(TAG, "Luu y: nhip tim/nhip tho CHI co gia tri sau 25 giay nam yen lien tuc - bang 0 truoc do la binh thuong.");

    while(1){
        vTaskDelay(10000 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "main_task: van dang chay binh thuong");
    }
}

void app_main(void){
    xTaskCreate(main_task, "main_task", 1024 * 6, NULL, 10, NULL);
}