// Xiao ESP32-C6 pinout https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "bootloader_random.h"
#include "esp_random.h"
#include <inttypes.h>

#define LED_GPIO        GPIO_NUM_1
uint32_t LED_PERIOD_MS = 0;

static const char *TAG = "RANDOM_NUMBERS";

#define RAND_MIN 50
#define RAND_MAX_INCL 1000

uint32_t get_random_in_range(void)
{
    uint32_t range = RAND_MAX_INCL - RAND_MIN + 1; // 951 possible values

    uint32_t r = esp_random();
    uint32_t limit = UINT32_MAX - (UINT32_MAX % range);
    while (r >= limit) {
        r = esp_random();
    }
    //bootloader_random_disable();
    return RAND_MIN + (r % range);
}

static void led_blink_task(void *arg)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);

    bool level = false;
    TickType_t last_wake = xTaskGetTickCount();
    while (1) {
        level = !level;
        gpio_set_level(LED_GPIO, level);
        LED_PERIOD_MS= get_random_in_range(); // Get a random number of milisseconds
        ESP_LOGI(TAG, "Delay: %" PRIu32, LED_PERIOD_MS);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(LED_PERIOD_MS));
    }
}

void app_main(void)
{
    // Only needed if Wi-Fi/BT/Thread/Zigbee are NOT active.
    // Must be paired with bootloader_random_disable() before using
    // ADC or any RF subsystem afterward.
    bootloader_random_enable();
    // create the blinking task
    xTaskCreate(led_blink_task, "led_blink_task", 2048, NULL, 5, NULL);
}