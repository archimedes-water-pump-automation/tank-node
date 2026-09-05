#pragma once

#define DEVICE_ID "tank-01"

/* ======================= pin map (ESP32-C3 DevKitM-1) ======================= */

#define PIN_TRIG  GPIO_NUM_3    /* JSN-SR04T trigger, 3.3 V drive is fine */
#define PIN_ECHO  GPIO_NUM_4    /* echo, via 1k/2k divider                */

/* ======================= sensor limits ======================= */

#define DIST_MIN_VALID_CM   3.0f
#define DIST_MAX_VALID_CM 400.0f

/* ======================= publish cadence ======================= */

/* The pump controller uses this stream as a control input, not just
 * telemetry, so it runs far faster than the original 30 s reporting
 * interval. The controller's stale window must be a multiple of this. */
#define LEVEL_PUBLISH_MS 5000

/* ======================= clock ======================= */

/* Events carry a UTC timestamp once SNTP has landed; see
 * MQTT_CONTRACT.md. Until then the field is simply absent and the
 * consumer falls back to its own receipt time. The clock never gates a
 * reading or a publish. */
#define SNTP_SERVER "pool.ntp.org"

/* ======================= network ======================= */

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy main/secrets.h.example to main/secrets.h and fill in credentials"
#endif

/* The level stream. Its payload is fixed by MQTT_CONTRACT.md, which
 * pump-ctl and archimedes-server parse against. */
#define TOPIC_LEVEL "watertank/" DEVICE_ID "/level"
