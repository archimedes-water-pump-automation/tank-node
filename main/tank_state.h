#pragma once

#include <stdbool.h>

/* What this node tells the pump controller about the tank.
 *
 * One question only: is the tank full? The controller starts its pump
 * from the flow sensor alone, so a falling level is not something it
 * acts on and not something this node reports. What it does act on is
 * a tank with nowhere left to put water, which stops the pump.
 *
 * The threshold behind it lives here, beside the sensor. See
 * MQTT_CONTRACT.md. */
typedef enum {
    TANK_UNKNOWN,   /* no usable reading; never means room in the tank */
    TANK_FULL,      /* distance <= DIST_FULL_CM: stop pumping         */
    TANK_NOT_FULL   /* room left; on its own, never a reason to pump  */
} tank_state_t;

/* Derives the state from one reading. Pass valid=false when the sensor
 * could not be read: that is TANK_UNKNOWN, which the controller treats
 * as a fault and stops on, never as a tank with room in it. */
tank_state_t tank_state_from_distance(float distance_cm, bool valid);

/* The wire name of a state, as published in the full_tank event. */
const char *tank_state_name(tank_state_t state);
