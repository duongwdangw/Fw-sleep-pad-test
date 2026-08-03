#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <cJSON.h>
#include <string.h>
#include <time.h>

#include "sleep_pad.h"
#include "process_manager.h"
#include "ble.h"
#include "mqtt_wifi.h"

#define TAG "PROC_MGR"

static int sp_convert_status_to_publish_scale(sleep_status_t raw) {
    switch(raw) {
        case SP_STATUS_OFF_BED:        return 1;
        case SP_STATUS_BODY_MOVEMENT:  return 2;
        case SP_STATUS_IN_BED:         return 4;
        case SP_STATUS_HEAVY_OBJECT:   return 6;
        case SP_STATUS_SNORING:        return 7;
        case SP_STATUS_WEAK_BREATHING: return 8;
        default:                       return 1;
    }
}

static long sp_get_unix_timestamp(void) {
    long current_time = (long)time(NULL);
    // Nếu chưa có WiFi, current_time sẽ rất nhỏ. Cộng vào mốc ảo để luôn có 10 chữ số.
    if (current_time < 1700000000) {
        long mock_base_time = 1785317204; 
        return mock_base_time + current_time; 
    }
    // Khi có WiFi, tự trả về giờ thực NTP chuẩn.
    return current_time;
}

static void sp_publish_json(const char *topic, cJSON *root) {
    char *payload = cJSON_PrintUnformatted(root);
    if (payload) {
        // In ra màn hình giống hệt format gốc của bạn
        ESP_LOGI(TAG, "Publish -> %s", payload);
        
        if (mqtt_connected) {
            mqtt_publish_data(topic, payload);
        } else {
            // Giữ lại dòng cảnh báo này để bạn biết nếu rớt mạng
            ESP_LOGW(TAG, "MQTT chua ket noi, bo qua du lieu JSON.");
        }
        free(payload);
    }
    cJSON_Delete(root);
}
void sp_data_process_task(void *pvParameter) {
    sleep_pad_data_second_report_t d;
    ESP_LOGI(TAG, "sp_data_process_task: da khoi dong");

    const char* device_id = ble_get_device_id();
    char sec_topic[64];
    char min_topic[64];
    snprintf(sec_topic, sizeof(sec_topic), "sleeppad/%s/sec", device_id);
    snprintf(min_topic, sizeof(min_topic), "sleeppad/%s/min", device_id);

    int minute_sample_count = 0;
    int minute_movement_count = 0;
    int minute_snore_count = 0;
    int minute_resp_disorder_count = 0;
    int minute_last_heart_rate = 0;
    float minute_last_breathing_rate = 0.0f;
    int minute_last_status_raw = SP_STATUS_OFF_BED;
    int minute_last_pdata = 0;

    while (1) {
        if (xQueueReceive(sp_data_queue_second_report, &d, portMAX_DELAY) == pdTRUE) {
            int pub_status = sp_convert_status_to_publish_scale(d.sleep_status);

            // BẢN TIN GIÂY
            cJSON *sec = cJSON_CreateObject();
            cJSON_AddStringToObject(sec, "device_id", device_id);
            cJSON_AddNumberToObject(sec, "ts", sp_get_unix_timestamp());
            cJSON_AddNumberToObject(sec, "status", pub_status);
            cJSON_AddNumberToObject(sec, "heart_rate", d.heart_rate);
            cJSON_AddNumberToObject(sec, "resp_rate", d.breathing_rate);
            cJSON_AddNumberToObject(sec, "sdata", d.sdata);
            cJSON_AddNumberToObject(sec, "pdata", d.pdata);
            if(mqtt_connected)
            {
              sp_publish_json(sec_topic, sec);
            }
            // TÍCH LŨY DỮ LIỆU PHÚT
            minute_sample_count++;
            if (d.sleep_status == SP_STATUS_BODY_MOVEMENT) minute_movement_count++;
            if (d.sleep_status == SP_STATUS_SNORING) minute_snore_count++;
            if (d.sleep_status == SP_STATUS_WEAK_BREATHING) minute_resp_disorder_count++;
            minute_last_heart_rate = d.heart_rate;
            minute_last_breathing_rate = d.breathing_rate;
            minute_last_status_raw = d.sleep_status;
            minute_last_pdata = d.pdata;

            // BẢN TIN PHÚT
            if (minute_sample_count >= 60) {
                cJSON *min = cJSON_CreateObject();
                cJSON_AddStringToObject(min, "device_id", device_id);
                cJSON_AddNumberToObject(min, "ts", sp_get_unix_timestamp());
                cJSON_AddNumberToObject(min, "status", sp_convert_status_to_publish_scale((sleep_status_t)minute_last_status_raw));
                cJSON_AddNumberToObject(min, "heart_rate", minute_last_heart_rate);
                cJSON_AddNumberToObject(min, "resp_rate", minute_last_breathing_rate);
                cJSON_AddNumberToObject(min, "movement", minute_movement_count);
                cJSON_AddNumberToObject(min, "snore_count", minute_snore_count);
                cJSON_AddNumberToObject(min, "resp_disorder", minute_resp_disorder_count);
                cJSON_AddNumberToObject(min, "pthd", minute_last_pdata);
                cJSON_AddNumberToObject(min, "temp", 0.0);
                if(mqtt_connected)
                {
                    sp_publish_json(min_topic, min);
                }
                // Reset biến đếm sau mỗi phút
                minute_sample_count = 0;
                minute_movement_count = 0;
                minute_snore_count = 0;
                minute_resp_disorder_count = 0;
            }
        }
    }
}