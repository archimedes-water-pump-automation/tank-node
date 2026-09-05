#pragma once

#define DEVICE_ID "tank-01"

/* ======================= pin map (ESP32-C3 DevKitM-1) ======================= */

#define PIN_TRIG  GPIO_NUM_3    /* JSN-SR04T trigger, 3.3 V drive is fine */
#define PIN_ECHO  GPIO_NUM_4    /* echo, via 1k/2k divider                */

/* ======================= sensor limits ======================= */

#define DIST_MIN_VALID_CM   3.0f
#define DIST_MAX_VALID_CM 400.0f

/* ======================= tank geometry ======================= */

/* The one threshold the pump controller acts on. It used to live in its
 * config, applied to a distance it received; it no longer sees a
 * distance, so it lives here with the sensor that produces one, and
 * this node publishes the derived state instead. See tank_state.h.
 *
 * There is no second, lower threshold: the controller starts its pump
 * from the flow sensor alone, so no level ever starts it and there is
 * no restart threshold to set.
 *
 * It depends on where the transducer is physically mounted. Measure
 * from the sensor face to the intended stop level and add margin. */
#define DIST_FULL_CM     12.0f   /* <= this: full, the pump must stop */

/* ======================= publish cadence ======================= */

/* One reading feeds both topics, so this is the cadence of both. The
 * full_tank stream is a control input for the pump controller, not just
 * telemetry, which is why it runs far faster than a reporting interval
 * would need: the controller's stale window must be a multiple of it,
 * and a slower cadence there means spurious faults. */
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

/* Two topics, two audiences, both fixed by MQTT_CONTRACT.md.
 *
 * The level stream carries the measurement and is read by
 * archimedes-server, which turns it into a volume.
 *
 * The full_tank stream carries only the derived state — no distance —
 * and is read by pump-ctl, which acts on it. A controller that cannot
 * see a raw distance cannot apply a threshold of its own that has
 * drifted from this node's. */
#define TOPIC_LEVEL     "watertank/" DEVICE_ID "/level"
#define TOPIC_FULL_TANK "watertank/" DEVICE_ID "/full_tank"
