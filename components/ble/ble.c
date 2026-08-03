#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <esp_bt.h>
#include <esp_bt_defs.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>
#include <esp_gatts_api.h>
#include <esp_gatt_common_api.h>
#include <esp_err.h>
#include <esp_mac.h>
#include <nvs_flash.h>

#include "ble.h"
#include "connect_wifi.h"
#include "nvs_config.h"

static const char *TAG = "BLE";

#define GATTS_APP_ID               0
#define GATTS_SERVICE_INSTANCE_ID  0

/* 128-bit UUIDs (per docs/protocols/ble-provisioning-and-mqtt-auth.md), stored
 * in little-endian byte order required by ESP-IDF GATT APIs.
 *
 * Service:    4fafc201-1fb5-459e-8fcc-c5c9c331914b
 * SSID:       beb5483e-36e1-4688-b7f5-ea07361b26a8
 * Password:   ...26a9
 * ServerHost: ...26aa
 * Status:     ...26ab  (READ/NOTIFY)
 * MqttUser:   ...26ac
 * MqttPass:   ...26ad
 * MqttScheme: ...26ae
 * DeviceId:   ...26af  (READ)
 */
static uint8_t s_uuid_service[16] = {
    0x4b, 0x91, 0x31, 0xc3, 0xc9, 0xc5, 0xcc, 0x8f,
    0x9e, 0x45, 0xb5, 0x1f, 0x01, 0xc2, 0xaf, 0x4f,
};

/* All char UUIDs share the same 15 trailing bytes; only byte[0] differs. */
#define CHAR_UUID_TAIL \
    0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7, 0x88, \
    0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe

static uint8_t s_uuid_ssid[16]      = { 0xa8, CHAR_UUID_TAIL };
static uint8_t s_uuid_password[16]  = { 0xa9, CHAR_UUID_TAIL };
static uint8_t s_uuid_host[16]      = { 0xaa, CHAR_UUID_TAIL };
static uint8_t s_uuid_status[16]    = { 0xab, CHAR_UUID_TAIL };
static uint8_t s_uuid_mqtt_user[16] = { 0xac, CHAR_UUID_TAIL };
static uint8_t s_uuid_mqtt_pass[16] = { 0xad, CHAR_UUID_TAIL };
static uint8_t s_uuid_scheme[16]    = { 0xae, CHAR_UUID_TAIL };
static uint8_t s_uuid_device_id[16] = { 0xaf, CHAR_UUID_TAIL };

enum {
    IDX_SVC,

    IDX_SSID_DECL, IDX_SSID_VAL, IDX_SSID_DESC,
    IDX_PASS_DECL, IDX_PASS_VAL, IDX_PASS_DESC,
    IDX_HOST_DECL, IDX_HOST_VAL, IDX_HOST_DESC,

    /* Status: declaration, value, CCCD (notify enable), user description */
    IDX_STATUS_DECL, IDX_STATUS_VAL, IDX_STATUS_CCCD, IDX_STATUS_DESC,

    IDX_MUSER_DECL,  IDX_MUSER_VAL,  IDX_MUSER_DESC,
    IDX_MPASS_DECL,  IDX_MPASS_VAL,  IDX_MPASS_DESC,
    IDX_SCHEME_DECL, IDX_SCHEME_VAL, IDX_SCHEME_DESC,

    IDX_DEVID_DECL, IDX_DEVID_VAL, IDX_DEVID_DESC,

    IDX_NB,
};

ble_config_data_t s_config;
static char s_device_id[BLE_DEVICE_ID_LEN + 1];

static bool s_ble_started;
static esp_gatt_if_t s_gatts_if = ESP_GATT_IF_NONE;
static uint16_t s_conn_id = 0;
static bool s_client_connected = false;
static bool s_status_notify_enabled = false;
static char s_status_value[16] = BLE_STATUS_WAITING;

static uint16_t s_handle_table[IDX_NB];

