#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <esp_log.h>

#include <drivers.h>
#include <esp_log.h>
#include <stdio.h>
#include <ble.h>
#include "connect_wifi.h"

#include "config_parameter.h"

#define TAG "DRIVERS"

void uart_sleep_pad_init(void){
    uart_config_t uart_config = {
        .baud_rate = SLEEP_PAD_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(SLEEP_PAD_UART_NUM, SLEEP_PAD_BUFFER_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(SLEEP_PAD_UART_NUM, &uart_config);
    uart_set_pin(SLEEP_PAD_UART_NUM, SLEEP_PAD_TX_PIN, SLEEP_PAD_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    
}

void stop_ble_timer_callback(){
    ESP_LOGI("CONFIG_GPIO", "Stopping BLE timer");
    ble_stop();
}


static esp_timer_handle_t stop_ble_timer_handler;

void start_stop_ble_timer(){
    if(stop_ble_timer_handler != NULL){
        esp_timer_stop(stop_ble_timer_handler);
        esp_timer_start_once(stop_ble_timer_handler, 300ULL * 1000000); // 300 seconds
        return;
    }
    esp_timer_create_args_t stop_ble_timer_args = {
        .callback = &stop_ble_timer_callback,
        .arg = NULL,
        .name = "stop_ble_timer"
    };
    esp_timer_create(&stop_ble_timer_args, &stop_ble_timer_handler);
    esp_timer_start_once(stop_ble_timer_handler, 300ULL * 1000000); // 300 seconds
    ESP_LOGI(TAG, "BLE timer started - Ble ");
}
