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

    return TANK_NOT_FULL;
}

const char *tank_state_name(tank_state_t state)
{
    switch (state) {
        case TANK_FULL:     return "full";
        case TANK_NOT_FULL: return "not_full";
        default:            return "unknown";
    }
}
