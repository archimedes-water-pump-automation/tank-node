#pragma once

#include <stdbool.h>

void telemetry_start(void);
bool telemetry_online(void);

/* Publishes a level reading. Pass valid=false when the sensor could not
 * be read; silence and a bad reading must not look the same downstream. */
void telemetry_publish_level(float distance_cm, bool valid);
