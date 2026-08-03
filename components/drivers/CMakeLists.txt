idf_component_register(SRCS "drivers.c"
                       INCLUDE_DIRS "include"  
                       REQUIRES 
                        driver
                        ble
                        config_parameter)