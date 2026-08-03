idf_component_register(SRCS "config_gpio.c"
                    INCLUDE_DIRS include
                    REQUIRES
                        driver
                        nvs_flash
                        
                        config_parameter
                        mqtt_wifi
                        ble
                        connect_wifi
                        drivers
                        )