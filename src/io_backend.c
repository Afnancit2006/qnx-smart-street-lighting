#include "io_backend.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

static bool valid_zone(uint8_t zone_id)
{
    return zone_id < ZONE_COUNT;
}

static uint8_t clamp_percent(uint8_t value)
{
    return value > 100U ? 100U : value;
}

static int lock_backend(IoBackend *backend)
{
    int result = pthread_mutex_lock(&backend->lock);

    if (result != 0) {
        errno = result;
        return -1;
    }

    return 0;
}

static int unlock_backend(IoBackend *backend)
{
    int result = pthread_mutex_unlock(&backend->lock);

    if (result != 0) {
        errno = result;
        return -1;
    }

    return 0;
}

int io_backend_init(IoBackend *backend)
{
    uint8_t zone_id;
    int result;

    if (backend == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(backend, 0, sizeof(*backend));

    result = pthread_mutex_init(&backend->lock, NULL);

    if (result != 0) {
        errno = result;
        return -1;
    }

    for (zone_id = 0U; zone_id < ZONE_COUNT; ++zone_id) {
        /* Start the demonstration in nighttime conditions. */
        backend->daylight_percent[zone_id] = 20U;
        backend->motion_detected[zone_id] = false;
        backend->sensor_healthy[zone_id] = true;
        backend->led_brightness_percent[zone_id] = 0U;
    }

    return 0;
}

void io_backend_destroy(IoBackend *backend)
{
    if (backend != NULL) {
        pthread_mutex_destroy(&backend->lock);
    }
}

int io_backend_read_sensor(
        IoBackend *backend,
        uint8_t zone_id,
        uint64_t timestamp_ms,
        SensorSample *sample)
{
    if (backend == NULL ||
        sample == NULL ||
        !valid_zone(zone_id)) {
        errno = EINVAL;
        return -1;
    }

    if (lock_backend(backend) != 0) {
        return -1;
    }

    sample->zone_id = zone_id;
    sample->daylight_percent =
            backend->daylight_percent[zone_id];
    sample->motion_detected =
            backend->motion_detected[zone_id];
    sample->sensor_healthy =
            backend->sensor_healthy[zone_id];
    sample->timestamp_ms = timestamp_ms;

    return unlock_backend(backend);
}

int io_backend_set_brightness(
        IoBackend *backend,
        uint8_t zone_id,
        uint8_t brightness_percent)
{
    if (backend == NULL || !valid_zone(zone_id)) {
        errno = EINVAL;
        return -1;
    }

    if (lock_backend(backend) != 0) {
        return -1;
    }

    backend->led_brightness_percent[zone_id] =
            clamp_percent(brightness_percent);

    return unlock_backend(backend);
}

int io_backend_set_daylight(
        IoBackend *backend,
        uint8_t zone_id,
        uint8_t daylight_percent)
{
    if (backend == NULL || !valid_zone(zone_id)) {
        errno = EINVAL;
        return -1;
    }

    if (lock_backend(backend) != 0) {
        return -1;
    }

    backend->daylight_percent[zone_id] =
            clamp_percent(daylight_percent);

    return unlock_backend(backend);
}

int io_backend_set_motion(
        IoBackend *backend,
        uint8_t zone_id,
        bool detected)
{
    if (backend == NULL || !valid_zone(zone_id)) {
        errno = EINVAL;
        return -1;
    }

    if (lock_backend(backend) != 0) {
        return -1;
    }

    backend->motion_detected[zone_id] = detected;

    return unlock_backend(backend);
}

int io_backend_set_sensor_health(
        IoBackend *backend,
        uint8_t zone_id,
        bool healthy)
{
    if (backend == NULL || !valid_zone(zone_id)) {
        errno = EINVAL;
        return -1;
    }

    if (lock_backend(backend) != 0) {
        return -1;
    }

    backend->sensor_healthy[zone_id] = healthy;

    return unlock_backend(backend);
}
