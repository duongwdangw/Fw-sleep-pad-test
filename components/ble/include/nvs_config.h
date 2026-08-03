#ifndef NVS_CONFIG_H
#define NVS_CONFIG_H

#include <stdbool.h>
#include "ble.h"

/* NVS namespace + keys — must match firmware/src/nvs-config.h on the
 * PlatformIO branch so a device flashed with either firmware reads the
 * same provisioning data. */
#define NVS_NAMESPACE   "sleeppad"
#define NVS_KEY_SSID    "wifi_ssid"
#define NVS_KEY_PASS    "wifi_pass"
#define NVS_KEY_HOST    "mqtt_host"
#define NVS_KEY_SCHEME  "mqtt_scheme"
#define NVS_KEY_USER    "mqtt_user"
#define NVS_KEY_MPASS   "mqtt_pass"

/* Persist a single string key under the sleeppad namespace.
 * Empty string is allowed (mqtt_user/mqtt_pass may legitimately be empty
 * for anonymous dev brokers). Returns true on success. */
bool nvs_config_save(const char *key, const char *value);

/* Load all 6 provisioning fields from NVS into `out`. Returns true if
 * the SSID is non-empty (i.e. enough data to attempt connect); false
 * means we should fall back to BLE pairing. The other fields may be
 * empty strings — handled by callers. */
bool nvs_config_load(ble_config_data_t *out);

#endif
