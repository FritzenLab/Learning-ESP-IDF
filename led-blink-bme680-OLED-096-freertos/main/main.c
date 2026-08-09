/*
This code does a couple of things:
- Xiao ESP32-C6 blinks an LED on pin GPIO 2,
- Reads a BME680 temperature, humidity and atmospheric pressure sensor,
- Shows on an 0.96" OLED display: all readings from BME680 and also FritzenLab logo.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "i2cdev.h"       // i2cdev_init() - https://esp-idf-lib.readthedocs.io/en/latest/groups/i2cdev.html
#include "bme680.h"       // esp-idf-lib bme680 driver - https://esp-idf-lib.readthedocs.io/en/latest/groups/bme680.html
#include "ssd1306_mini.h" // our own minimal driver, shares the i2cdev bus with bme680
#include "fritzenlab_logo.h"

#define LED_GPIO        GPIO_NUM_0

#define LED_PERIOD_MS   200
#define I2C_PORT        I2C_NUM_0
#define I2C_SDA_GPIO    GPIO_NUM_22
#define I2C_SCL_GPIO    GPIO_NUM_23

#define BME680_ADDR     BME680_I2C_ADDR_1   // 0x76; use BME680_I2C_ADDR_1 (0x77) if your SDO pin is pulled high
#define SSD1306_ADDR    0x3C                // most 0.96" modules; try 0x3D if the screen stays blank

static const char *TAG = "APP";
static const char* TEMP = "Temperature";
static const char* HUM = "Humidity";
static const char* HPA = "Pressure";

// Toggles the LED every LED_PERIOD_MS. Runs as its own FreeRTOS task so it
// is never blocked by the BME680 + OLED in the other task.
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
// Reads BME680 (temperature/humidity/pressure) and shows it on the SSD1306,
// one large (2x-scaled) line per value. Runs as its own task so a slow
// TPHG measurement cycle never blocks the LED blink task.
static void bme680_display_task(void *arg)
{
    bme680_t sensor;
    memset(&sensor, 0, sizeof(sensor));
    ESP_ERROR_CHECK(bme680_init_desc(&sensor, BME680_ADDR, I2C_PORT, I2C_SDA_GPIO, I2C_SCL_GPIO));
    ESP_ERROR_CHECK(bme680_init_sensor(&sensor));

    // Reasonable default oversampling / filtering; see:
    // https://esp-idf-lib.readthedocs.io/en/latest/groups/bme680.html
    // args: temperature OSR, pressure OSR, humidity OSR - all enabled this time
    // https://esp-idf-lib.readthedocs.io/en/latest/groups/bme680.html
    bme680_set_oversampling_rates(&sensor, BME680_OSR_4X, BME680_OSR_4X, BME680_OSR_2X);
    bme680_set_filter_size(&sensor, BME680_IIR_SIZE_7);
    bme680_set_heater_profile(&sensor, 0, 200, 100);
    bme680_use_heater_profile(&sensor, 0);
    bme680_set_ambient_temperature(&sensor, 25);

    uint32_t duration;
    bme680_get_measurement_duration(&sensor, &duration);

    ssd1306_mini_t oled;
    ESP_ERROR_CHECK(ssd1306_mini_init(&oled, I2C_PORT, I2C_SDA_GPIO, I2C_SCL_GPIO, SSD1306_ADDR));

    bme680_values_float_t values;
    
    while (1) {
        if (bme680_force_measurement(&sensor) == ESP_OK) {
            vTaskDelay(duration); // passive wait for the TPHG cycle to finish

            if (bme680_get_results_float(&sensor, &values) == ESP_OK) {
                char line1[16], line2[16], line3[16];
                snprintf(line1, sizeof(line1), "%.1f C", values.temperature);
                snprintf(line2, sizeof(line2), "%.1f %%", values.humidity);
                snprintf(line3, sizeof(line3), "%.0fhPa", values.pressure);

                ESP_LOGI(TEMP, "Temperature: %.1fC", values.temperature);
                ESP_LOGI(HUM, "Humidity: %.1f%%", values.humidity);
                ESP_LOGI(HPA, "Atm. Pressure: %.1fhpa", values.pressure);

                ssd1306_mini_clear(&oled);
                ssd1306_mini_draw_text_2x(&oled, 0, 0,  line1);
                ssd1306_mini_draw_text_2x(&oled, 0, 20, line2);
                ssd1306_mini_draw_text_2x(&oled, 0, 40, line3);
                ssd1306_mini_flush(&oled);
            } else {
                ESP_LOGW(TAG, "BME680 read failed");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(4000)); // hold the readings on screen for 3s

        ssd1306_mini_draw_bitmap(&oled, fritzenlab_logo_128x64);
        ssd1306_mini_flush(&oled);
        vTaskDelay(pdMS_TO_TICKS(2000)); // then hold the logo for 3s
    }
    
}

void app_main(void)
{
    // unblocking FreeRTOS task
    xTaskCreate(led_blink_task, "led_blink_task", 2048, NULL, 5, NULL);
    ESP_ERROR_CHECK(i2cdev_init()); // must run once before any i2cdev-based device is initialized
    xTaskCreate(bme680_display_task, "bme680_display_task", 4096, NULL, 5, NULL);
}