#pragma once

#include <stdbool.h>

/* What this node tells the pump controller about the tank.
 *
 * The controller used to derive this itself from the raw distance. It
 * no longer sees a distance at all, so the derivation — and both
 * thresholds behind it — lives here, in the one place that owns the
 * sensor. See MQTT_CONTRACT.md.
 *
 * Two thresholds, not one, because a single one makes the pump relay
 * chatter as the water surface moves across it: the tank is full at
 * DIST_FULL_CM and only counts as refillable again at the further
 * DIST_REFILL_CM. TANK_PARTIAL is the band between them, where a
 * running pump keeps running and an idle one stays idle. */
typedef enum {
    TANK_UNKNOWN,    /* no usable reading; never means room in the tank */
    TANK_FULL,       /* distance <= DIST_FULL_CM: nowhere to put water  */
    TANK_PARTIAL,    /* inside the hysteresis band: no transition       */
    TANK_REFILLABLE  /* distance >= DIST_REFILL_CM: low enough to start */
} tank_state_t;

/* Derives the state from one reading. Pass valid=false when the sensor
 * could not be read: that is TANK_UNKNOWN, which the controller treats
 * as a fault and stops on, never as a tank with room in it. */
tank_state_t tank_state_from_distance(float distance_cm, bool valid);

/* The wire name of a state, as published in the full_tank event. */
const char *tank_state_name(tank_state_t state);
