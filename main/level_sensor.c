#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_log.h"

#include "config.h"
#include "level_sensor.h"

static const char *TAG = "level";

static volatile int64_t  s_echo_rise  = 0;
static volatile int64_t  s_echo_width = 0;
static SemaphoreHandle_t s_echo_done;

static void IRAM_ATTR echo_isr(void *arg)
{
    (void)arg;
    int64_t now = esp_timer_get_time();

    if (gpio_get_level(PIN_ECHO)) {
        s_echo_rise = now;
        return;
    }
    if (s_echo_rise == 0) {
        return;
    }

    s_echo_width = now - s_echo_rise;
    s_echo_rise  = 0;

    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_echo_done, &hp);
    if (hp) {
        portYIELD_FROM_ISR();
    }
}

static bool ping(float *out_cm)
{
    xSemaphoreTake(s_echo_done, 0);   /* drain a stale completion */
    s_echo_rise = 0;

    gpio_set_level(PIN_TRIG, 0);
    esp_rom_delay_us(4);
    gpio_set_level(PIN_TRIG, 1);
    esp_rom_delay_us(10);
    gpio_set_level(PIN_TRIG, 0);

    /* 400 cm round trip is about 23 ms; 60 ms covers the no-echo case. */
    if (xSemaphoreTake(s_echo_done, pdMS_TO_TICKS(60)) != pdTRUE) {
        return false;
    }

    float cm = (float)s_echo_width / 58.0f;
    if (cm < DIST_MIN_VALID_CM || cm > DIST_MAX_VALID_CM) {
        return false;
    }

    *out_cm = cm;
    return true;
}

bool level_sensor_read_cm(float *out_cm)
{
    float samples[5];
    int   n = 0;

    for (int attempt = 0; attempt < 8 && n < 5; attempt++) {
        float d;
        if (ping(&d)) {
            samples[n++] = d;
        }
        vTaskDelay(pdMS_TO_TICKS(60));
    }

    if (n < 3) {
        return false;
    }

    for (int i = 1; i < n; i++) {
        float key = samples[i];
        int   j   = i - 1;
        while (j >= 0 && samples[j] > key) {
            samples[j + 1] = samples[j];
            j--;
        }
        samples[j + 1] = key;
    }

    *out_cm = samples[n / 2];
    return true;
}

void level_sensor_init(void)
{
    gpio_config_t out_conf = {
        .pin_bit_mask = (1ULL << PIN_TRIG),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&out_conf));
    gpio_set_level(PIN_TRIG, 0);

    gpio_config_t echo_conf = {
        .pin_bit_mask = (1ULL << PIN_ECHO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&echo_conf));

    s_echo_done = xSemaphoreCreateBinary();
    if (s_echo_done == NULL) {
        ESP_LOGE(TAG, "semaphore alloc failed");
        return;
    }

    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_IRAM));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_ECHO, echo_isr, NULL));
}
