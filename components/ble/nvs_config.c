#include <string.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>

#include "nvs_config.h"

static const char *TAG = "NVS_CFG";

bool nvs_config_save(const char *key, const char *value)
{
    if (key == NULL || value == NULL) {
        return false;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return false;
    }
    err = nvs_set_str(h, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str(%s) failed: %s", key, esp_err_to_name(err));
        return false;
    }
    return true;
}

/* Load a string key into dst (NUL-terminated). On miss leaves dst[0]='\0'. */
static void load_str(nvs_handle_t h, const char *key, char *dst, size_t dst_size)
{
    size_t len = dst_size;
    esp_err_t err = nvs_get_str(h, key, dst, &len);
    if (err != ESP_OK) {
        dst[0] = '\0';
    }
}

bool nvs_config_load(ble_config_data_t *out)
{
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        /* Namespace doesn't exist yet (fresh flash) — not an error. */
        return false;
    }

    load_str(h, NVS_KEY_SSID,   out->wifi_ssid,     sizeof(out->wifi_ssid));
    load_str(h, NVS_KEY_PASS,   out->wifi_password, sizeof(out->wifi_password));
    load_str(h, NVS_KEY_HOST,   out->mqtt_broker,   sizeof(out->mqtt_broker));
    load_str(h, NVS_KEY_SCHEME, out->mqtt_scheme,   sizeof(out->mqtt_scheme));
    load_str(h, NVS_KEY_USER,   out->mqtt_username, sizeof(out->mqtt_username));
    load_str(h, NVS_KEY_MPASS,  out->mqtt_password, sizeof(out->mqtt_password));

    nvs_close(h);

    /* SSID is the canary — without WiFi creds we can't connect. */
    bool ok = out->wifi_ssid[0] != '\0';
    ESP_LOGI(TAG, "NVS load %s ssid=\"%s\"", ok ? "hit" : "miss", out->wifi_ssid);
    return ok;
}
