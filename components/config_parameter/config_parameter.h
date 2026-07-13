#ifndef __CONFIG_PARAMETER_H__
#define __CONFIG_PARAMETER_H__
#include <driver/uart.h>

//Define mqtt enviroment
//...
//Define the Sleep Pad UART parameters
#define SLEEP_PAD_UART_NUM UART_NUM_1
#define SLEEP_PAD_UART_BAUD 115200
#define SLEEP_PAD_BUFFER_SIZE 1024
#define SLEEP_PAD_TX_PIN 16
#define SLEEP_PAD_RX_PIN 17


#endif