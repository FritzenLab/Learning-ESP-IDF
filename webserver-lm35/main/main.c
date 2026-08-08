// Xiao ESP32-C6 pinout https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/
#include <stdio.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "bootloader_random.h"
#include "esp_random.h"
#include <inttypes.h>
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#define LED_GPIO        GPIO_NUM_1
uint32_t LED_PERIOD_MS = 0;
#define LM35_ADC_CHANNEL ADC_CHANNEL_2

#define RAND_MIN 50
#define RAND_MAX_INCL 1000
#define WIFI_SSID "Clovis 2.4G"
#define WIFI_PASS "99143304"

static const char *TAG = "LM35_WEB";

adc_oneshot_unit_handle_t adc1_handle;
adc_cali_handle_t adc1_cali_handle = NULL;

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
        //ESP_LOGI(TAG, "Delay: %" PRIu32, LED_PERIOD_MS);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(LED_PERIOD_MS));
    }
}
static void adc_init(void)
{
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    // https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-reference/peripherals/adc_oneshot.html
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_0,     // ~0-1.1V range
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, LM35_ADC_CHANNEL, &config));

    // Calibration gives millivolt values instead of raw counts
    // https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-reference/peripherals/adc_calibration.html
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .chan = LM35_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_0,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &adc1_cali_handle));
}

static float read_lm35_temp_c(void)
{
    int raw, mv;
    adc_oneshot_read(adc1_handle, LM35_ADC_CHANNEL, &raw);
    adc_cali_raw_to_voltage(adc1_cali_handle, raw, &mv);
    // LM35 outputs 10mV per °C
    return mv * 1.1 / 4095 * 100;
}

// Handler for the root page: returns HTML with JS that polls /adc.json
static esp_err_t root_get_handler(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html><html><head><title>LM35 Monitor</title></head>"
        "<body><h1>Temperature</h1><h2 id='t'>-- C</h2>"
        "<script>"
        "setInterval(()=>{fetch('/adc.json').then(r=>r.json()).then(d=>{"
        "document.getElementById('t').innerText=d.temp.toFixed(1)+' C';});},1000);"
        "</script></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Handler for the JSON data endpoint, polled by the page's JS
static esp_err_t adc_get_handler(httpd_req_t *req)
{
    char resp[64];
    float t = read_lm35_temp_c();
    snprintf(resp, sizeof(resp), "{\"temp\":%.2f}", t);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-reference/protocols/esp_http_server.html
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_uri = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
        httpd_uri_t adc_uri  = { .uri = "/adc.json", .method = HTTP_GET, .handler = adc_get_handler };
        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &adc_uri);
    }
    return server;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        start_webserver();
    }
}

static void wifi_init_sta(void)
{
    // https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-guides/wifi.html
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}
void app_main(void)
{   
    ESP_ERROR_CHECK(nvs_flash_init());
    adc_init();
    wifi_init_sta();
    // Only needed if Wi-Fi/BT/Thread/Zigbee are NOT active.
    // Must be paired with bootloader_random_disable() before using
    // ADC or any RF subsystem afterward.
    bootloader_random_enable();
    // create the blinking task
    xTaskCreate(led_blink_task, "led_blink_task", 2048, NULL, 5, NULL);
    
}