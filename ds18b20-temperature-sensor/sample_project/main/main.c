#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "onewire_bus.h"
#include "ds18b20.h"

#define LED_GPIO        GPIO_NUM_1
#define ONEWIRE_GPIO    GPIO_NUM_16
#define LED_PERIOD_MS   300
#define TEMP_PERIOD_MS  1000

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

// Reads the DS18B20 every TEMP_PERIOD_MS over an RMT-backed 1-Wire bus.
// Component docs / API reference:
// https://github.com/espressif/idf-extra-components/tree/master/onewire_bus
// https://github.com/espressif/idf-extra-components/tree/master/ds18b20
static void ds18b20_task(void *arg)
{
    onewire_bus_handle_t bus = NULL;
    onewire_bus_config_t bus_config = {
        .bus_gpio_num = ONEWIRE_GPIO,
    };
    onewire_bus_rmt_config_t rmt_config = {
        .max_rx_bytes = 10, // enough for a single ROM + scratchpad exchange
    };
    ESP_ERROR_CHECK(onewire_new_bus_rmt(&bus_config, &rmt_config, &bus));

    ds18b20_device_handle_t ds18b20 = NULL;
    onewire_device_iter_handle_t iter = NULL;
    onewire_device_t device;

    // Enumerate devices on the bus and grab the first DS18B20 found
    ESP_ERROR_CHECK(onewire_new_device_iter(bus, &iter));
    if (onewire_device_iter_get_next(iter, &device) == ESP_OK) {
        ds18b20_config_t ds_cfg = {};
        ESP_ERROR_CHECK(ds18b20_new_device(&device, &ds_cfg, &ds18b20));
        ESP_LOGI(TAG, "DS18B20 found, address: %016llX", device.address);
    } else {
        ESP_LOGE(TAG, "No DS18B20 found on GPIO %d", ONEWIRE_GPIO);
    }
    onewire_del_device_iter(iter);

    TickType_t last_wake = xTaskGetTickCount();
    while (1) {
        if (ds18b20) {
            // Note: this call blocks ~750ms internally waiting for the sensor's
            // own conversion time, but only within THIS task — the LED task
            // keeps blinking on schedule because FreeRTOS preempts between them.
            ESP_ERROR_CHECK(ds18b20_trigger_temperature_conversion(ds18b20));
            float temperature = 0;
            ESP_ERROR_CHECK(ds18b20_get_temperature(ds18b20, &temperature));
            ESP_LOGI(TAG, "Temperature: %.2f C", temperature);
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TEMP_PERIOD_MS));
    }
}

void app_main(void)
{
    // Two independent tasks on the FreeRTOS scheduler — neither task's
    // delay blocks the other.
    xTaskCreate(led_blink_task, "led_blink_task", 2048, NULL, 5, NULL);
    xTaskCreate(ds18b20_task, "ds18b20_task", 4096, NULL, 5, NULL);
}