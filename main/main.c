#include "hal/uart_types.h"
#include "soc/clk_tree_defs.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <string.h>
#include <stdio.h>
#include <driver/uart.h>

#include "config_parameter.h"
#include "driver.h"

#define BUF_SIZE 1024
#define UART_BAUD 115200


char buffer[100];

void app_main(void)
{
    uart_sleep_pad_init();
    while(1){
        int len = uart_read_bytes(UART_NUM_1, buffer, BUF_SIZE, 1000 / portTICK_PERIOD_MS);
        if(len > 0){
            buffer[len] = '\0';
            ESP_LOGI("APP", "Received: %d bytes", len);
            printf("Received: ");
            for(int i = 0; i < len; i++){
                printf("%02X ", buffer[i]);
            }
            printf("\n");
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
