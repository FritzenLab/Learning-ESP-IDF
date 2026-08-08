idf_component_register(SRCS "main.c"
                        INCLUDE_DIRS "."
                        REQUIRES driver esp_driver_gpio bootloader_support esp_wifi esp_event esp_netif nvs_flash esp_http_server esp_adc)
