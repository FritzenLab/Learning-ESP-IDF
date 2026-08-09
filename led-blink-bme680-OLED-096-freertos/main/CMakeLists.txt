idf_component_register(SRCS "main.c" "ssd1306_mini.c"
                        INCLUDE_DIRS "."
                        REQUIRES driver esp_driver_gpio bme680 i2cdev)
