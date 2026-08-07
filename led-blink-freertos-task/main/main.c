#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"


#define LED_GPIO        GPIO_NUM_1

#define LED_PERIOD_MS   100

static const char *TAG = "APP";

// Toggles the LED every LED_PERIOD_MS. Runs as its own FreeRTOS task so it
// is never blocked by the DS18B20 conversion delay in the other task.
// vTaskDelayUntil keeps a stable period regardless of loop execution time:
// https://www.freertos.org/vtaskdelayuntil.html
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
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(LED_PERIOD_MS));
    }
}


void app_main(void)
{
    // unblocking FreeRTOS task
    xTaskCreate(led_blink_task, "led_blink_task", 2048, NULL, 5, NULL);
    
}