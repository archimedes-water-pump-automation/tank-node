#pragma once

#include <stdbool.h>

/* Configures the trigger and echo pins and installs the echo ISR. */
void level_sensor_init(void);

/* Median of up to 5 good pings. Returns false if the sensor is
 * unreadable, which the caller must report rather than hide. */
bool level_sensor_read_cm(float *out_cm);
