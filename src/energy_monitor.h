#ifndef ENERGY_MONITOR_H_
#define ENERGY_MONITOR_H_

#include <stdint.h>

#include "lighting_types.h"

typedef struct {
    uint64_t last_update_ms;

    /*
     * Brightness percentage multiplied by operating time.
     * It is accumulated across all zones.
     */
    uint64_t actual_brightness_ms;
    uint64_t baseline_brightness_ms;
} EnergyMonitor;

void energy_monitor_init(EnergyMonitor *monitor);

void energy_monitor_update(
        EnergyMonitor *monitor,
        const ZoneState zones[ZONE_COUNT],
        uint64_t current_time_ms);

double energy_monitor_savings_percent(
        const EnergyMonitor *monitor);

double energy_monitor_actual_kwh(
        const EnergyMonitor *monitor,
        double rated_watts_per_light);

double energy_monitor_baseline_kwh(
        const EnergyMonitor *monitor,
        double rated_watts_per_light);

#endif /* ENERGY_MONITOR_H_ */