/* Standard 16-bit UUIDs used in the GATT attribute table. */
static uint16_t s_uuid_primary_service = ESP_GATT_UUID_PRI_SERVICE;
static uint16_t s_uuid_char_decl       = ESP_GATT_UUID_CHAR_DECLARE;
static uint16_t s_uuid_char_user_desc  = ESP_GATT_UUID_CHAR_DESCRIPTION;
static uint16_t s_uuid_cccd            = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

static uint8_t s_prop_write       = ESP_GATT_CHAR_PROP_BIT_WRITE;
static uint8_t s_prop_read_notify = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static uint8_t s_prop_read        = ESP_GATT_CHAR_PROP_BIT_READ;

static const uint8_t s_desc_ssid[]      = "WiFi SSID";
static const uint8_t s_desc_password[]  = "WiFi Password";
static const uint8_t s_desc_host[]      = "Server Host";
static const uint8_t s_desc_status[]    = "Status";
static const uint8_t s_desc_mqtt_user[] = "MQTT Username";
static const uint8_t s_desc_mqtt_pass[] = "MQTT Password";
static const uint8_t s_desc_scheme[]    = "MQTT Scheme";
static const uint8_t s_desc_device_id[] = "Device ID";

static uint16_t s_cccd_value = 0; /* notify-enable bitmap, written by client */

static bool flag_data_written[6] = {0}; /* SSID, Pass, Host, MUser, MPass, Scheme */

static uint8_t s_prep_write_buf[256];
static uint16_t s_prep_write_len = 0;
static uint16_t s_prep_write_handle = 0;

/* Primary adv data — name only. The 128-bit service UUID + a 15-char name
 * blow past the 31-byte AD budget and the controller drops part of the
 * payload. Flutter scans by name prefix `SleepPad-`, so the UUID is moved
 * to the scan response below. */
static esp_ble_adv_data_t s_adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = false,
    .min_interval = 0x20,
    .max_interval = 0x40,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 0,
    .p_service_uuid = NULL,
    .flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT,
};

/* Scan response — carries the 128-bit service UUID for tools that filter
 * by service. Flutter doesn't need this but it's standard practice. */
static esp_ble_adv_data_t s_scan_rsp_data = {
    .set_scan_rsp = true,
    .include_name = false,
    .include_txpower = false,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(s_uuid_service),
    .p_service_uuid = s_uuid_service,
    .flag = 0,
};

static esp_ble_adv_params_t s_adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

#define ATTR_DECL(uuid_p, prop_p) {                              \
    {ESP_GATT_AUTO_RSP},                                         \
    {                                                            \
        ESP_UUID_LEN_16,                                         \
        (uint8_t *)&s_uuid_char_decl,                            \
        ESP_GATT_PERM_READ,                                      \
        sizeof(uint8_t),                                         \
        sizeof(uint8_t),                                         \
        (uint8_t *)(prop_p),                                     \
    },                                                           \
}

#define ATTR_VALUE_WRITE(uuid_p, max_len) {                      \
    {ESP_GATT_AUTO_RSP},                                         \
    {                                                            \
        ESP_UUID_LEN_128,                                        \
        (uuid_p),                                                \
        ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,                \
        (max_len),                                               \
        0,                                                       \
        NULL,                                                    \
    },                                                           \
}

#define ATTR_VALUE_READ(uuid_p, max_len) {                       \
    {ESP_GATT_AUTO_RSP},                                         \
    {                                                            \
        ESP_UUID_LEN_128,                                        \
        (uuid_p),                                                \
        ESP_GATT_PERM_READ,                                      \
        (max_len),                                               \
        0,                                                       \
        NULL,                                                    \
    },                                                           \
}

#define ATTR_USER_DESC(desc_arr) {                               \
    {ESP_GATT_AUTO_RSP},                                         \
    {                                                            \
        ESP_UUID_LEN_16,                                         \
        (uint8_t *)&s_uuid_char_user_desc,                       \
        ESP_GATT_PERM_READ,                                      \
        sizeof(desc_arr),                                        \
        sizeof(desc_arr),                                        \
        (uint8_t *)(desc_arr),                                   \
    },                                                           \
}

