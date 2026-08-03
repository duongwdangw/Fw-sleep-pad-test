#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <string.h>
#include <esp_err.h>
#include <mqtt_client.h>
#include <esp_crt_bundle.h>
#include <esp_bt.h>
#include <esp_bt_defs.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include "connect_wifi.h"
#include "ble.h"

#include "mqtt_wifi.h"

static const char *TAG = "MQTT_WIFI";
static esp_mqtt_client_handle_t client = NULL;
bool mqtt_connected = false;
/* True once we've reached MQTT_EVENT_CONNECTED at least once. After that
 * transient errors are normal (auto-reconnect handles it) and we don't
 * spam the BLE client with mqtt_failed. */
static bool s_mqtt_ever_connected = false;

// ISRG Root X1 — Let's Encrypt's RSA root, valid until 2035-06-04.
// We pin this single root cert instead of attaching the Mozilla CA bundle
// because the bundle attach path is broken on some ESP-IDF versions.
static const char* ISRG_ROOT_X1_PEM =
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "-----END CERTIFICATE-----\n";

static esp_err_t mqtt_subscribe(esp_mqtt_client_handle_t client_id, const char *topic, int qos)
{
    if (client_id == NULL || topic == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    int msg_id = esp_mqtt_client_subscribe_single(client_id, topic, qos);
    ESP_LOGI(TAG, "Subscribed to topic %s with message ID: %d", topic, msg_id);
    return (msg_id == -1) ? ESP_FAIL : ESP_OK;
}

static esp_err_t mqtt_publish(esp_mqtt_client_handle_t client_id, const char *topic, const char *data, int len, int qos, int retain)
{
    if (client_id == NULL || topic == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    int msg_id = esp_mqtt_client_publish(client_id, topic, data, len, qos, retain);
    ESP_LOGI(TAG, "Published message to %s with ID: %d", topic, msg_id);
    return (msg_id == -1) ? ESP_FAIL : ESP_OK;
}

esp_err_t mqtt_publish_data(const char *topic, const char *data)
{
    if (client == NULL) {
        ESP_LOGE(TAG, "MQTT client is not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (topic == NULL || data == NULL) {
        ESP_LOGE(TAG, "MQTT publish arguments invalid");
        return ESP_ERR_INVALID_ARG;
    }
    return mqtt_publish(client, topic, data, strlen(data), 1, 0);
}

esp_err_t mqtt_stop(void)
{
    if (client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = esp_mqtt_client_stop(client);
    esp_mqtt_client_destroy(client);
    client = NULL;
    return err;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t event_client = event->client;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED: {
        mqtt_connected = true;
        s_mqtt_ever_connected = true;
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

        /* 1. Notify Flutter pairing app that provisioning is complete.
         *    Flutter waits up to 60s for Status="connected" via BLE notify;
         *    must fly out BEFORE we tear down the BLE stack below. */
        ble_set_status(BLE_STATUS_CONNECTED);

        /* 2. Publish retained "online" to sleeppad/{device_id}/status so the
         *    backend marks this device online (Flutter then polls /devices/mine
         *    until online=true, then exits the pairing screen). */
        char topic[96];
        snprintf(topic, sizeof(topic), "sleeppad/%s/status", ble_get_device_id());
        mqtt_publish(event_client, topic, "online", 6 /* len */, 1 /* qos */, 1 /* retain */);

        /* 3. Give iOS a moment to receive the BLE notify before we tear down
         *    the radio. ~800ms is enough for the notify packet + connection
         *    update to land on the central. */
        if (ble_is_active()) {
            vTaskDelay(pdMS_TO_TICKS(800));
            esp_err_t err = esp_ble_gap_stop_advertising();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "esp_ble_gap_stop_advertising() returned %s", esp_err_to_name(err));
            }
            esp_bluedroid_disable();
            esp_bt_controller_disable();
            esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
        }

        /* Subscribe to the device's cmd topic so the backend can deliver
         * remote commands (currently only `factory_reset`). QoS 1 so a
         * retained cmd published while we were offline is delivered on
         * reconnect. Per docs/protocols/ble-provisioning-and-mqtt-auth.md
         * the broker ACL only allows %c (= device_id) under sleeppad/. */
        char cmd_topic[96];
        snprintf(cmd_topic, sizeof(cmd_topic), "sleeppad/%s/cmd", ble_get_device_id());
        mqtt_subscribe(event_client, cmd_topic, 1);
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        mqtt_connected = false;
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA: {
        ESP_LOGI(TAG, "MQTT_EVENT_DATA topic=%.*s data=%.*s",
                 event->topic_len, event->topic, event->data_len, event->data);

        /* Remote `factory_reset` on sleeppad/{device_id}/cmd — sent by backend
         * when user removes the device from their account. We must:
         *   1. Clear the retained cmd, otherwise a re-paired device with the
         *      same MAC-derived device_id would loop-reset on every boot.
         *   2. Wipe NVS so wifi/mqtt creds are forgotten.
         *   3. Restart — boot path with empty NVS goes back to BLE pairing.
         */
        char cmd_topic[96];
        snprintf(cmd_topic, sizeof(cmd_topic), "sleeppad/%s/cmd", ble_get_device_id());
        int cmd_topic_len = (int)strlen(cmd_topic);
        const char fr[] = "factory_reset";
        const int fr_len = (int)sizeof(fr) - 1;

        if (event->topic_len == cmd_topic_len &&
            strncmp(event->topic, cmd_topic, cmd_topic_len) == 0 &&
            event->data_len == fr_len &&
            strncmp(event->data, fr, fr_len) == 0) {
            ESP_LOGW(TAG, "factory_reset received — clearing retained cmd, wiping NVS, restarting");
            esp_mqtt_client_publish(event_client, cmd_topic, "", 0, 1, 1);
            vTaskDelay(pdMS_TO_TICKS(500));
            nvs_flash_erase();
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_restart();
        }
        break;
    }
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "MQTT_EVENT_ERROR (ever_connected=%d)", s_mqtt_ever_connected);
        if (!s_mqtt_ever_connected) {
            /* First-time connect failed — surface to the BLE-paired app so
             * Flutter shows "MQTT failed" instead of waiting 60s for a
             * generic timeout. Most common causes: wrong host/scheme, broker
             * down, ACL rejecting our client_id, TLS handshake failure
             * (cert/SNI mismatch or stale clock). */
            ble_set_status(BLE_STATUS_MQTT_FAILED);
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

void mqtt_init(const char *mqtt_address, const char *client_id, const char *username, const char *password)
{
    ESP_LOGI(TAG, "MQTT configuration function called");
    ESP_LOGI(TAG, "Broker URI: %s", mqtt_address ? mqtt_address : "NULL");
    ESP_LOGI(TAG, "Client ID: %s", client_id ? client_id : "NULL");
    ESP_LOGI(TAG, "Username: %s", username && strlen(username) > 0 ? username : "(anonymous)");
    
    if (mqtt_address == NULL || client_id == NULL) {
        ESP_LOGE(TAG, "MQTT init parameters invalid");
        return;
    }

    if (client != NULL) {
        ESP_LOGW(TAG, "MQTT client already initialized. Restarting client.");
        esp_mqtt_client_stop(client);
        esp_mqtt_client_destroy(client);
        client = NULL;
    }

    // ESP-IDF 5.x configuration with TLS verification
    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = mqtt_address;
    // mqtt_cfg.broker.verification.certificate = ISRG_ROOT_X1_PEM;
    /* Best practice would be esp_crt_bundle_attach with the Mozilla bundle
     * (resilient to Let's Encrypt X1↔X2 rotation), but the full bundle adds
     * ~150 KB and we're already at ~3% free on the 1.5 MB app partition.
     * Keep the single ISRG Root X1 pin; revisit when partition is enlarged. */
    if(strstr(mqtt_address, "mqtts://") == mqtt_address || strstr(mqtt_address, "wss://") == mqtt_address) {
        mqtt_cfg.broker.verification.certificate = ISRG_ROOT_X1_PEM;
    }
    else {
        ESP_LOGW(TAG, "MQTT broker URI does not use TLS — skipping certificate attach");
    }
    mqtt_cfg.credentials.client_id = client_id;
    if (username != NULL && strlen(username) > 0) {
        mqtt_cfg.credentials.username = username;
        mqtt_cfg.credentials.authentication.password = password;
    }
    /* Tuning notes (best-practice for cold MQTT WSS over Cloudflare):
     *   - timeout_ms 30s: TLS handshake on cold links (DNS warm-up + TCP
     *     SYN retransmit + cert chain ~3 KB) routinely takes 10-25 s on
     *     poor signal; 15 s default trips on first try, then auto-reconnect
     *     succeeds — wasteful and confusing.
     *   - reconnect_timeout_ms 3s: ESP-MQTT default is 10 s; we drop it so
     *     a transient dropout recovers within a few seconds (UX matters
     *     for the pairing flow, not just steady-state telemetry).
     *   - keepalive 30s: half the default 60 s so the broker flips us
     *     offline within ~45 s of a hard power loss instead of ~90 s. */
    mqtt_cfg.session.keepalive            = 30;
    mqtt_cfg.network.timeout_ms           = 30000;
    mqtt_cfg.network.reconnect_timeout_ms = 3000;
    mqtt_cfg.network.disable_auto_reconnect = false;

    /* Last Will and Testament — broker auto-publishes when our connection
     * drops without a clean disconnect. Backend's MQTT subscriber watches
     * this topic to flip device online→offline in Redis (last_seen). The
     * static buffer must outlive mqtt_init() because esp-mqtt holds the
     * pointer for the lifetime of the client. */
    static char s_lwt_topic[96];
    snprintf(s_lwt_topic, sizeof(s_lwt_topic), "sleeppad/%s/status", client_id);
    mqtt_cfg.session.last_will.topic    = s_lwt_topic;
    mqtt_cfg.session.last_will.msg      = "offline";
    mqtt_cfg.session.last_will.msg_len  = 7;
    mqtt_cfg.session.last_will.qos      = 0;
    mqtt_cfg.session.last_will.retain   = 1;

    client = esp_mqtt_client_init(&mqtt_cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return;
    }

    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_err_t err = esp_mqtt_client_start(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_start failed: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(client);
        client = NULL;
    }
}
