#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "config.h"
#include "level_sensor.h"
#include "telemetry.h"

static const char *TAG = "main";

static void level_task(void *arg)
{
    (void)arg;

    for (;;) {
        int64_t start_ms = esp_timer_get_time() / 1000;

        float dist_cm = 0.0f;
        bool  ok = level_sensor_read_cm(&dist_cm);

        if (ok) {
            ESP_LOGD(TAG, "%.1f cm", dist_cm);
        } else {
            ESP_LOGW(TAG, "sensor unreadable");
        }

        telemetry_publish_level(dist_cm, ok);

        /* Reading takes a few hundred ms; hold the publish cadence
         * steady so the controller's stale window stays meaningful. */
        int64_t elapsed = (esp_timer_get_time() / 1000) - start_ms;
        int64_t wait    = LEVEL_PUBLISH_MS - elapsed;
        vTaskDelay(pdMS_TO_TICKS(wait > 100 ? wait : 100));
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    level_sensor_init();
    telemetry_start();

    xTaskCreate(level_task, "level", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "tank node up");
}
