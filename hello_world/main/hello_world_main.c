#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_log.h"

#define LED_GPIO 1 // D1 on Xiao ESP32-C6
#define BUTTON_GPIO 17 // D7 on Xiao ESP32-C6

static int led_state = 0;
static bool blink_LED = false;
bool last_button_state = true; // idle state with pull-up is HIGH (1)
int debounce_counter = 0;
const int DEBOUNCE_THRESHOLD = 3; // require 3 consecutive stable polls (~60ms) before accepting a press

static bool timer_on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx) {
    if(blink_LED){
        if(led_state == false){
            led_state = true;    
        } else{
            led_state = false;
        }
        gpio_set_level(LED_GPIO, led_state);
    } else{
        gpio_set_level(LED_GPIO, false);
    }
    
    return false;
}

void app_main(void) {
    
    // Configure GPIO
    gpio_reset_pin(LED_GPIO);
    gpio_reset_pin(BUTTON_GPIO);

    // LED pin: output, no pull resistor needed
    // https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/gpio.html#_CPPv415gpio_config_t
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_conf);

    // Button pin: input, internal pull-up so idle reads HIGH, press pulls to GND (LOW)
    gpio_config_t button_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&button_conf);

    // Configure GPTimer
    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
        //.clk_src = GPTIMER_CLK_SRC_DEFAULT, // this was the default clock source
        .clk_src = SOC_MOD_CLK_XTAL, // I picked this just for testing purposes
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 100000, // 100 kHz, 1 tick = 10 us
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

    gptimer_event_callbacks_t cbs = {
        .on_alarm = timer_on_alarm_cb,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));

    gptimer_alarm_config_t alarm_config = {
        .alarm_count = 20000, // 200,000 us = 200 ms
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));

    ESP_ERROR_CHECK(gptimer_enable(gptimer));
    ESP_ERROR_CHECK(gptimer_start(gptimer));

    while (1) {
        bool current_button_state = gpio_get_level(BUTTON_GPIO);

        if (current_button_state == false) {
            // Button reads pressed — count consecutive low polls to filter out bounce.
            debounce_counter++;
            if (debounce_counter == DEBOUNCE_THRESHOLD && last_button_state == true) {
                blink_LED = !blink_LED;
                last_button_state = false; // latch until the button is confirmed released
            }
        } else {
            // Button released — reset so the next press can be detected again.
            debounce_counter = 0;
            last_button_state = true;
        }
        // Equivalent to Arduino's Serial.println():
        //ESP_LOGI("BTN", "level=%d", current_button_state);
        // Allow the CPU do execute all other stuff it needs
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}