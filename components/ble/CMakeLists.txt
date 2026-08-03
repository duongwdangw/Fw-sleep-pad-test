idf_component_register(SRCS "ble.c" "nvs_config.c"
                    INCLUDE_DIRS "include"
                    REQUIRES
                        bt
                        nvs_flash
                        connect_wifi
                        )