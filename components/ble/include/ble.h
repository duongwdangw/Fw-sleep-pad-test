#ifndef BLE_H
#define BLE_H

#include <stdbool.h>

/* Maximum payload sizes for configuration written over BLE. */
#define BLE_WIFI_SSID_MAX_LEN        32
#define BLE_WIFI_PASSWORD_MAX_LEN    64
#define BLE_MQTT_BROKER_MAX_LEN      128
#define BLE_MQTT_USERNAME_MAX_LEN    64
#define BLE_MQTT_PASSWORD_MAX_LEN    64
#define BLE_MQTT_SCHEME_MAX_LEN      16

/* Device ID is "CNU" + 7 hex chars from low 28 bits of efuse MAC = 10 ASCII. */
#define BLE_DEVICE_ID_LEN            10

/* Status values published on the Status characteristic (READ/NOTIFY).
 * Flutter app polls this after writing all 6 WRITE chars and waits up to 60s
 * for "connected" before considering pairing successful. */
#define BLE_STATUS_WAITING       "waiting"
#define BLE_STATUS_CONNECTING    "connecting"
#define BLE_STATUS_CONNECTED     "connected"
#define BLE_STATUS_WIFI_FAILED   "wifi_failed"
#define BLE_STATUS_MQTT_FAILED   "mqtt_failed"

typedef struct {
	char wifi_ssid[BLE_WIFI_SSID_MAX_LEN + 1];
	char wifi_password[BLE_WIFI_PASSWORD_MAX_LEN + 1];
	char mqtt_broker[BLE_MQTT_BROKER_MAX_LEN + 1];
	char mqtt_username[BLE_MQTT_USERNAME_MAX_LEN + 1];
	char mqtt_password[BLE_MQTT_PASSWORD_MAX_LEN + 1];
	char mqtt_scheme[BLE_MQTT_SCHEME_MAX_LEN + 1];
} ble_config_data_t;

extern ble_config_data_t s_config;

void ble_init(void);
const ble_config_data_t *ble_get_config(void);

/* Returns the per-chip device_id (10 ASCII chars, NUL-terminated).
 * Lazy: derives from efuse MAC on first call so callers may invoke this
 * before ble_init() (e.g. main.c when deciding whether to skip BLE
 * pairing because NVS already has WiFi creds). */
const char *ble_get_device_id(void);

/* True after ble_init() has been called and the BLE stack is up. Used
 * by the MQTT shutdown path to skip BLE teardown when we boot directly
 * from NVS-saved creds and never advertised in the first place. */
bool ble_is_active(void);

/* Update the Status characteristic and send a notify to the connected client.
 * Safe to call before any client has subscribed — only writes the attribute
 * value when no connection is active. */
void ble_set_status(const char *status);

#endif