static const esp_gatts_attr_db_t s_gatt_db[IDX_NB] = {
    [IDX_SVC] = {
        {ESP_GATT_AUTO_RSP},
        {
            ESP_UUID_LEN_16,
            (uint8_t *)&s_uuid_primary_service,
            ESP_GATT_PERM_READ,
            sizeof(s_uuid_service),
            sizeof(s_uuid_service),
            s_uuid_service,
        },
    },

    [IDX_SSID_DECL] = ATTR_DECL(s_uuid_ssid, &s_prop_write),
    [IDX_SSID_VAL]  = ATTR_VALUE_WRITE(s_uuid_ssid, BLE_WIFI_SSID_MAX_LEN),
    [IDX_SSID_DESC] = ATTR_USER_DESC(s_desc_ssid),

    [IDX_PASS_DECL] = ATTR_DECL(s_uuid_password, &s_prop_write),
    [IDX_PASS_VAL]  = ATTR_VALUE_WRITE(s_uuid_password, BLE_WIFI_PASSWORD_MAX_LEN),
    [IDX_PASS_DESC] = ATTR_USER_DESC(s_desc_password),

    [IDX_HOST_DECL] = ATTR_DECL(s_uuid_host, &s_prop_write),
    [IDX_HOST_VAL]  = ATTR_VALUE_WRITE(s_uuid_host, BLE_MQTT_BROKER_MAX_LEN),
    [IDX_HOST_DESC] = ATTR_USER_DESC(s_desc_host),

    [IDX_STATUS_DECL] = ATTR_DECL(s_uuid_status, &s_prop_read_notify),
    [IDX_STATUS_VAL]  = {
        {ESP_GATT_AUTO_RSP},
        {
            ESP_UUID_LEN_128,
            s_uuid_status,
            ESP_GATT_PERM_READ,
            sizeof(s_status_value),
            sizeof(BLE_STATUS_WAITING),
            (uint8_t *)s_status_value,
        },
    },
    [IDX_STATUS_CCCD] = {
        {ESP_GATT_AUTO_RSP},
        {
            ESP_UUID_LEN_16,
            (uint8_t *)&s_uuid_cccd,
            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            sizeof(uint16_t),
            sizeof(s_cccd_value),
            (uint8_t *)&s_cccd_value,
        },
    },
    [IDX_STATUS_DESC] = ATTR_USER_DESC(s_desc_status),

    [IDX_MUSER_DECL] = ATTR_DECL(s_uuid_mqtt_user, &s_prop_write),
    [IDX_MUSER_VAL]  = ATTR_VALUE_WRITE(s_uuid_mqtt_user, BLE_MQTT_USERNAME_MAX_LEN),
    [IDX_MUSER_DESC] = ATTR_USER_DESC(s_desc_mqtt_user),

    [IDX_MPASS_DECL] = ATTR_DECL(s_uuid_mqtt_pass, &s_prop_write),
    [IDX_MPASS_VAL]  = ATTR_VALUE_WRITE(s_uuid_mqtt_pass, BLE_MQTT_PASSWORD_MAX_LEN),
    [IDX_MPASS_DESC] = ATTR_USER_DESC(s_desc_mqtt_pass),

    [IDX_SCHEME_DECL] = ATTR_DECL(s_uuid_scheme, &s_prop_write),
    [IDX_SCHEME_VAL]  = ATTR_VALUE_WRITE(s_uuid_scheme, BLE_MQTT_SCHEME_MAX_LEN),
    [IDX_SCHEME_DESC] = ATTR_USER_DESC(s_desc_scheme),

    [IDX_DEVID_DECL] = ATTR_DECL(s_uuid_device_id, &s_prop_read),
    [IDX_DEVID_VAL]  = ATTR_VALUE_READ(s_uuid_device_id, BLE_DEVICE_ID_LEN),
    [IDX_DEVID_DESC] = ATTR_USER_DESC(s_desc_device_id),
};

