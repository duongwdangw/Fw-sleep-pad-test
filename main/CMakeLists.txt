idf_component_register(SRCS "process_manager.c" "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES config_parameter
                             drivers
                             
                             sleep_pad)
