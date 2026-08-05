# Component registration reference:
# https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-guides/build-system.html#cmakelists-txt-example
idf_component_register(SRCS "potentiometer-to-pwm.c"
                        INCLUDE_DIRS "."
                        REQUIRES esp_adc esp_driver_ledc esp_driver_gpio driver)