#ifndef __PROCESS_MANAGER_H__
#define __PROCESS_MANAGER_H__

// Khoi tao WiFi (STA mode) va cho den khi co IP. Goi 1 lan trong main_task,
// TRUOC khi goi mqtt_app_start().
void wifi_init_sta(void);

// Khoi tao va ket noi MQTT client toi broker cau hinh trong config_parameter.h
// (wss://mqtt.sleeptech.me). Goi SAU khi wifi_init_sta() da co IP.
void mqtt_app_start(void);

// Task nhan du lieu tu sp_data_queue_second_report, chuyen doi sang dinh dang
// JSON theo dung schema yeu cau, va publish len MQTT:
//   - Moi giay  -> sleeppad/{DEVICE_ID}/sec
//   - Moi phut  -> sleeppad/{DEVICE_ID}/min (gop tu 60 mau giay)
void sp_data_process_task(void *pvParameter);

#endif