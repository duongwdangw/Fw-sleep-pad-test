#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>
#include <esp_log.h>

#include "sleep_pad.h"
#include "process_manager.h"
#include "drivers.h"

void main_task(void *pvParameter){
    uart_sleep_pad_init();
    sp_set_mode(SLEEP_PAD_MONITOR_MODE);
    xTaskCreate(sp_read_uart_data_task, "sp_read_uart_data_task", 1024 * 10, NULL, 10, NULL);
    sp_change_to_idle_mode();
    sp_request_send_data_once_a_second();
    while(1){
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}