#include "config.h"
#include "tank_state.h"

tank_state_t tank_state_from_distance(float distance_cm, bool valid)
{
    if (!valid) {
        return TANK_UNKNOWN;
    }

    /* distance_cm is measured downward from the sensor face, so it
     * shrinks as the tank fills. */
    if (distance_cm <= DIST_FULL_CM) {
        return TANK_FULL;
    }
    if (distance_cm >= DIST_REFILL_CM) {
        return TANK_REFILLABLE;
    }

    return TANK_PARTIAL;
}

const char *tank_state_name(tank_state_t state)
{
    switch (state) {
        case TANK_FULL:       return "full";
        case TANK_PARTIAL:    return "partial";
        case TANK_REFILLABLE: return "refillable";
        default:              return "unknown";
    }
}