static void derive_device_id(void)
{
    /* Match firmware/src/main.cpp::derive_device_id (Arduino branch):
     * "CNU" + low 28 bits of efuse MAC formatted as 7 hex chars uppercase.
     * Total 10 ASCII so the sleep pad UART protocol's fixed-width device_id
     * field fits unchanged. */
    uint8_t mac[6] = {0};
    esp_efuse_mac_get_default(mac);
    uint64_t mac64 =
        ((uint64_t)mac[0]) |
        ((uint64_t)mac[1] << 8)  |
        ((uint64_t)mac[2] << 16) |
        ((uint64_t)mac[3] << 24) |
        ((uint64_t)mac[4] << 32) |
        ((uint64_t)mac[5] << 40);
    snprintf(s_device_id, sizeof(s_device_id), "CNU%07llX",
             (unsigned long long)(mac64 & 0xFFFFFFFULL));
}

static void copy_ble_value(char *dst, size_t dst_size, const uint8_t *src, uint16_t len)
{
    size_t copy_len = len;
    if (copy_len >= dst_size) {
        copy_len = dst_size - 1;
    }
    if (copy_len > 0) {
        memcpy(dst, src, copy_len);
    }
    dst[copy_len] = '\0';
}

static void handle_write_value(uint16_t handle, const uint8_t *value, uint16_t len)
{
    if (handle == s_handle_table[IDX_SSID_VAL]) {
        copy_ble_value(s_config.wifi_ssid, sizeof(s_config.wifi_ssid), value, len);
        if(strstr(s_config.wifi_ssid, "skip") != NULL) {
            ESP_LOGW(TAG, "Skip setup new wifi");
            if (nvs_config_load(&s_config)) {
                const char *device_id = ble_get_device_id();
                ESP_LOGI(TAG, "Resuming with NVS creds (ssid=%s, scheme=%s, host=%s) device_id=%s",
                        s_config.wifi_ssid, s_config.mqtt_scheme, s_config.mqtt_broker, device_id);
                wifi_connect_sta(s_config.wifi_ssid, s_config.wifi_password);
            } 
        }
        else{
        nvs_config_save(NVS_KEY_SSID, s_config.wifi_ssid);
        flag_data_written[0] = true;
        ESP_LOGI(TAG, "Received WiFi SSID: %s", s_config.wifi_ssid);
        }

    } else if (handle == s_handle_table[IDX_PASS_VAL]) {
        copy_ble_value(s_config.wifi_password, sizeof(s_config.wifi_password), value, len);
        nvs_config_save(NVS_KEY_PASS, s_config.wifi_password);
        flag_data_written[1] = true;
        ESP_LOGI(TAG, "Received WiFi password (len=%u)", len);
    } else if (handle == s_handle_table[IDX_HOST_VAL]) {
        copy_ble_value(s_config.mqtt_broker, sizeof(s_config.mqtt_broker), value, len);
        nvs_config_save(NVS_KEY_HOST, s_config.mqtt_broker);
        flag_data_written[2] = true;
        ESP_LOGI(TAG, "Received MQTT broker: %s", s_config.mqtt_broker);
    } else if (handle == s_handle_table[IDX_MUSER_VAL]) {
        copy_ble_value(s_config.mqtt_username, sizeof(s_config.mqtt_username), value, len);
        nvs_config_save(NVS_KEY_USER, s_config.mqtt_username);
        flag_data_written[3] = true;
        ESP_LOGI(TAG, "Received MQTT username (len=%u)", len);
    } else if (handle == s_handle_table[IDX_MPASS_VAL]) {
        copy_ble_value(s_config.mqtt_password, sizeof(s_config.mqtt_password), value, len);
        nvs_config_save(NVS_KEY_MPASS, s_config.mqtt_password);
        flag_data_written[4] = true;
        ESP_LOGI(TAG, "Received MQTT password (len=%u)", len);
    } else if (handle == s_handle_table[IDX_SCHEME_VAL]) {
        copy_ble_value(s_config.mqtt_scheme, sizeof(s_config.mqtt_scheme), value, len);
        nvs_config_save(NVS_KEY_SCHEME, s_config.mqtt_scheme);
        flag_data_written[5] = true;
        ESP_LOGI(TAG, "Received MQTT scheme: %s", s_config.mqtt_scheme);
    } else if (handle == s_handle_table[IDX_STATUS_CCCD]) {
        if (len >= 2) {
            uint16_t v = value[0] | (value[1] << 8);
            s_status_notify_enabled = (v & 0x0001) != 0;
            ESP_LOGI(TAG, "Status CCCD %s", s_status_notify_enabled ? "notify-enabled" : "disabled");
        }
        return;
    }

    if (flag_data_written[0] && flag_data_written[1] && flag_data_written[2] &&
        flag_data_written[3] && flag_data_written[4] && flag_data_written[5]) {
        ESP_LOGI(TAG, "All 6 chars received — starting WiFi+MQTT connect");
        ble_set_status(BLE_STATUS_CONNECTING);
        wifi_connect_sta(s_config.wifi_ssid, s_config.wifi_password);
    }
}

