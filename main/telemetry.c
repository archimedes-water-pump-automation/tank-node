#include <stdarg.h>
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
#include "wallclock.h"

static const char *TAG = "telemetry";

static esp_mqtt_client_handle_t s_client;
static volatile bool            s_connected;

/* Delivered by the broker if this node drops off without a clean
 * disconnect. The pump controller sees state:"unknown" and stops,
 * rather than coasting on the last good state until its stale timer
 * fires.
 *
 * MQTT allows one will per connection, and this is the one worth
 * having: the full_tank stream is what holds a pump off, while a level
 * that stops arriving only means the server's volume stops updating.
 *
 * It carries no timestamp and no uptime: the broker publishes it on
 * this node's behalf, long after the node wrote it, so both fields
 * would be lies. MQTT_CONTRACT.md makes them optional for exactly
 * this case. */
static const char *LWT_PAYLOAD =
    "{\"event\":\"full_tank\",\"device\":\"" DEVICE_ID "\","
    "\"state\":\"unknown\",\"reason\":\"node_offline\"}";

bool telemetry_online(void)
{
    return s_connected;
}

/* Appends to buf at *off, tracking the length the message would have
 * needed so the caller can tell a truncated payload from a whole one. */
static void json_append(char *buf, size_t size, size_t *off,
                        const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(*off < size ? buf + *off : NULL,
                      *off < size ? size - *off : 0,
                      fmt, ap);
    va_end(ap);

    if (n > 0) {
        *off += (size_t)n;
    }
}

/* Writes the envelope every message on every topic shares: the event
 * name, this device, and the UTC timestamp when the clock has one. See
 * MQTT_CONTRACT.md. */
static void json_open_envelope(char *buf, size_t size, size_t *off,
                               const char *event)
{
    char ts[WALLCLOCK_ISO8601_LEN];

    json_append(buf, size, off, "{\"event\":\"%s\",\"device\":\"%s\"",
                event, DEVICE_ID);

    if (wallclock_iso8601(ts, sizeof(ts))) {
        json_append(buf, size, off, ",\"timestamp\":\"%s\"", ts);
    }
}

void telemetry_publish_level(float distance_cm, bool valid)
{
    char   payload[256];
    size_t off = 0;

    if (!s_connected || s_client == NULL) {
        return;
    }

    json_open_envelope(payload, sizeof(payload), &off, "level");

    if (valid) {
        json_append(payload, sizeof(payload), &off,
                    ",\"valid\":true,\"distance_cm\":%.1f", distance_cm);
    } else {
        /* distance_cm stays null rather than 0.0: a consumer that read
         * zero centimetres would see a tank filled to the sensor. */
        json_append(payload, sizeof(payload), &off,
                    ",\"valid\":false,\"distance_cm\":null,"
                    "\"reason\":\"sensor_unreadable\"");
    }

    json_append(payload, sizeof(payload), &off, ",\"uptime_s\":%lu}",
                (unsigned long)(esp_timer_get_time() / 1000000));

    if (off >= sizeof(payload)) {
        ESP_LOGE(TAG, "level payload truncated at %u bytes, not published",
                 (unsigned)sizeof(payload));
        return;
    }

    /* QoS 0, not retained. A queued or retained level reading that
     * arrives late is worse than none at all: the controller would
     * treat stale data as fresh. Freshness is the whole point here. */
    esp_mqtt_client_enqueue(s_client, TOPIC_LEVEL, payload,
                            (int)off, 0, 0, true);
}

void telemetry_publish_tank_state(tank_state_t state)
{
    char   payload[224];
    size_t off = 0;

    if (!s_connected || s_client == NULL) {
        return;
    }

    json_open_envelope(payload, sizeof(payload), &off, "full_tank");
    json_append(payload, sizeof(payload), &off, ",\"state\":\"%s\"",
                tank_state_name(state));

    if (state == TANK_UNKNOWN) {
        json_append(payload, sizeof(payload), &off,
                    ",\"reason\":\"sensor_unreadable\"");
    }

    json_append(payload, sizeof(payload), &off, ",\"uptime_s\":%lu}",
                (unsigned long)(esp_timer_get_time() / 1000000));

    if (off >= sizeof(payload)) {
        ESP_LOGE(TAG, "full_tank payload truncated at %u bytes, not published",
                 (unsigned)sizeof(payload));
        return;
    }

    /* QoS 0, not retained, for the same reason as the level stream: a
     * queued or retained state that arrives late would be treated as
     * current, and this one holds a pump off. */
    esp_mqtt_client_enqueue(s_client, TOPIC_FULL_TANK, payload,
                            (int)off, 0, 0, true);
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
        .session.last_will.topic             = TOPIC_FULL_TANK,
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
