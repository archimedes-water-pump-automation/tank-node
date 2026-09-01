#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "mqtt_client.h"

#include "config.h"
#include "telemetry.h"

static const char *TAG = "telemetry";

static esp_mqtt_client_handle_t s_client;
static volatile bool            s_connected;

/* Delivered by the broker if this node drops off without a clean
 * disconnect. The pump controller sees valid:false and stops, rather
 * than coasting on the last good reading until its stale timer fires. */
static const char *LWT_PAYLOAD =
    "{\"event\":\"level\",\"device\":\"" DEVICE_ID "\",\"distance_cm\":null,"
    "\"valid\":false,\"reason\":\"node_offline\"}";

bool telemetry_online(void)
{
    return s_connected;
}

void telemetry_publish_level(float distance_cm, bool valid)
{
    if (!s_connected || s_client == NULL) {
        return;
    }

    char payload[192];
    uint32_t up = (uint32_t)(esp_timer_get_time() / 1000000);

    if (valid) {
        snprintf(payload, sizeof(payload),
                 "{\"event\":\"level\",\"device\":\"%s\",\"distance_cm\":%.1f,"
                 "\"valid\":true,\"uptime_s\":%lu}",
                 DEVICE_ID, distance_cm, (unsigned long)up);
    } else {
        snprintf(payload, sizeof(payload),
                 "{\"event\":\"level\",\"device\":\"%s\",\"distance_cm\":null,"
                 "\"valid\":false,\"reason\":\"sensor_unreadable\","
                 "\"uptime_s\":%lu}",
                 DEVICE_ID, (unsigned long)up);
    }

    /* QoS 0, not retained. A queued or retained level reading that
     * arrives late is worse than none at all: the controller would
     * treat stale data as fresh. Freshness is the whole point here. */
    esp_mqtt_client_enqueue(s_client, TOPIC_LEVEL, payload,
                            (int)strlen(payload), 0, 0, true);
}

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)arg; (void)base; (void)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            s_connected = true;
            ESP_LOGI(TAG, "broker connected");
            break;
        case MQTT_EVENT_DISCONNECTED:
            s_connected = false;
            ESP_LOGW(TAG, "broker disconnected");
            break;
        default:
            break;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)arg; (void)event_data;

    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_wifi_connect();
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "wifi up");
        esp_mqtt_client_start(s_client);
    }
}

void telemetry_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri                  = MQTT_BROKER_URI,
        .credentials.username                = MQTT_USERNAME,
        .credentials.client_id               = DEVICE_ID,
        .credentials.authentication.password = MQTT_PASSWORD,
        .session.last_will.topic             = TOPIC_LEVEL,
        .session.last_will.msg               = LWT_PAYLOAD,
        .session.last_will.msg_len           = 0,
        .session.last_will.qos               = 1,
        .session.last_will.retain            = 0,
        .session.keepalive                   = 15,
        .network.reconnect_timeout_ms        = 3000,
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "mqtt client init failed");
        return;
    }

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
        s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_start());
}
