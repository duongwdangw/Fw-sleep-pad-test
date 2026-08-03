#ifndef MQTT_WIFI_H
#define MQTT_WIFI_H

#include <esp_err.h>

extern bool mqtt_connected;

esp_err_t mqtt_publish_data(const char *topic, const char *data);
esp_err_t mqtt_stop(void);
void mqtt_init(const char *mqtt_address, const char *client_id, const char *username, const char *password);

#endif