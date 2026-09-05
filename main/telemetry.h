#pragma once

#include <stdbool.h>

#include "tank_state.h"

void telemetry_start(void);
bool telemetry_online(void);

/* Publishes a level reading in the envelope MQTT_CONTRACT.md defines
 * for this topic. Pass valid=false when the sensor could not be read;
 * silence and a bad reading must not look the same downstream, and
 * distance_cm then goes out as null rather than as a distance of zero,
 * which reads as a full tank. */
void telemetry_publish_level(float distance_cm, bool valid);

/* Publishes the derived tank state to the pump controller, in the
 * envelope MQTT_CONTRACT.md defines for the full_tank topic.
 *
 * No distance goes out on this topic. The controller acts on the state
 * alone, so there is nothing for it to re-derive with a threshold of
 * its own that has drifted from this node's.
 *
 * Published every cycle rather than only on a change: the controller
 * faults if this stream goes stale, and it is the silence that has to
 * be detectable, not just the transition. */
void telemetry_publish_tank_state(tank_state_t state);
