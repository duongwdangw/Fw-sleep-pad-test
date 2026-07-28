idf_component_register(
    SRCS "main.c" "process_manager.c"
    INCLUDE_DIRS "."
    REQUIRES
        sleep_pad
        drivers
        config_parameter
    PRIV_REQUIRES
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        mqtt
        json
        driver
)