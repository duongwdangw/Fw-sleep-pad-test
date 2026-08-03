#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <ctype.h>
#include <inttypes.h>
#include <esp_log.h>    

#include <time.h>
#include <sys/time.h>
#include <esp_sntp.h>

#include "ble.h"
#include "connect_wifi.h"
#include "mqtt_wifi.h"

/* Wait until SNTP returns a real wall-clock (> 2023-11-14). Required before
 * any TLS handshake or the cert validity window check rejects everything as
 * "not-yet-valid" (RTC starts at 1970 → mbedtls error 0x4290).
 *
 * `timeout_ms` is forgiving — Wi-Fi has typically been up for a couple of
 * seconds by the time we get here, but DNS + UDP roundtrip to public NTP
 * pools can still take 2-5 s on slow links. Returns true if synced. */
static bool time_sync_wait(int timeout_ms)
{
    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.cloudflare.com");
    esp_sntp_setservername(2, "time.google.com");
    esp_sntp_init();

    const int step_ms = 200;
    int waited = 0;
    while (waited < timeout_ms) {
        time_t now = time(NULL);
        if (now > 1700000000) {
            ESP_LOGI("WIFI", "SNTP synced epoch=%lld", (long long)now);
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(step_ms));
        waited += step_ms;
    }
    ESP_LOGW("WIFI", "SNTP sync timeout — TLS may reject certs (clock=%lld)", (long long)time(NULL));
    return false;
}

/* Build the esp-mqtt URI from spec scheme + host. The PlatformIO firmware's
 * build_uri (firmware/src/mqtt-client.cpp) is the reference. Spec section 1
 * defines four schemes:
 *   - wss   → prod TLS via cloudflared, path /mqtt, default :443
 *   - ws    → dev WebSocket, default :9001 (caller-supplied port suffix)
 *   - mqtts → TLS over plain TCP
 *   - tcp   → plain MQTT, default :1883 if no port in host
 * Host may already contain `:port` (e.g. `192.168.1.100:9001`); we don't
 * append a default port when one is present. */
static void build_mqtt_uri(char *out, size_t out_size, const char *scheme, const char *host)
{
    if (strcmp(scheme, "wss") == 0) {
        snprintf(out, out_size, "wss://%s/mqtt", host);
    } else if (strcmp(scheme, "ws") == 0) {
        snprintf(out, out_size, "ws://%s/mqtt", host);
    } else if (strcmp(scheme, "mqtts") == 0) {
        snprintf(out, out_size, "mqtts://%s", host);
    } else if (strchr(host, ':') != NULL) {
        snprintf(out, out_size, "mqtt://%s", host);
    } else {
        snprintf(out, out_size, "mqtt://%s:1883", host);
    }
}

static EventGroupHandle_t s_wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static bool s_connected = false;
static int s_retry_num = 0;
static bool got_ip = false;
/* Counts consecutive STA_DISCONNECTED events that occurred without ever
 * reaching IP_GOT_IP. Resets on first successful IP. After WIFI_FAIL_THRESHOLD
 * we tell the BLE-paired app the SSID/password are wrong rather than letting
 * Flutter's 60s pairing watchdog time out with no specific cause. */
static int s_wifi_fail_count = 0;
#define WIFI_FAIL_THRESHOLD 5
static const char *TAG = "WIFI";

static char buffer[512];

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_CONNECTED:
            printf("connected to wifi successful\r\n");
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG, "WiFi disconnected");
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            if (!got_ip) {
                /* Pairing-phase failure — try a few more times then surface to
                 * the app over BLE. Auto-retry is on by default in IDF v5.x,
                 * but we still call esp_wifi_connect() defensively. */
                if (++s_wifi_fail_count >= WIFI_FAIL_THRESHOLD) {
                    ESP_LOGW(TAG, "WiFi failed %d times — notifying app wifi_failed", s_wifi_fail_count);
                    ble_set_status(BLE_STATUS_WIFI_FAILED);
                    s_wifi_fail_count = 0; /* don't spam if user keeps retrying */
                } else {
                    esp_wifi_connect();
                }
            }

            wifi_mode_t mode;
            esp_wifi_get_mode(&mode);

            if (mode == WIFI_MODE_AP)
            {
                ESP_LOGI("WIFI", "Đang ở chế độ Access Point");
                // wifi_state = 2;
            }
            else if (mode == WIFI_MODE_STA)
            {
                ESP_LOGI("WIFI", "Đang ở chế độ Station");
                // reopen_network();
            }
            else if (mode == WIFI_MODE_APSTA)
            {
                ESP_LOGI("WIFI", "Đang ở chế độ AP + STA ");
                // wifi_state = 2;
            }
            else
            {
                ESP_LOGI("WIFI", "Wi-Fi đang tắt hoặc không xác định");
            }
            break;
        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "User access to AP");
            // act_handle = true;
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "Out from setup wifi mode");
            // act_handle = false;
            break;
        default:
            break;
        }
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) // internet protocol (IP)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        got_ip = true;
        s_wifi_fail_count = 0;
        ESP_LOGI(TAG, "Got IP address");
        s_connected = true;
        s_retry_num = 0;
        
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        // Disable access point mode after successful station connection
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

        /* Sync clock before MQTT — TLS cert verify needs a real wall-clock,
         * otherwise mbedtls errors with 0x4290 ("cert not yet valid") because
         * the device boots with epoch=0 (1970). 10 s is enough on most links;
         * on failure we still try MQTT — esp-mqtt's TLS layer is less strict
         * about clock than full mbedtls, and the connect may still succeed. */
        time_sync_wait(10000);

        ESP_LOGI(TAG, "Connect to WiFi successful > connect MQTT");
        build_mqtt_uri(buffer, sizeof(buffer), s_config.mqtt_scheme, s_config.mqtt_broker);
        ESP_LOGI(TAG, "MQTT URI: %s", buffer);
        /* MQTT client_id MUST equal device_id — Mosquitto ACL enforces
         * write sleeppad/%c/# so a mismatch silently drops publishes. */
        mqtt_init(buffer, ble_get_device_id(), s_config.mqtt_username, s_config.mqtt_password);
    }
}

void wifi_connect_sta(const char *ssid, const char *pass) // thử kết nối WiFi ở chế độ STA  (station: truy cập vào wifi của người dùng)
{
    wifi_config_t sta_conf = {0};
    strncpy((char *)sta_conf.sta.ssid, ssid, sizeof(sta_conf.sta.ssid) - 1);
    strncpy((char *)sta_conf.sta.password, pass, sizeof(sta_conf.sta.password) - 1);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    esp_wifi_disconnect();
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_conf));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "esp_wifi_connect failed: %d", err);
    }
    else
    {
        ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);
    }
}

void wifi_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL, &instance_got_ip));
}