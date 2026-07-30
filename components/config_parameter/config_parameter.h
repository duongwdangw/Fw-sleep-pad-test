#ifndef __CONFIG_PARAMETER_H__
#define __CONFIG_PARAMETER_H__
#include <driver/uart.h>

// Define the Sleep Pad UART parameters
#define SLEEP_PAD_UART_NUM UART_NUM_1
#define SLEEP_PAD_UART_BAUD 115200
#define SLEEP_PAD_BUFFER_SIZE 1024
#define SLEEP_PAD_TX_PIN 16
#define SLEEP_PAD_RX_PIN 17

// Define WiFi parameters
#define WIFI_SSID "CHTLab"
#define WIFI_PASSWORD "Coinhe2018"

// Define MQTT parameters
#define MQTT_BROKER_URI "wss://mqtt.sleeptech.me"
#define MQTT_USERNAME "sleeppad-device"
#define MQTT_PASSWORD "e2392d5bc4de3182b4788e9b5c777a8f"

// Dinh danh thiet bi (Dung de tao topic: sleeppad/{DEVICE_ID}/sec)
#define SLEEP_PAD_DEVICE_ID "CNUBABC31C"

#endif