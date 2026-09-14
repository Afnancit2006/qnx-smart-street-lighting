#ifndef LIGHTING_CONTROLLER_H_
#define LIGHTING_CONTROLLER_H_

#include <stdbool.h>
#include <stdint.h>

#include "lighting_types.h"

/* Creates the initial state for every lighting zone. */
void lighting_controller_init(ZoneState zones[ZONE_COUNT]);

/* Stores a new sensor reading for one zone. */
void lighting_controller_apply_sensor(
        ZoneState zones[ZONE_COUNT],
        const SensorSample *sample);

/* Stores a new predicted traffic demand for one zone. */
void lighting_controller_apply_prediction(
        ZoneState zones[ZONE_COUNT],
        const PredictionUpdate *prediction);

/* Applies manual and automatic-mode CLI commands. */
void lighting_controller_apply_command(
        ZoneState zones[ZONE_COUNT],
        const ManualCommand *command);

/* Calculates the correct mode and brightness for every zone. */
void lighting_controller_update(
        ZoneState zones[ZONE_COUNT],
        bool emergency_active,
        uint64_t current_time_ms);

/* Converts a lighting mode into readable CLI text. */
const char *lighting_mode_name(LightMode mode);

#endif /* LIGHTING_CONTROLLER_H_ */
