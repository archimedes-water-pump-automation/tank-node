#pragma once

#include <stdbool.h>

void telemetry_start(void);
bool telemetry_online(void);

/* Publishes a level reading in the envelope MQTT_CONTRACT.md defines
 * for this topic. Pass valid=false when the sensor could not be read;
 * silence and a bad reading must not look the same downstream, and
 * distance_cm then goes out as null rather than as a distance of zero,
 * which reads as a full tank. */
void telemetry_publish_level(float distance_cm, bool valid);