void ble_set_status(const char *status)
{
    if (status == NULL) return;
    size_t n = strlen(status);
    if (n >= sizeof(s_status_value)) n = sizeof(s_status_value) - 1;
    memcpy(s_status_value, status, n);
    s_status_value[n] = '\0';

    if (s_gatts_if == ESP_GATT_IF_NONE || s_handle_table[IDX_STATUS_VAL] == 0) {
        return;
    }
    /* Update the attribute value so READ requests get the latest status. */
    esp_ble_gatts_set_attr_value(s_handle_table[IDX_STATUS_VAL],
                                 (uint16_t)n, (const uint8_t *)s_status_value);

    if (s_client_connected && s_status_notify_enabled) {
        esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id,
                                    s_handle_table[IDX_STATUS_VAL],
                                    (uint16_t)n, (uint8_t *)s_status_value,
                                    false /* need_confirm = false → notify */);
    }
    ESP_LOGI(TAG, "Status → %s", s_status_value);
}

static bool s_adv_data_done = false;
static bool s_scan_rsp_done = false;

static void ble_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        s_adv_data_done = true;
        if (s_scan_rsp_done) esp_ble_gap_start_advertising(&s_adv_params);
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        s_scan_rsp_done = true;
        if (s_adv_data_done) esp_ble_gap_start_advertising(&s_adv_params);
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "BLE advertising started");
        } else {
            ESP_LOGE(TAG, "Failed to start advertising, status=0x%x", param->adv_start_cmpl.status);
        }
        break;
    default:
        break;
    }
}

