#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "driver/gpio.h"


static const char *TAG = "ADC_TO_PWM";

// ESP32-C6 has a single ADC unit (ADC1). ADC_CHANNEL_2 maps to GPIO2.
// Channel-to-GPIO mapping table:
// https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/peripherals/adc_oneshot.html#adc-attenuation
#define ADC_UNIT        ADC_UNIT_1
#define ADC_CHAN        ADC_CHANNEL_2

// PWM output pin and LEDC config
#define PWM_GPIO        GPIO_NUM_1
#define PWM_TIMER       LEDC_TIMER_0
#define PWM_MODE        LEDC_LOW_SPEED_MODE   // ESP32-C6 only supports low-speed mode
#define PWM_CHANNEL     LEDC_CHANNEL_0
#define PWM_DUTY_RES    LEDC_TIMER_13_BIT     // 13-bit -> duty range 0-8191
#define PWM_FREQ_HZ     500

static adc_oneshot_unit_handle_t adc_handle;

static void adc_init(void)
{
    // Oneshot ADC driver init reference:
    // https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/peripherals/adc_oneshot.html
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        // ADC_ATTEN_DB_12 gives full ~0-3.3V input range on ESP32-C6
        // https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/peripherals/adc_oneshot.html#adc-attenuation
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT, // resolves to 12-bit on C6
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHAN, &chan_cfg));
}

static void pwm_init(void)
{
    // LEDC PWM configuration reference:
    // https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/peripherals/ledc.html
    ledc_timer_config_t timer_cfg = {
        .speed_mode       = PWM_MODE,
        .timer_num        = PWM_TIMER,
        .duty_resolution  = PWM_DUTY_RES,
        .freq_hz          = PWM_FREQ_HZ,
        .clk_cfg          = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t channel_cfg = {
        .gpio_num       = PWM_GPIO,
        .speed_mode     = PWM_MODE,
        .channel        = PWM_CHANNEL,
        .timer_sel      = PWM_TIMER,
        .duty           = 0,
        .hpoint         = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_cfg));
}

void app_main(void)
{
    adc_init();
    pwm_init();

    int raw = 0;
    const int adc_max  = 3400; // 12-bit ADC max value
    const int duty_max = 8191; // 13-bit LEDC duty max value

    while (1) {
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHAN, &raw));

        // Linear map: ADC raw (0-4095) -> PWM duty (0-8191)
        int duty = (raw * duty_max) / adc_max;

        if(duty > 8191){
            duty = 8191;
        }
        ledc_set_duty(PWM_MODE, PWM_CHANNEL, duty);
        ledc_update_duty(PWM_MODE, PWM_CHANNEL);

        ESP_LOGI(TAG, "ADC raw: %d -> PWM duty: %d", raw, duty);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
