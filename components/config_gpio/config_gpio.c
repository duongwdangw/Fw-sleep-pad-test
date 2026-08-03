#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>

#include "config_gpio.h"
#include "config_parameter.h"
#include "hal/gpio_types.h"
#include "mqtt_wifi.h"
#include "ble.h"
#include "nvs_config.h"
#include "connect_wifi.h"
#include "drivers.h"

static const char *TAG = "CONFIG_GPIO";

static esp_timer_handle_t stop_ble_timer_handler;

void config_gpio_out_init(void) {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE, // Disable interrupt
        .mode = GPIO_MODE_OUTPUT,       // Set as output mode
        .pin_bit_mask = (1ULL << WIFI_STATUS_LED_GPIO), // Configure GPIO2
        .pull_down_en = 0,              // Disable pull-down
        .pull_up_en = 0                 // Disable pull-up
    };
    gpio_config(&io_conf);
    
    ESP_LOGI("CONFIG_GPIO", "GPIO output initialized");
}

void config_gpio_in_init(void) {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE, // Disable interrupt
        .mode = GPIO_MODE_INPUT,        // Set as input mode
        .pin_bit_mask = (1ULL << BUTTON_SETUP_WIFI_MODE), // Configure GPIO4
        .pull_down_en = 0,              // Disable pull-down
        .pull_up_en = GPIO_PULLUP_ENABLE                  // Disable pull-up
    };
    gpio_config(&io_conf);
    
    ESP_LOGI("CONFIG_GPIO", "GPIO input initialized");
}

// void stop_ble_timer_callback(){
//     ESP_LOGI("CONFIG_GPIO", "Stopping BLE timer");
//     ble_stop();
// }

// void start_stop_ble_timer(){
//     if(stop_ble_timer_handler != NULL){
//         esp_timer_stop(stop_ble_timer_handler);
//         esp_timer_start_once(stop_ble_timer_handler, 300ULL * 1000000); // 300 seconds
//         return;
//     }
//     esp_timer_create_args_t stop_ble_timer_args = {
//         .callback = &stop_ble_timer_callback,
//         .arg = NULL,
//         .name = "stop_ble_timer"
//     };
//     esp_timer_create(&stop_ble_timer_args, &stop_ble_timer_handler);
//     esp_timer_start_once(stop_ble_timer_handler, 300ULL * 1000000); // 300 seconds
//     ESP_LOGI(TAG, "BLE timer started - Ble ");
// }

void stop_ble_timer(void){
    if(stop_ble_timer_handler != NULL){
        esp_timer_stop(stop_ble_timer_handler);
        esp_timer_delete(stop_ble_timer_handler);   
        // esp_timer_destroy(stop_ble_timer_handler);
        stop_ble_timer_handler = NULL;
        ESP_LOGI(TAG, "BLE timer will be stopped");
    }
    else {
        ESP_LOGI(TAG, "BLE timer is not initialized");
    }
}


void detect_mqtt_task(void *pvParameters){
    config_gpio_out_init();
    config_gpio_in_init();
    /* Retry WiFi connect at a low rate when MQTT is offline.
     * NOTE: esp_wifi_connect()/disconnect() are heavy; do NOT call every loop. */
    int count_to_connect_wifi = 0;
    while (1) {
        if(gpio_get_level(BUTTON_SETUP_WIFI_MODE) == 0) {
            while (gpio_get_level(BUTTON_SETUP_WIFI_MODE)==0)
            {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            ESP_LOGI("CONFIG_GPIO", "Button pressed - starting BLE provisioning");
            ble_init();
            start_stop_ble_timer();
        }
        if(mqtt_connected) {
            gpio_set_level(WIFI_STATUS_LED_GPIO, 1); // Turn on LED
        }
        else{
            gpio_set_level(WIFI_STATUS_LED_GPIO, 0); 
        }
        if (!got_ip) {
           // Turn off LED
            /* While BLE provisioning is active, avoid forced WiFi reconnect loops.
             * BLE and WiFi coexist, but reconnecting WiFi repeatedly can starve BLE
             * and make the whole system look "stuck". */
            if (!ble_is_active()) {
                /* 50 * 500ms = 25s backoff between connect attempts */
                if (count_to_connect_wifi == 0) {
                    if (nvs_config_load(&s_config)) {
                        ESP_LOGI(TAG, "Resuming with NVS creds (ssid=%s, scheme=%s, host=%s) device_id=%s",
                                s_config.wifi_ssid, s_config.mqtt_scheme, s_config.mqtt_broker, ble_get_device_id());
                        wifi_connect_sta(s_config.wifi_ssid, s_config.wifi_password);
                    }
                    count_to_connect_wifi = 1;
                } else {
                    count_to_connect_wifi++;
                    if (count_to_connect_wifi >= 50){
                        count_to_connect_wifi = 0;
                    }
                }
            } else {
                /* Hold off reconnect attempts until BLE auto-stops. */
                count_to_connect_wifi = 1;
            }
        }
        else{
            count_to_connect_wifi = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(500)); 
    }
}