#include "energy_monitor.h"

#include <stddef.h>

#define FULL_BRIGHTNESS_PERCENT 100U
#define MILLISECONDS_PER_HOUR 3600000.0
#define WATTS_PER_KILOWATT 1000.0

void energy_monitor_init(EnergyMonitor *monitor)
{
    if (monitor == NULL) {
        return;
    }

    monitor->last_update_ms = 0U;
    monitor->actual_brightness_ms = 0U;
    monitor->baseline_brightness_ms = 0U;
}

void energy_monitor_update(
        EnergyMonitor *monitor,
        const ZoneState zones[ZONE_COUNT],
        uint64_t current_time_ms)
{
    uint64_t elapsed_ms;
    uint64_t total_brightness = 0U;
    uint8_t zone_id;

    if (monitor == NULL || zones == NULL) {
        return;
    }

    /*
     * The first call establishes the starting time.
     * No energy is accumulated until the next update.
     */
    if (monitor->last_update_ms == 0U) {
        monitor->last_update_ms = current_time_ms;
        return;
    }

    if (current_time_ms <= monitor->last_update_ms) {
        return;
    }

    elapsed_ms = current_time_ms - monitor->last_update_ms;

    for (zone_id = 0U; zone_id < ZONE_COUNT; ++zone_id) {
        total_brightness += zones[zone_id].brightness_percent;
    }

    monitor->actual_brightness_ms +=
            total_brightness * elapsed_ms;

    /*
     * Baseline: every zone remains at 100% brightness
     * for the entire operating time.
     */
    monitor->baseline_brightness_ms +=
            ((uint64_t)ZONE_COUNT *
             FULL_BRIGHTNESS_PERCENT *
             elapsed_ms);

    monitor->last_update_ms = current_time_ms;
}

double energy_monitor_savings_percent(
        const EnergyMonitor *monitor)
{
    uint64_t saved_brightness_ms;

    if (monitor == NULL ||
        monitor->baseline_brightness_ms == 0U) {
        return 0.0;
    }

    if (monitor->actual_brightness_ms >=
        monitor->baseline_brightness_ms) {
        return 0.0;
    }

    saved_brightness_ms =
            monitor->baseline_brightness_ms -
            monitor->actual_brightness_ms;

    return ((double)saved_brightness_ms * 100.0) /
            (double)monitor->baseline_brightness_ms;
}

double energy_monitor_actual_kwh(
        const EnergyMonitor *monitor,
        double rated_watts_per_light)
{
    if (monitor == NULL || rated_watts_per_light < 0.0) {
        return 0.0;
    }

    return ((double)monitor->actual_brightness_ms *
            rated_watts_per_light) /
            (100.0 *
             MILLISECONDS_PER_HOUR *
             WATTS_PER_KILOWATT);
}

double energy_monitor_baseline_kwh(
        const EnergyMonitor *monitor,
        double rated_watts_per_light)
{
    if (monitor == NULL || rated_watts_per_light < 0.0) {
        return 0.0;
    }

    return ((double)monitor->baseline_brightness_ms *
            rated_watts_per_light) /
            (100.0 *
             MILLISECONDS_PER_HOUR *
             WATTS_PER_KILOWATT);
}
