#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <nvs_flash.h>
#include <mqtt_client.h>
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

static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_mqtt_connected = false;

// ============================================================================
// WiFi
// ============================================================================
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data){
    if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START){
        esp_wifi_connect();
    } else if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED){
        ESP_LOGW(TAG, "WiFi mat ket noi, dang thu ket noi lai...");
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if(event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP){
        ESP_LOGI(TAG, "WiFi da co IP.");
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void){
    esp_err_t nvs_ret = nvs_flash_init();
    if(nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND){
        ESP_LOGW(TAG, "NVS can xoa va khoi tao lai (ret=0x%x)...", nvs_ret);
        nvs_flash_erase();
        nvs_ret = nvs_flash_init();
    }
    if(nvs_ret != ESP_OK){
        ESP_LOGE(TAG, "nvs_flash_init that bai (ret=0x%x) - WiFi/calibration co the hoat dong sai!", nvs_ret);
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

    ESP_LOGI(TAG, "Dang cho ket noi WiFi toi SSID '%s'...", WIFI_SSID);
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "WiFi ket noi thanh cong.");
}

// ============================================================================
// MQTT
// ============================================================================
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data){
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    (void)event; // hien chua doc chi tiet payload/topic tu event, chi xu ly theo event_id
    switch((esp_mqtt_event_id_t)event_id){
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT da ket noi toi broker.");
            s_mqtt_connected = true;
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT mat ket noi.");
            s_mqtt_connected = false;
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT loi ket noi/xac thuc. Kiem tra lai URI/username/password "
                          "va dam bao sdkconfig co bat CONFIG_MBEDTLS_CERTIFICATE_BUNDLE cho wss://.");
            break;
        default:
            break;
    }
}

void mqtt_app_start(void){
    esp_mqtt_client_config_t mqtt_cfg = { 0 };
    mqtt_cfg.broker.address.uri = MQTT_BROKER_URI;
    mqtt_cfg.credentials.username = MQTT_USERNAME;
    mqtt_cfg.credentials.authentication.password = MQTT_PASSWORD;
    // BAT BUOC voi wss://: phai khai bao ro cach xac thuc chung chi server,
    // neu khong esp-mqtt/esp-tls se bao loi ESP_ERR_MBEDTLS_SSL_SETUP_FAILED.
    // Dung bo chung chi goc co san cua ESP-IDF (yeu cau sdkconfig da bat
    // CONFIG_MBEDTLS_CERTIFICATE_BUNDLE, mac dinh thuong da bat san).
    mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_mqtt_client, -1 /*ESP_EVENT_ANY_ID*/, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);
    ESP_LOGI(TAG, "Dang ket noi MQTT toi %s ...", MQTT_BROKER_URI);
}

// ============================================================================
// Chuyen doi trang thai THAT cua phan cung (0-5, theo tai lieu LSM-800-T
// UART Hardware Spec V106) sang thang trang thai PUBLISH (1-8) theo dung
// yeu cau file MQTT ban gui.
//
// QUAN TRONG - toi phai noi that: phan cung CHI co 6 trang thai (0-5), trong
// khi thang publish co 8 muc (bao gom "situp"=3 va "weakup"=5) ma phan cung
// nay KHONG co khai niem tuong ung. Vi vay 2 ma 3 va 5 ben publish se
// KHONG BAO GIO xuat hien tu thiet bi that - day la gioi han vat ly cua
// phan cung, khong phai loi code. Neu doi yeu cau MQTT can dung 2 trang
// thai do that, se can phan cung/thuat toan khac co kha nang phat hien.
// ============================================================================
static int sp_convert_status_to_publish_scale(sleep_status_t raw){
    switch(raw){
        case SP_STATUS_OFF_BED:        return 1; // getout
        case SP_STATUS_BODY_MOVEMENT:  return 2; // move
        case SP_STATUS_IN_BED:         return 4; // sleep
        case SP_STATUS_HEAVY_OBJECT:   return 6; // vat nang
        case SP_STATUS_SNORING:        return 7; // ngay
        case SP_STATUS_WEAK_BREATHING: return 8; // tho yeu
        default:                       return 1;
    }
}

// Lay timestamp Unix that. CANH BAO: can cau hinh dong bo gio qua SNTP
// (vi du esp_sntp_init) o noi khac trong project thi time(NULL) moi tra ve
// dung gio thuc; neu chua cau hinh SNTP, gia tri nay se KHONG chinh xac
// (thuong la so giay tu luc boot). Toi khong tu them SNTP vao day vi ngoai
// pham vi cua sleep_pad component, can ban tich hop rieng.
static long sp_get_unix_timestamp(void){
    return (long)time(NULL);
}