static void ble_gatts_cb(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                         esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT: {
        s_gatts_if = gatts_if;
        char adv_name[24];
        /* Device name = "SleepPad-" + last 6 chars of device_id (per spec). */
        const char *tail = (strlen(s_device_id) >= 6) ?
            (s_device_id + strlen(s_device_id) - 6) : s_device_id;
        snprintf(adv_name, sizeof(adv_name), "SleepPad-%s", tail);
        esp_ble_gap_set_device_name(adv_name);
        ESP_LOGI(TAG, "Advertising as \"%s\"", adv_name);
        esp_ble_gap_config_adv_data(&s_adv_data);
        esp_ble_gap_config_adv_data(&s_scan_rsp_data);
        esp_ble_gatts_create_attr_tab(s_gatt_db, gatts_if, IDX_NB, GATTS_SERVICE_INSTANCE_ID);
        break;
    }

    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (param->add_attr_tab.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "Create attribute table failed, status=0x%x", param->add_attr_tab.status);
            break;
        }
        if (param->add_attr_tab.num_handle != IDX_NB) {
            ESP_LOGE(TAG, "Unexpected handle count: %d (expected %d)",
                     param->add_attr_tab.num_handle, IDX_NB);
            break;
        }
        memcpy(s_handle_table, param->add_attr_tab.handles, sizeof(s_handle_table));
        esp_ble_gatts_start_service(s_handle_table[IDX_SVC]);

        /* Pre-populate the DeviceId READ characteristic so Flutter can fetch
         * it immediately after GATT connect (before writing provisioning). */
        esp_ble_gatts_set_attr_value(s_handle_table[IDX_DEVID_VAL],
                                     BLE_DEVICE_ID_LEN, (const uint8_t *)s_device_id);
        ESP_LOGI(TAG, "GATT service started — device_id=%s", s_device_id);
        break;

    case ESP_GATTS_CONNECT_EVT:
        s_conn_id = param->connect.conn_id;
        s_client_connected = true;
        s_status_notify_enabled = false;
        ESP_LOGI(TAG, "Client connected, conn_id=%d", s_conn_id);
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        s_client_connected = false;
        s_status_notify_enabled = false;
        ESP_LOGI(TAG, "Client disconnected, restarting advertising");
        esp_ble_gap_start_advertising(&s_adv_params);
        break;

    case ESP_GATTS_WRITE_EVT:
        if (param->write.is_prep) {
            if (param->write.handle != s_prep_write_handle) {
                s_prep_write_len = 0;
                s_prep_write_handle = param->write.handle;
            }
            if ((uint32_t)param->write.offset + param->write.len > sizeof(s_prep_write_buf)) {
                ESP_LOGW(TAG, "Prep write overflow: handle=%d offset=%d len=%d",
                         param->write.handle, param->write.offset, param->write.len);
            } else {
                memcpy(&s_prep_write_buf[param->write.offset], param->write.value, param->write.len);
                uint16_t new_prep_len = param->write.offset + param->write.len;
                s_prep_write_len = (new_prep_len > s_prep_write_len) ? new_prep_len : s_prep_write_len;
            }
        } else {
            handle_write_value(param->write.handle, param->write.value, param->write.len);
        }

        if (param->write.need_rsp && s_gatts_if != ESP_GATT_IF_NONE) {
            esp_gatt_rsp_t rsp;
            memset(&rsp, 0, sizeof(rsp));
            rsp.attr_value.handle = param->write.handle;
            rsp.attr_value.len = 0;
            esp_ble_gatts_send_response(s_gatts_if, param->write.conn_id, param->write.trans_id,
                                        ESP_GATT_OK, &rsp);
        }
        break;

    case ESP_GATTS_EXEC_WRITE_EVT:
        if (param->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC &&
            s_prep_write_handle != 0 && s_prep_write_len > 0) {
            handle_write_value(s_prep_write_handle, s_prep_write_buf, s_prep_write_len);
        }
        s_prep_write_len = 0;
        s_prep_write_handle = 0;
        break;

    default:
        break;
    }
}

void ble_init(void) {
    if (s_ble_started) {
        ESP_LOGI(TAG, "BLE already initialized");
        return;
    }
    for (int i = 0; i < 6; i++) {
        flag_data_written[i] = false;
    }
    static bool s_nvs_done = false;
    static bool s_classic_released = false;

    if (!s_nvs_done) {
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);
        s_nvs_done = true;
    }

    derive_device_id();

    if (!s_classic_released) {
        ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
        s_classic_released = true;
    }

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_ble_gap_register_callback(ble_gap_cb));
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(ble_gatts_cb));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(GATTS_APP_ID));

    s_ble_started = true;
    ESP_LOGI(TAG, "BLE init complete");
}

const ble_config_data_t *ble_get_config(void)
{
    return &s_config;
}

const char *ble_get_device_id(void)
{
    if (s_device_id[0] == '\0') {
        derive_device_id();
    }
    return s_device_id;
}

bool ble_is_active(void)
{
    return s_ble_started;
}

// Dừng BLE - KHÔNG gọi mem_release
void ble_stop(void) {
    if (!ble_is_active()) return;

    esp_ble_gap_stop_advertising();
    vTaskDelay(pdMS_TO_TICKS(200));

    esp_bluedroid_disable();
    esp_bluedroid_deinit();

    esp_bt_controller_disable();
    esp_bt_controller_deinit();  // deinit thay vì mem_release

    s_ble_started = false;
    ESP_LOGI(TAG, "BLE stopped");
}