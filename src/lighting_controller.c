#include "lighting_controller.h"

#include <stddef.h>

#define DAYLIGHT_OFF_THRESHOLD          70U
#define HIGH_TRAFFIC_THRESHOLD          60U
#define MOTION_HOLD_TIME_MS          10000ULL

#define ECO_BRIGHTNESS                  20U
#define PREDICTIVE_BRIGHTNESS           50U
#define FAILSAFE_BRIGHTNESS             70U
#define FULL_BRIGHTNESS                100U

static uint8_t clamp_percent(int32_t value)
{
    if (value < 0) {
        return 0U;
    }

    if (value > 100) {
        return 100U;
    }

    return (uint8_t)value;
}

static bool valid_zone(uint8_t zone_id)
{
    return zone_id < ZONE_COUNT;
}

static bool motion_is_active(
        const ZoneState *zone,
        uint64_t current_time_ms)
{
    if (zone->motion_detected) {
        return true;
    }

    if (zone->last_motion_ms == 0U ||
        current_time_ms < zone->last_motion_ms) {
        return false;
    }

    return (current_time_ms - zone->last_motion_ms)
            <= MOTION_HOLD_TIME_MS;
}

void lighting_controller_init(ZoneState zones[ZONE_COUNT])
{
    uint8_t zone_id;

    for (zone_id = 0U; zone_id < ZONE_COUNT; ++zone_id) {
        zones[zone_id].zone_id = zone_id;
        zones[zone_id].mode = LIGHT_FAILSAFE;
        zones[zone_id].brightness_percent = FAILSAFE_BRIGHTNESS;

        zones[zone_id].daylight_percent = 0U;
        zones[zone_id].predicted_demand_percent = 0U;
        zones[zone_id].motion_detected = false;
        zones[zone_id].sensor_healthy = false;

        zones[zone_id].manual_override = false;
        zones[zone_id].manual_brightness_percent = 0U;
        zones[zone_id].last_motion_ms = 0U;
    }
}

void lighting_controller_apply_sensor(
        ZoneState zones[ZONE_COUNT],
        const SensorSample *sample)
{
    ZoneState *zone;

    if (sample == NULL || !valid_zone(sample->zone_id)) {
        return;
    }

    zone = &zones[sample->zone_id];

    zone->daylight_percent =
            clamp_percent(sample->daylight_percent);

    zone->motion_detected = sample->motion_detected;
    zone->sensor_healthy = sample->sensor_healthy;

    if (sample->motion_detected) {
        zone->last_motion_ms = sample->timestamp_ms;
    }
}

void lighting_controller_apply_prediction(
        ZoneState zones[ZONE_COUNT],
        const PredictionUpdate *prediction)
{
    if (prediction == NULL ||
        !valid_zone(prediction->zone_id)) {
        return;
    }

    zones[prediction->zone_id].predicted_demand_percent =
            clamp_percent(prediction->demand_percent);
}

void lighting_controller_apply_command(
        ZoneState zones[ZONE_COUNT],
        const ManualCommand *command)
{
    ZoneState *zone;

    if (command == NULL || !valid_zone(command->zone_id)) {
        return;
    }

    zone = &zones[command->zone_id];

    switch (command->command) {
    case COMMAND_MANUAL_BRIGHTNESS:
        zone->manual_override = true;
        zone->manual_brightness_percent =
                clamp_percent(command->value);
        break;

    case COMMAND_AUTO_MODE:
        zone->manual_override = false;
        break;

    default:
        break;
    }
}

void lighting_controller_update(
        ZoneState zones[ZONE_COUNT],
        bool emergency_active,
        uint64_t current_time_ms)
{
    uint8_t zone_id;

    for (zone_id = 0U; zone_id < ZONE_COUNT; ++zone_id) {
        ZoneState *zone = &zones[zone_id];

        /*
         * Priority order:
         * Emergency > sensor failure > manual control >
         * daylight > occupancy > prediction > eco mode.
         */

        if (emergency_active) {
            zone->mode = LIGHT_EMERGENCY;
            zone->brightness_percent = FULL_BRIGHTNESS;
        }
        else if (!zone->sensor_healthy) {
            zone->mode = LIGHT_FAILSAFE;
            zone->brightness_percent = FAILSAFE_BRIGHTNESS;
        }
        else if (zone->manual_override) {
            zone->mode = LIGHT_MANUAL;
            zone->brightness_percent =
                    zone->manual_brightness_percent;
        }
        else if (zone->daylight_percent >=
                 DAYLIGHT_OFF_THRESHOLD) {
            zone->mode = LIGHT_DAY_OFF;
            zone->brightness_percent = 0U;
        }
        else if (motion_is_active(zone, current_time_ms)) {
            zone->mode = LIGHT_OCCUPIED;
            zone->brightness_percent = FULL_BRIGHTNESS;
        }
        else if (zone->predicted_demand_percent >=
                 HIGH_TRAFFIC_THRESHOLD) {
            zone->mode = LIGHT_PREDICTIVE;
            zone->brightness_percent =
                    PREDICTIVE_BRIGHTNESS;
        }
        else {
            zone->mode = LIGHT_ECO;
            zone->brightness_percent = ECO_BRIGHTNESS;
        }
    }
}

const char *lighting_mode_name(LightMode mode)
{
    switch (mode) {
    case LIGHT_DAY_OFF:
        return "DAY_OFF";

    case LIGHT_ECO:
        return "ECO";

    case LIGHT_PREDICTIVE:
        return "PREDICTIVE";

    case LIGHT_OCCUPIED:
        return "OCCUPIED";

    case LIGHT_MANUAL:
        return "MANUAL";

    case LIGHT_FAILSAFE:
        return "FAILSAFE";

    case LIGHT_EMERGENCY:
        return "EMERGENCY";

    default:
        return "UNKNOWN";
    }
}