static void sp_publish_json(const char *topic, cJSON *root){
    char *payload = cJSON_PrintUnformatted(root);
    if(payload){
        if(s_mqtt_connected){
            esp_mqtt_client_publish(s_mqtt_client, topic, payload, 0, 1, 0);
        } else {
            ESP_LOGW(TAG, "MQTT chua ket noi, bo qua publish toi %s", topic);
        }
        ESP_LOGI(TAG, "Publish -> %s : %s", topic, payload);
        free(payload);
    }
    cJSON_Delete(root);
}

// ============================================================================
// Task chinh: nhan du lieu tu hang doi, publish moi giay, gop va publish
// moi phut.
// ============================================================================
void sp_data_process_task(void *pvParameter){
    sleep_pad_data_second_report_t d;
    ESP_LOGI(TAG, "sp_data_process_task: da khoi dong");

    int sec_topic_len = strlen("sleeppad/") + strlen(SLEEP_PAD_DEVICE_ID) + strlen("/sec") + 1;
    char sec_topic[64];
    char min_topic[64];
    snprintf(sec_topic, sizeof(sec_topic), "sleeppad/%s/sec", SLEEP_PAD_DEVICE_ID);
    snprintf(min_topic, sizeof(min_topic), "sleeppad/%s/min", SLEEP_PAD_DEVICE_ID);
    (void)sec_topic_len;

    // Bo dem gop du lieu theo phut (60 mau giay)
    int minute_sample_count = 0;
    int minute_movement_count = 0;
    int minute_snore_count = 0;
    int minute_resp_disorder_count = 0;
    int minute_last_heart_rate = 0;
    float minute_last_breathing_rate = 0.0f;
    int minute_last_status_raw = SP_STATUS_OFF_BED;
    int minute_last_pdata = 0;

    while(1){
        if(xQueueReceive(sp_data_queue_second_report, &d, portMAX_DELAY) == pdTRUE){

            int pub_status = sp_convert_status_to_publish_scale(d.sleep_status);

            // ---- Publish moi giay: sleeppad/{device}/sec ----
            cJSON *sec = cJSON_CreateObject();
            cJSON_AddStringToObject(sec, "device_id", SLEEP_PAD_DEVICE_ID);
            cJSON_AddNumberToObject(sec, "ts", sp_get_unix_timestamp());
            cJSON_AddNumberToObject(sec, "status", pub_status);
            cJSON_AddNumberToObject(sec, "heart_rate", d.heart_rate);
            cJSON_AddNumberToObject(sec, "resp_rate", d.breathing_rate);
            cJSON_AddNumberToObject(sec, "sdata", d.sdata);
            cJSON_AddNumberToObject(sec, "pdata", d.pdata);
            sp_publish_json(sec_topic, sec);

            // ---- Gop du lieu cho ban tin phut ----
            minute_sample_count++;
            if(d.sleep_status == SP_STATUS_BODY_MOVEMENT) minute_movement_count++;
            if(d.sleep_status == SP_STATUS_SNORING) minute_snore_count++;
            if(d.sleep_status == SP_STATUS_WEAK_BREATHING) minute_resp_disorder_count++;
            minute_last_heart_rate = d.heart_rate;
            minute_last_breathing_rate = d.breathing_rate;
            minute_last_status_raw = d.sleep_status;
            minute_last_pdata = d.pdata;

            if(minute_sample_count >= 60){
                // ---- Publish moi phut: sleeppad/{device}/min ----
                cJSON *min = cJSON_CreateObject();
                cJSON_AddStringToObject(min, "device_id", SLEEP_PAD_DEVICE_ID);
                cJSON_AddNumberToObject(min, "ts", sp_get_unix_timestamp());
                cJSON_AddNumberToObject(min, "status", sp_convert_status_to_publish_scale((sleep_status_t)minute_last_status_raw));
                cJSON_AddNumberToObject(min, "heart_rate", minute_last_heart_rate);
                cJSON_AddNumberToObject(min, "resp_rate", minute_last_breathing_rate);
                cJSON_AddNumberToObject(min, "movement", minute_movement_count);
                cJSON_AddNumberToObject(min, "snore_count", minute_snore_count);
                cJSON_AddNumberToObject(min, "resp_disorder", minute_resp_disorder_count);
                cJSON_AddNumberToObject(min, "pthd", minute_last_pdata);
                // "temp": phan cung LSM-800-T KHONG co cam bien nhiet do (theo dung
                // tai lieu UART Hardware Spec V106 - khong co muc nao noi ve nhiet
                // do). Gui 0.0 de dung schema JSON yeu cau, KHONG phai gia tri do
                // duoc that - can lam ro voi doi yeu cau neu truong nay bat buoc.
                cJSON_AddNumberToObject(min, "temp", 0.0);
                sp_publish_json(min_topic, min);

                minute_sample_count = 0;
                minute_movement_count = 0;
                minute_snore_count = 0;
                minute_resp_disorder_count = 0;
            }
        }
    }
}