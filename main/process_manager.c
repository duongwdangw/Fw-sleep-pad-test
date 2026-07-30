#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <nvs_flash.h>
#include <esp_websocket_client.h>
#include <cJSON.h>
#include <esp_crt_bundle.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include "sleep_pad.h"
#include "process_manager.h"
#include "config_parameter.h"

#define TAG "PROC_MGR"

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static esp_websocket_client_handle_t s_ws_client = NULL;
static bool s_ws_connected = false;

// ============================================================================
// WiFi
// ============================================================================
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi mat ket noi, dang thu ket noi lai...");
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "WiFi da co IP.");
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void) {
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_ret = nvs_flash_init();
    }
    
    s_wifi_event_group = xEventGroupCreate();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = { 0 };
    strncpy((char*)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);
    
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "WiFi ket noi thanh cong.");
}

// ============================================================================
// WEBSOCKET
// ============================================================================
static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WebSocket da ket noi toi server.");
            s_ws_connected = true;
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "WebSocket mat ket noi.");
            s_ws_connected = false;
            break;
        case WEBSOCKET_EVENT_DATA:
            ESP_LOGI(TAG, "Nhan lenh tu server: %.*s", data->data_len, (char *)data->data_ptr);
            break;
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "WebSocket loi ket noi. Kiem tra lai URI va SSL Bundle.");
            break;
    }
}

void websocket_app_start(void) {
    esp_websocket_client_config_t ws_cfg = {
        .uri = WEBSOCKET_URI,
        .crt_bundle_attach = esp_crt_bundle_attach, // Bat buoc cho wss://
    };

    s_ws_client = esp_websocket_client_init(&ws_cfg);
    esp_websocket_register_events(s_ws_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)s_ws_client);
    esp_websocket_client_start(s_ws_client);
    ESP_LOGI(TAG, "Dang ket noi WebSocket toi %s ...", WEBSOCKET_URI);
}

// ============================================================================
// JSON & LOGIC
// ============================================================================
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

// Ham lay thoi gian thuc Unix Epoch (Con so vi du: 1722243819)
static long sp_get_unix_timestamp(void) {
    return (long)time(NULL);
}

static void sp_publish_json(const char *topic, cJSON *root) {
    cJSON_AddStringToObject(root, "topic", topic);
    
    char *payload = cJSON_PrintUnformatted(root);
    if(payload) {
        if(s_ws_connected) {
            esp_websocket_client_send_text(s_ws_client, payload, strlen(payload), portMAX_DELAY);
            ESP_LOGI(TAG, "Send WS -> %s", payload);
        } else {
            ESP_LOGW(TAG, "WS chua ket noi, bo qua gui data.");
        }
        free(payload);
    }
    cJSON_Delete(root);
}

void sp_data_process_task(void *pvParameter) {
    sleep_pad_data_second_report_t d;
    ESP_LOGI(TAG, "sp_data_process_task: da khoi dong (WebSocket Mode)");

    char sec_topic[64];
    char min_topic[64];
    snprintf(sec_topic, sizeof(sec_topic), "sleeppad/%s/sec", SLEEP_PAD_DEVICE_ID);
    snprintf(min_topic, sizeof(min_topic), "sleeppad/%s/min", SLEEP_PAD_DEVICE_ID);

    int minute_sample_count = 0;
    int minute_movement_count = 0;
    int minute_snore_count = 0;
    int minute_resp_disorder_count = 0;
    int minute_last_heart_rate = 0;
    float minute_last_breathing_rate = 0.0f;
    int minute_last_status_raw = SP_STATUS_OFF_BED;
    int minute_last_pdata = 0;

    while(1) {
        if(xQueueReceive(sp_data_queue_second_report, &d, portMAX_DELAY) == pdTRUE) {
            int pub_status = sp_convert_status_to_publish_scale(d.sleep_status);

            // 1. Tao va gui Payload Giay
            cJSON *sec = cJSON_CreateObject();
            cJSON_AddStringToObject(sec, "device_id", SLEEP_PAD_DEVICE_ID);
            cJSON_AddNumberToObject(sec, "ts", sp_get_unix_timestamp()); // Timestamp chuan nguyen
            cJSON_AddNumberToObject(sec, "status", pub_status);
            cJSON_AddNumberToObject(sec, "heart_rate", d.heart_rate);
            cJSON_AddNumberToObject(sec, "resp_rate", d.breathing_rate);
            cJSON_AddNumberToObject(sec, "sdata", d.sdata);
            cJSON_AddNumberToObject(sec, "pdata", d.pdata);
            sp_publish_json(sec_topic, sec);

            // 2. Tinh toan thong ke cho phut
            minute_sample_count++;
            if(d.sleep_status == SP_STATUS_BODY_MOVEMENT) minute_movement_count++;
            if(d.sleep_status == SP_STATUS_SNORING) minute_snore_count++;
            if(d.sleep_status == SP_STATUS_WEAK_BREATHING) minute_resp_disorder_count++;
            minute_last_heart_rate = d.heart_rate;
            minute_last_breathing_rate = d.breathing_rate;
            minute_last_status_raw = d.sleep_status;
            minute_last_pdata = d.pdata;

            // 3. Gui Payload Phut neu du 60 mau
            if(minute_sample_count >= 60) {
                cJSON *min = cJSON_CreateObject();
                cJSON_AddStringToObject(min, "device_id", SLEEP_PAD_DEVICE_ID);
                cJSON_AddNumberToObject(min, "ts", sp_get_unix_timestamp()); // Timestamp chuan nguyen
                cJSON_AddNumberToObject(min, "status", sp_convert_status_to_publish_scale((sleep_status_t)minute_last_status_raw));
                cJSON_AddNumberToObject(min, "heart_rate", minute_last_heart_rate);
                cJSON_AddNumberToObject(min, "resp_rate", minute_last_breathing_rate);
                cJSON_AddNumberToObject(min, "movement", minute_movement_count);
                cJSON_AddNumberToObject(min, "snore_count", minute_snore_count);
                cJSON_AddNumberToObject(min, "resp_disorder", minute_resp_disorder_count);
                cJSON_AddNumberToObject(min, "pthd", minute_last_pdata);
                cJSON_AddNumberToObject(min, "temp", 0.0);
                
                sp_publish_json(min_topic, min);

                // Reset bo dem
                minute_sample_count = 0;
                minute_movement_count = 0;
                minute_snore_count = 0;
                minute_resp_disorder_count = 0;
            }
        }
    }
}