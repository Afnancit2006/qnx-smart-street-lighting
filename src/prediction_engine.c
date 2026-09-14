#include "prediction_engine.h"

#include <stddef.h>
#include <string.h>

#define HISTORY_WEIGHT_PERCENT       70U
#define RECENT_WEIGHT_PERCENT        30U
#define MOTION_SCORE_PER_EVENT       20U
#define MAX_RECENT_MOTION_EVENTS      5U
#define OLD_HISTORY_WEIGHT_PERCENT   80U
#define NEW_HISTORY_WEIGHT_PERCENT   20U

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

static uint8_t default_demand_for_hour(uint8_t hour)
{
    if (hour < 6U) {
        return 10U;   /* Midnight and early morning */
    }

    if (hour < 10U) {
        return 75U;   /* Morning peak */
    }

    if (hour < 16U) {
        return 40U;   /* Normal daytime traffic */
    }

    if (hour < 21U) {
        return 85U;   /* Evening peak */
    }

    return 25U;       /* Late evening */
}

void prediction_engine_init(PredictionEngine *engine)
{
    static const int8_t zone_adjustment[ZONE_COUNT] = {
        0, 8, -5, 5
    };

    uint8_t zone_id;
    uint8_t hour;

    if (engine == NULL) {
        return;
    }

    memset(engine, 0, sizeof(*engine));

    for (zone_id = 0U; zone_id < ZONE_COUNT; ++zone_id) {
        for (hour = 0U; hour < HOURS_PER_DAY; ++hour) {
            int32_t adjusted_demand =
                    (int32_t)default_demand_for_hour(hour) +
                    zone_adjustment[zone_id];

            engine->hourly_profile[zone_id][hour] =
                    clamp_percent(adjusted_demand);
        }
    }
}

void prediction_engine_record_motion(
        PredictionEngine *engine,
        uint8_t zone_id)
{
    if (engine == NULL || !valid_zone(zone_id)) {
        return;
    }

    if (engine->recent_motion_count[zone_id] <
        MAX_RECENT_MOTION_EVENTS) {
        ++engine->recent_motion_count[zone_id];
    }
}

uint8_t prediction_engine_calculate(
        PredictionEngine *engine,
        uint8_t zone_id,
        uint8_t hour)
{
    uint32_t historical_demand;
    uint32_t recent_demand;
    uint32_t combined_demand;

    if (engine == NULL ||
        !valid_zone(zone_id) ||
        hour >= HOURS_PER_DAY) {
        return 0U;
    }

    historical_demand =
            engine->hourly_profile[zone_id][hour];

    recent_demand =
            engine->recent_motion_count[zone_id] *
            MOTION_SCORE_PER_EVENT;

    if (recent_demand > 100U) {
        recent_demand = 100U;
    }

    combined_demand =
            (historical_demand * HISTORY_WEIGHT_PERCENT) +
            (recent_demand * RECENT_WEIGHT_PERCENT);

    combined_demand /= 100U;

    /*
     * Gradually forget old motion events so that one vehicle
     * does not keep the prediction permanently high.
     */
    engine->recent_motion_count[zone_id] /= 2U;

    return clamp_percent((int32_t)combined_demand);
}

void prediction_engine_learn(
        PredictionEngine *engine,
        uint8_t zone_id,
        uint8_t hour,
        uint8_t observed_demand_percent)
{
    uint32_t old_value;
    uint32_t observed_value;
    uint32_t learned_value;

    if (engine == NULL ||
        !valid_zone(zone_id) ||
        hour >= HOURS_PER_DAY) {
        return;
    }

    old_value = engine->hourly_profile[zone_id][hour];
    observed_value = clamp_percent(observed_demand_percent);

    learned_value =
            (old_value * OLD_HISTORY_WEIGHT_PERCENT) +
            (observed_value * NEW_HISTORY_WEIGHT_PERCENT);

    learned_value /= 100U;

    engine->hourly_profile[zone_id][hour] =
            clamp_percent((int32_t)learned_value);
}
