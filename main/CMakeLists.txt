idf_component_register(
    SRCS "main.c" "process_manager.c"
    INCLUDE_DIRS "."
    REQUIRES
        sleep_pad
        drivers
        config_parameter
        ble
        connect_wifi
        mqtt_wifi
        config_gpio
    PRIV_REQUIRES
        nvs_flash
        json
)