#ifndef __PROCESS_MANAGER_H__
#define __PROCESS_MANAGER_H__

// Khoi tao WiFi (STA mode) va cho den khi co IP. 
void wifi_init_sta(void);

// Khoi tao va ket noi WebSocket client toi server.
void websocket_app_start(void);

// Task nhan du lieu va gui qua WebSocket
void sp_data_process_task(void *pvParameter);

#endif