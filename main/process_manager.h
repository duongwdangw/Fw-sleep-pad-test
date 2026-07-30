#ifndef __PROCESS_MANAGER_H__
#define __PROCESS_MANAGER_H__

// Khoi tao WiFi (STA mode) va cho den khi co IP.
void wifi_init_sta(void);

// Dong bo thoi gian thuc tu Internet thong qua NTP Server.
void sync_time_from_ntp(void);

// Khoi tao va ket noi MQTT client toi broker.
void mqtt_app_start(void);

// Task nhan du lieu tu queue, chuyen doi sang JSON va publish len MQTT.
void sp_data_process_task(void *pvParameter);

#endif