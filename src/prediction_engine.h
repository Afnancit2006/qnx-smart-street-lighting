#ifndef PREDICTION_ENGINE_H_
#define PREDICTION_ENGINE_H_

#include <stdint.h>

#include "lighting_types.h"

#define HOURS_PER_DAY 24U

typedef struct {
    /* Historical demand for every zone and hour. */
    uint8_t hourly_profile[ZONE_COUNT][HOURS_PER_DAY];

    /* Recent PIR detections used to correct the history. */
    uint16_t recent_motion_count[ZONE_COUNT];
} PredictionEngine;

/* Loads the initial demonstration traffic profile. */
void prediction_engine_init(PredictionEngine *engine);

/* Records a real-time PIR detection. */
void prediction_engine_record_motion(
        PredictionEngine *engine,
        uint8_t zone_id);

/* Combines historical and recent traffic information. */
uint8_t prediction_engine_calculate(
        PredictionEngine *engine,
        uint8_t zone_id,
        uint8_t hour);

/* Updates history using newly observed traffic. */
void prediction_engine_learn(
        PredictionEngine *engine,
        uint8_t zone_id,
        uint8_t hour,
        uint8_t observed_demand_percent);

#endif /* PREDICTION_ENGINE_H_ */
