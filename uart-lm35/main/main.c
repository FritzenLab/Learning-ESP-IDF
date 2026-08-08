#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "driver/gpio.h"

#define UART_PORT      UART_NUM_1
#define UART_TX_PIN    16
#define UART_RX_PIN    17
#define UART_BAUD_RATE 9600
#define RX_BUF_SIZE    1024

#define LED_GPIO        GPIO_NUM_1
#define LED_PERIOD_MS   100

static const char *TAG = "LM35_RX";

static void uart_rx_task(void *arg)
{
    // Temp buffer for raw bytes pulled off the UART driver's ring buffer
    uint8_t data[RX_BUF_SIZE];
    // Accumulates bytes until we see a '\n', since println() sends line-delimited values
    char line[64];
    int line_pos = 0;

    while (1) {
        // uart_read_bytes: blocks THIS TASK up to the given tick timeout, not the rest of the app.
        // https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/peripherals/uart.html#_CPPv414uart_read_bytes14uart_port_tPv6size_t10TickType_t
        int len = uart_read_bytes(UART_PORT, data, RX_BUF_SIZE, pdMS_TO_TICKS(100));

        for (int i = 0; i < len; i++) {
            char c = (char)data[i];
            if (c == '\n' || c == '\r') {
                if (line_pos > 0) {
                    line[line_pos] = '\0';
                    // "line" is an integer 0-1023 that comes via UART
                    float temperature = atof(line) * 3.33 / 4095 * 100; // convert to temperature ºC
                    ESP_LOGI(TAG, "LM35 ºC: %.2f", temperature);
                    line_pos = 0;
                }
            } else if (line_pos < (int)sizeof(line) - 1) {
                line[line_pos++] = c;
            }
        }
    }
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
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(LED_PERIOD_MS));
    }
}

void app_main(void)
{
    // 8N1, no flow control — matches Serial1.begin(9600) default frame format on the C3 side
    // https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/peripherals/uart.html#_CPPv415uart_config_t
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // Driver install must come before param_config/set_pin on IDF v5.x
    // https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/peripherals/uart.html#_CPPv420uart_driver_install14uart_port_tiii12QueueHandle_tPP17QueueHandle_ti
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, RX_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config));

    // TX pin unused by us (C3 -> C6 is one-way here), but still assigned in case you add replies later
    // https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/peripherals/uart.html#_CPPv412uart_set_pin14uart_port_tiiii
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    xTaskCreate(uart_rx_task, "uart_rx_task", 4096, NULL, 10, NULL);
    xTaskCreate(led_blink_task, "led_blink_task", 2048, NULL, 5, NULL);
}