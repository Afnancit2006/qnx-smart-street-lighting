#ifndef IO_BACKEND_H_
#define IO_BACKEND_H_

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "lighting_types.h"

/*
 * Stores simulated inputs now.
 * Later, these functions will communicate with Pi GPIO, ADC and PWM.
 */
typedef struct {
    pthread_mutex_t lock;

    uint8_t daylight_percent[ZONE_COUNT];
    bool motion_detected[ZONE_COUNT];
    bool sensor_healthy[ZONE_COUNT];

    uint8_t led_brightness_percent[ZONE_COUNT];
} IoBackend;

int io_backend_init(IoBackend *backend);
void io_backend_destroy(IoBackend *backend);

int io_backend_read_sensor(
        IoBackend *backend,
        uint8_t zone_id,
        uint64_t timestamp_ms,
        SensorSample *sample);

int io_backend_set_brightness(
        IoBackend *backend,
        uint8_t zone_id,
        uint8_t brightness_percent);

/* Functions used by the software-only CLI simulator. */
int io_backend_set_daylight(
        IoBackend *backend,
        uint8_t zone_id,
        uint8_t daylight_percent);

int io_backend_set_motion(
        IoBackend *backend,
        uint8_t zone_id,
        bool detected);

int io_backend_set_sensor_health(
        IoBackend *backend,
        uint8_t zone_id,
        bool healthy);

#endif /* IO_BACKEND_H_ */
