#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <stdio.h>
#include <driver/uart.h>

#include "sleep_pad.h"
#include "config_parameter.h"


#define TAG "SleepPad"

QueueHandle_t sp_data_queue_second_report;
QueueHandle_t sp_data_queue_minute_report;

void sp_host_send_data(uint8_t *data, uint16_t length){
    uart_write_bytes(SLEEP_PAD_UART_NUM, data, length);
}
//==Mode Setting==
void sp_set_mode(sleep_pad_mode_t mode){
    switch(mode){
        case SLEEP_PAD_MONITOR_MODE:
            // 7D 04 10 00 55 4E 43 4F 4E 46 49 47 45 44 20 0D
            uint8_t cmd[] = {0x7D, 0x04, 0x10, 0x00, 0x55, 0x4E, 0x43, 0x4F, 0x4E, 0x46, 0x49, 0x47, 0x45, 0x44, 0x20, 0x0D};
            sp_host_send_data(cmd, sizeof(cmd));
            break;
        case SLEEP_PAD_DATA_DEBUG_MODE:
            break;
        case SLEEP_PAD_BLE_DEBUG_MODE:
            break;
        case SLEEP_PAD_UPD_MODE:
            break;
    }
}
//==Parameter Setting==
void sp_reset_parameter(){
   // 7D 0A 10 00 55 4E 43 4F 4E 46 49 47 45 44 00 0D 
    uint8_t cmd[] = {0x7D, 0x0A, 0x10, 0x00, 0x55, 0x4E, 0x43, 0x4F, 0x4E, 0x46, 0x49, 0x47, 0x45, 0x44, 0x00, 0x0D};
    sp_host_send_data(cmd, sizeof(cmd));
}
void sp_get_parameter(){
    uint8_t cmd[] = {0x7D, 0x0A, 0x10, 0x00, 0x55, 0x4E, 0x43, 0x4F, 0x4E, 0x46, 0x49, 0x47, 0x45, 0x44, 0x01, 0x0D};
    sp_host_send_data(cmd, sizeof(cmd));
}
void sp_set_parameter(){
    uint8_t cmd[] = {0x7D, 0x0A, 0x10, 0x00, 0x55, 0x4E, 0x43, 0x4F, 0x4E, 0x46, 0x49, 0x47, 0x45, 0x44, 0x01, 0x0D};
    sp_host_send_data(cmd, sizeof(cmd));
}
//==parse data==
int sp_convert_hex_to_int(uint8_t *data, int length){
    int value = 0;
    for(int i=0; i<length; i++){
        value = (value << 8) | data[i];
    }
    return value;
}
float sp_convert_hex_to_float(uint8_t *data, int length){
    float value = 0;
    for(int i=0; i<length; i++){
        value = (value << 8) | data[i];
    }
    return value;
}
void sp_parse_parameter_report(uint8_t *data){

}
void sp_parse_data_second_report(uint8_t *data){
}
void sp_parse_data_minute_report(uint8_t *data){
}
//==Main process task==

void sp_queue_init(){
    sp_data_queue_second_report = xQueueCreate(10, 512);
    sp_data_queue_minute_report = xQueueCreate(10, 512);
}

void sp_read_uart_data_task(void *pvParameter){
    while(1){
        uint8_t data[1024];
        int length = uart_read_bytes(SLEEP_PAD_UART_NUM, data, sizeof(data), 1000 / portTICK_PERIOD_MS);
        if(length > 0){
            switch (length) {
                case 16:
                    ESP_LOGI(TAG, "Received response data\r\n");
                    if(data[1]==0x84){
                        if(data[14]==0x20){
                            ESP_LOGI(TAG, "entered monitor mode\r\n");
                        }
                        else if(data[14]==0x21){
                            ESP_LOGI(TAG, "monitor mode already entered\r\n");
                        }
                        else if(data[14]==0x22){
                            ESP_LOGI(TAG, "entered monitor mode failed\r\n");
                        }
                    }
                    if(data[1]==0x8A){
                        if(data[14]==0x00){
                            ESP_LOGI(TAG, "reset parameter success\r\n");
                        }
                        else if(data[14]==0x01){
                            ESP_LOGI(TAG, "reset parameter failed\r\n");
                        }
                    }
                    break;
                case 27:
                    ESP_LOGI(TAG,"Received data report each 1 second\r\n");
                    xQueueSend(sp_data_queue_second_report, data, 100 / portTICK_PERIOD_MS);
                    break;
                case 30:
                    ESP_LOGI(TAG,"Received data report each 1 minute\r\n");
                    xQueueSend(sp_data_queue_minute_report, data, 100 / portTICK_PERIOD_MS);
                    break;
                case 80:
                    ESP_LOGI(TAG,"Received parameter report\r\n");
                    break;
            }
        }
    }
}