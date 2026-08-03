idf_component_register(SRCS "mqtt_wifi.c"
                    INCLUDE_DIRS "include"
                    REQUIRES
                        bt
                        nvs_flash
                        connect_wifi
                        ble

                        mqtt)