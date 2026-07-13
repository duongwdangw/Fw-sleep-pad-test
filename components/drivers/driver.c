#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/uart.h>
#include <driver.h>
#include <esp_log.h>
#include <stdio.h>

#include "config_parameter.h"

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