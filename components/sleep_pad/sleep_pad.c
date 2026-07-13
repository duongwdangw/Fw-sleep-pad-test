#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <stdio.h>
#include <driver/uart.h>
#include <string.h>


#include "sleep_pad.h"
#include "config_parameter.h"
#include "include/sleep_pad.h"


#define TAG "SleepPad"

QueueHandle_t sp_data_queue_second_report;
QueueHandle_t sp_data_queue_minute_report;

sleep_pad_data_second_report_t sp_data_second_report;
sleep_pad_data_minute_report_t sp_data_minute_report;
sleep_pad_parameter_t sp_data_parameter_report;

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
    int value = 0;
    for(int i=0; i<length; i++){
        value = (value << 8) | data[i];
    }
    return (float)value;
}

float sp_convert_bytes_to_float_little_endian(const uint8_t data[4])
{
    uint32_t bits =
        ((uint32_t)data[0])       |
        ((uint32_t)data[1] << 8)  |
        ((uint32_t)data[2] << 16) |
        ((uint32_t)data[3] << 24);

    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

void sp_parse_parameter_report(uint8_t *data){
    sp_data_parameter_report.enter_exit_bed_threshold = sp_convert_bytes_to_float_little_endian(&data[15]);
    sp_data_parameter_report.body_movement_threshold = sp_convert_bytes_to_float_little_endian(&data[19]);
    sp_data_parameter_report.pressure_threshold = sp_convert_bytes_to_float_little_endian(&data[23]);
    sp_data_parameter_report.snoring_sensitivity = sp_convert_bytes_to_float_little_endian(&data[27]);
    sp_data_parameter_report.weak_breath_sensitivity =sp_convert_bytes_to_float_little_endian(&data[31]);
    sp_data_parameter_report.sleep_rt_staging_threshold_1 =sp_convert_bytes_to_float_little_endian(&data[35]);
    sp_data_parameter_report.sleep_rt_staging_threshold_2 =sp_convert_bytes_to_float_little_endian(&data[39]);
    sp_data_parameter_report.sleep_rt_staging_threshold_3 =sp_convert_bytes_to_float_little_endian(&data[43]);
    sp_data_parameter_report.sleep_staging_threshold_1 =sp_convert_bytes_to_float_little_endian(&data[47]);
    sp_data_parameter_report.sleep_staging_threshold_2 =sp_convert_bytes_to_float_little_endian(&data[51]);
    sp_data_parameter_report.sleep_staging_threshold_3 =sp_convert_bytes_to_float_little_endian(&data[55]);
    sp_data_parameter_report.daily_report_time =sp_convert_bytes_to_float_little_endian(&data[59]);
    sp_data_parameter_report.internal_parameter = sp_convert_bytes_to_float_little_endian(&data[63]);
    sp_data_parameter_report.hardware_enable = sp_convert_bytes_to_float_little_endian(&data[67]);
}

void sp_parse_data_second_report(uint8_t *data){
    sp_data_second_report.serial_number = sp_convert_hex_to_int(&data[14], 1);
    sp_data_second_report.time_stamp = sp_convert_hex_to_int(&data[15], 4);
    if(data[19]==0x01) sp_data_second_report.sleep_status = OUT_OF_BED;
    else if(data[19]==0x02) sp_data_second_report.sleep_status = BODY_MOVEMENT;
    else if(data[19]==0x03) sp_data_second_report.sleep_status = SITTING_UP;
    else if(data[19]==0x04) sp_data_second_report.sleep_status = SLEEPING;
    else if(data[19]==0x05) sp_data_second_report.sleep_status = WAKE_UP;
    else if(data[19]==0x06) sp_data_second_report.sleep_status = HAVY_THING_ON_BED;
    else if(data[19]==0x07) sp_data_second_report.sleep_status = SNORING;
    else if(data[19]==0x08) sp_data_second_report.sleep_status = WEAK_BREATH;
    sp_data_second_report.heart_rate = sp_convert_hex_to_int(&data[20], 1);
    sp_data_second_report.breathing_rate = sp_convert_hex_to_float(&data[21], 1);
    sp_data_second_report.sdata = sp_convert_hex_to_int(&data[22], 2);
    sp_data_second_report.pdata = sp_convert_hex_to_int(&data[24], 2);
}
void sp_parse_data_minute_report(uint8_t *data){
    sp_data_minute_report.serial_number = sp_convert_hex_to_int(&data[14], 1);
    sp_data_minute_report.time_stamp = sp_convert_hex_to_int(&data[15], 4);
    if(data[19]==0x01) sp_data_minute_report.sleep_status = OUT_OF_BED;
    else if(data[19]==0x02) sp_data_minute_report.sleep_status = BODY_MOVEMENT;
    else if(data[19]==0x03) sp_data_minute_report.sleep_status = SITTING_UP;
    else if(data[19]==0x04) sp_data_minute_report.sleep_status = SLEEPING;
    else if(data[19]==0x05) sp_data_minute_report.sleep_status = WAKE_UP;
    else if(data[19]==0x06) sp_data_minute_report.sleep_status = HAVY_THING_ON_BED;
    else if(data[19]==0x07) sp_data_minute_report.sleep_status = SNORING;
    else if(data[19]==0x08) sp_data_minute_report.sleep_status = WEAK_BREATH;
    sp_data_minute_report.heart_rate = sp_convert_hex_to_int(&data[20], 1);
    sp_data_minute_report.breathing_rate = sp_convert_hex_to_float(&data[21], 1);
    sp_data_minute_report.body_movement_count = sp_convert_hex_to_int(&data[22], 1);
    sp_data_minute_report.snoring_count = sp_convert_hex_to_int(&data[23], 1);
    sp_data_minute_report.respiratory_disorder_count = sp_convert_hex_to_int(&data[24], 1);
    sp_data_minute_report.PTHD = sp_convert_hex_to_int(&data[25], 2);
    sp_data_minute_report.TEMP = sp_convert_hex_to_int(&data[27], 2);
}

void sp_parse_data_second_report_v2(uint8_t *data){
    sp_data_second_report.serial_number = sp_convert_hex_to_int(&data[62], 1);
    // sp_data_second_report.time_stamp = sp_convert_hex_to_int(&data[63], 4);
    if(data[63]==0x01) sp_data_second_report.sleep_status = OUT_OF_BED;
    else if(data[63]==0x02) sp_data_second_report.sleep_status = BODY_MOVEMENT;
    else if(data[63]==0x03) sp_data_second_report.sleep_status = SITTING_UP;
    else if(data[19]==0x04) sp_data_second_report.sleep_status = SLEEPING;
    else if(data[63]==0x05) sp_data_second_report.sleep_status = WAKE_UP;
    else if(data[63]==0x06) sp_data_second_report.sleep_status = HAVY_THING_ON_BED;
    else if(data[63]==0x07) sp_data_second_report.sleep_status = SNORING;
    else if(data[63]==0x08) sp_data_second_report.sleep_status = WEAK_BREATH;
    sp_data_second_report.heart_rate = sp_convert_hex_to_int(&data[64], 1);
    sp_data_second_report.breathing_rate = sp_convert_hex_to_float(&data[65], 1);
    // sp_data_second_report.sdata = sp_convert_hex_to_int(&data[66], 2);
    // sp_data_second_report.pdata = sp_convert_hex_to_int(&data[68], 2);
}
// other datasheet
void sp_change_to_idle_mode(){
    char *cmd = "TAOSE";
    uint8_t cmd_length = strlen(cmd);
    uart_write_bytes(SLEEP_PAD_UART_NUM, cmd, cmd_length);
}

void sp_check_hardware_issue(){
    char *cmd = "TAOSH";
    uint8_t cmd_length = strlen(cmd);
    uart_write_bytes(SLEEP_PAD_UART_NUM, cmd, cmd_length);
}

void sp_request_send_data_once_a_second(){
    char *cmd = "TAOSG";
    uint8_t cmd_length = strlen(cmd);
    uart_write_bytes(SLEEP_PAD_UART_NUM, cmd, cmd_length);
}

//==Main process task==
void sp_queue_init(){
    sp_data_queue_second_report = xQueueCreate(10, 512);
    sp_data_queue_minute_report = xQueueCreate(10, 512);
}

void sp_read_uart_data_task(void *pvParameter){
    while(1){
        uint8_t data[66];
        int length = uart_read_bytes(SLEEP_PAD_UART_NUM, data, sizeof(data), 10 / portTICK_PERIOD_MS);
        if(length > 0){
            ESP_LOGI(TAG, "Received raw data: %d bytes\r\n", length);
            for(int i=0; i<length; i++){
                printf("%02X ", data[i]);
            }
            printf("\r\n");
            ESP_LOGI(TAG, "--------------------------------\r\n");
            if(strstr((char*)data,"OK") && strstr((char*)data,"0")) {printf("hardware okee\r\n"); continue;}
            if(strstr((char*)data,"OK") && strstr((char*)data,"1")) {printf("hardware failed\r\n"); continue;}
            if(strstr((char*)data, "OK")) {printf("Đã vào idle mode\r\n"); continue;}
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
                    // xQueueSend(sp_data_queue_second_report, data, 100 / portTICK_PERIOD_MS);
                    if(data[1]==0x85){
                        ESP_LOGI(TAG,"Received data report each 1 second\r\n");
                        sp_parse_data_second_report(data);
                        ESP_LOGI(TAG,"Serial Number: %d\r\n", sp_data_second_report.serial_number);
                        ESP_LOGI(TAG,"Time Stamp: %d\r\n", sp_data_second_report.time_stamp);
                        ESP_LOGI(TAG,"Sleep Status: %d\r\n", sp_data_second_report.sleep_status);
                        ESP_LOGI(TAG,"Heart Rate: %d\r\n", sp_data_second_report.heart_rate);
                        ESP_LOGI(TAG,"Breathing Rate: %f\r\n", sp_data_second_report.breathing_rate);
                        ESP_LOGI(TAG,"SData: %d\r\n", sp_data_second_report.sdata);
                        ESP_LOGI(TAG,"PData: %d\r\n", sp_data_second_report.pdata);
                        ESP_LOGI(TAG,"--------------------------------\r\n");
                    }
                    break;
                case 30:
                    // xQueueSend(sp_data_queue_minute_report, data, 100 / portTICK_PERIOD_MS);
                    if(data[1]==0x86){
                        ESP_LOGI(TAG,"Received data report each 1 minute\r\n");
                        sp_parse_data_minute_report(data);
                        ESP_LOGI(TAG,"Serial Number: %d\r\n", sp_data_minute_report.serial_number);
                        ESP_LOGI(TAG,"Time Stamp: %d\r\n", sp_data_minute_report.time_stamp);
                        ESP_LOGI(TAG,"Sleep Status: %d\r\n", sp_data_minute_report.sleep_status);
                        ESP_LOGI(TAG,"Heart Rate: %d\r\n", sp_data_minute_report.heart_rate);
                        ESP_LOGI(TAG,"Breathing Rate: %f\r\n", sp_data_minute_report.breathing_rate);
                        ESP_LOGI(TAG,"Body Movement Count: %d\r\n", sp_data_minute_report.body_movement_count);
                        ESP_LOGI(TAG,"Snoring Count: %d\r\n", sp_data_minute_report.snoring_count);
                        ESP_LOGI(TAG,"Respiratory Disorder Count: %d\r\n", sp_data_minute_report.respiratory_disorder_count);
                        ESP_LOGI(TAG,"PTHD: %d\r\n", sp_data_minute_report.PTHD);
                        ESP_LOGI(TAG,"TEMP: %d\r\n", sp_data_minute_report.TEMP);
                        ESP_LOGI(TAG,"--------------------------------\r\n");
                    }
                    break;
                case 80:
                    // ESP_LOGI(TAG,"Received parameter report\r\n");
                    if(data[1]==0x8A){
                        sp_parse_parameter_report(data);
                        ESP_LOGI(TAG,"Enter Exit Bed Threshold: %f\r\n", sp_data_parameter_report.enter_exit_bed_threshold);
                        ESP_LOGI(TAG,"Body Movement Threshold: %f\r\n", sp_data_parameter_report.body_movement_threshold);
                        ESP_LOGI(TAG,"Pressure Threshold: %f\r\n", sp_data_parameter_report.pressure_threshold);
                        ESP_LOGI(TAG,"Snoring Sensitivity: %f\r\n", sp_data_parameter_report.snoring_sensitivity);
                        ESP_LOGI(TAG,"Weak Breath Sensitivity: %f\r\n", sp_data_parameter_report.weak_breath_sensitivity);
                        ESP_LOGI(TAG,"Sleep RT Staging Threshold 1: %f\r\n", sp_data_parameter_report.sleep_rt_staging_threshold_1);
                        ESP_LOGI(TAG,"Sleep RT Staging Threshold 2: %f\r\n", sp_data_parameter_report.sleep_rt_staging_threshold_2);
                        ESP_LOGI(TAG,"Sleep RT Staging Threshold 3: %f\r\n", sp_data_parameter_report.sleep_rt_staging_threshold_3);
                    }
                    break;
                case 66:
                    if(data[1]==0x64){
                        sp_parse_data_second_report_v2(data);
                        ESP_LOGI(TAG,"Serial Number: %d\r\n", sp_data_second_report.serial_number);
                        // ESP_LOGI(TAG,"Time Stamp: %d\r\n", sp_data_second_report.time_stamp);
                        ESP_LOGI(TAG,"Sleep Status: %d\r\n", sp_data_second_report.sleep_status);
                        ESP_LOGI(TAG,"Heart Rate: %d\r\n", sp_data_second_report.heart_rate);
                        ESP_LOGI(TAG,"Breathing Rate: %f BPM\r\n", sp_data_second_report.breathing_rate/10.0f);
                        // ESP_LOGI(TAG,"SData: %d\r\n", sp_data_second_report.sdata);
                        // ESP_LOGI(TAG,"PData: %d\r\n", sp_data_second_report.pdata);
                        ESP_LOGI(TAG,"--------------------------------\r\n");
                    }
            }
        }
    }
}

// void sp_data_process_task(void *pvParameter){
//     sp_queue_init();
//     while(1){
//         if(xQueueReceive(sp_data_queue_second_report, &sp_data_second_report, 100/portTICK_PERIOD_MS))
//         {
//             sp_parse_data_second_report(&sp_data_second_report);
//         }
//         if(xQueueReceive(sp_data_queue_minute_report, &sp_data_minute_report, 100/portTICK_PERIOD_MS))
//         {
//             sp_parse_data_minute_report(&sp_data_minute_report);
//         }
//     }
// }