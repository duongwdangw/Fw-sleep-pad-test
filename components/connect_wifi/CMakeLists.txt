idf_component_register(SRCS "connect_wifi.c"
                    INCLUDE_DIRS "include"
                    REQUIRES
                        esp_wifi
                        esp_event
                        esp_netif
                        lwip
                        nvs_flash
                        ble
                        mqtt_wifi
                        )