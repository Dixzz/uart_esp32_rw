idf_component_register(SRCS "uart_bootloader_main.c"
                    PRIV_REQUIRES nvs_flash esp_netif mqtt
                    REQUIRES esp_driver_uart esp_driver_gpio json
                    INCLUDE_DIRS ".")
