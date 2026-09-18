#include "app_tasks.h"

#include <errno.h>
#include <stdio.h>
#include <time.h>

#include "event_queue.h"
#include "lighting_controller.h"

#define SENSOR_PERIOD_MS 1000U
#define PREDICTION_PERIOD_MS 5000U
#define WAIT_SLICE_MS 100U

#define MILLISECONDS_PER_SECOND 1000ULL
#define NANOSECONDS_PER_MILLISECOND 1000000L

static void sleep_ms(uint32_t milliseconds)
{
    struct timespec requested;

    requested.tv_sec = (time_t)(milliseconds / 1000U);
    requested.tv_nsec =
            (long)(milliseconds % 1000U) *
            NANOSECONDS_PER_MILLISECOND;

    while (nanosleep(&requested, &requested) == -1 &&
           errno == EINTR) {
        /* Continue sleeping for the remaining duration. */
    }
}

static void task_wait(AppContext *context, uint32_t milliseconds)
{
    uint32_t remaining_ms = milliseconds;

    while (remaining_ms > 0U && app_is_running(context)) {
        uint32_t slice_ms =
                remaining_ms > WAIT_SLICE_MS ?
                WAIT_SLICE_MS :
                remaining_ms;

        sleep_ms(slice_ms);
        remaining_ms -= slice_ms;
    }
}

uint64_t app_time_ms(void)
{
    struct timespec current_time;

    if (clock_gettime(CLOCK_MONOTONIC, &current_time) == -1) {
        return 0U;
    }

    return ((uint64_t)current_time.tv_sec *
            MILLISECONDS_PER_SECOND) +
           ((uint64_t)current_time.tv_nsec /
            NANOSECONDS_PER_MILLISECOND);
}

bool app_is_running(AppContext *context)
{
    bool running;

    if (context == NULL) {
        return false;
    }

    pthread_mutex_lock(&context->control_lock);
    running = context->running;
    pthread_mutex_unlock(&context->control_lock);

    return running;
}

void app_request_stop(AppContext *context)
{
    bool send_shutdown = false;

    if (context == NULL) {
        return;
    }

    pthread_mutex_lock(&context->control_lock);

    if (context->running) {
        context->running = false;
        send_shutdown = true;
    }

    pthread_cond_broadcast(&context->emergency_condition);
    pthread_mutex_unlock(&context->control_lock);

    if (send_shutdown) {
        LightingEvent event = {0};

        event.type = EVENT_SHUTDOWN;

        if (event_queue_send(
                    context->event_queue,
                    &event,
                    PRIORITY_EMERGENCY) == -1) {
            perror("Failed to send shutdown event");
        }
    }
}

void app_request_emergency(AppContext *context, bool active)
{
    if (context == NULL) {
        return;
    }

    pthread_mutex_lock(&context->control_lock);

    if (context->running) {
        context->emergency_requested_active = active;
        context->emergency_request_pending = true;
        pthread_cond_signal(&context->emergency_condition);
    }

    pthread_mutex_unlock(&context->control_lock);
}

void *sensor_task(void *argument)
{
    AppContext *context = (AppContext *)argument;
    uint8_t zone_id;

    while (app_is_running(context)) {
        uint64_t timestamp_ms = app_time_ms();

        for (zone_id = 0U; zone_id < ZONE_COUNT; ++zone_id) {
            LightingEvent event = {0};

            event.type = EVENT_SENSOR;

            if (io_backend_read_sensor(
                        &context->io,
                        zone_id,
                        timestamp_ms,
                        &event.data.sensor) == 0) {

                if (event_queue_send(
                            context->event_queue,
                            &event,
                            PRIORITY_SENSOR) == -1 &&
                    app_is_running(context)) {
                    perror("Sensor task queue send failed");
                }
            }
        }

        task_wait(context, SENSOR_PERIOD_MS);
    }

    return NULL;
}

void *prediction_task(void *argument)
{
    AppContext *context = (AppContext *)argument;
    uint8_t zone_id;

    while (app_is_running(context)) {
        time_t calendar_time = time(NULL);
        struct tm local_time;
        uint8_t hour = 0U;
        uint64_t timestamp_ms = app_time_ms();

        if (localtime_r(&calendar_time, &local_time) != NULL) {
            hour = (uint8_t)local_time.tm_hour;
        }

        for (zone_id = 0U; zone_id < ZONE_COUNT; ++zone_id) {
            LightingEvent event = {0};

            event.type = EVENT_PREDICTION;
            event.data.prediction.zone_id = zone_id;
            event.data.prediction.timestamp_ms = timestamp_ms;

            pthread_mutex_lock(&context->prediction_lock);

            event.data.prediction.demand_percent =
                    prediction_engine_calculate(
                            &context->predictor,
                            zone_id,
                            hour);

            pthread_mutex_unlock(&context->prediction_lock);

            if (event_queue_send(
                        context->event_queue,
                        &event,
                        PRIORITY_PREDICTION) == -1 &&
                app_is_running(context)) {
                perror("Prediction task queue send failed");
            }
        }

        task_wait(context, PREDICTION_PERIOD_MS);
    }

    return NULL;
}

void *lighting_controller_task(void *argument)
{
    AppContext *context = (AppContext *)argument;
    LightingEvent event;
    unsigned int priority;
    uint8_t zone_id;

    printf("[CONTROLLER] Event-driven controller started.\n");
    fflush(stdout);

    while (true) {
        LightMode previous_mode[ZONE_COUNT];
        uint8_t previous_brightness[ZONE_COUNT];
        uint64_t current_time_ms;

        if (event_queue_receive(
                    context->event_queue,
                    &event,
                    &priority) == -1) {

            if (errno == EINTR) {
                continue;
            }

            if (!app_is_running(context)) {
                break;
            }

            perror("Controller queue receive failed");
            continue;
        }

        if (event.type == EVENT_SHUTDOWN) {
            break;
        }

        current_time_ms = app_time_ms();

        pthread_mutex_lock(&context->state_lock);

        for (zone_id = 0U; zone_id < ZONE_COUNT; ++zone_id) {
            previous_mode[zone_id] = context->zones[zone_id].mode;
            previous_brightness[zone_id] =
                    context->zones[zone_id].brightness_percent;
        }

        energy_monitor_update(
                &context->energy,
                context->zones,
                current_time_ms);

        switch (event.type) {
        case EVENT_SENSOR:
        {
            bool new_motion = false;
            uint8_t event_zone = event.data.sensor.zone_id;

            if (event_zone < ZONE_COUNT) {
                new_motion =
                        event.data.sensor.motion_detected &&
                        !context->zones[event_zone].motion_detected;
            }

            lighting_controller_apply_sensor(
                    context->zones,
                    &event.data.sensor);

            if (new_motion) {
                pthread_mutex_lock(&context->prediction_lock);

                prediction_engine_record_motion(
                        &context->predictor,
                        event_zone);

                pthread_mutex_unlock(&context->prediction_lock);
            }

            break;
        }

        case EVENT_PREDICTION:
            lighting_controller_apply_prediction(
                    context->zones,
                    &event.data.prediction);
            break;

        case EVENT_COMMAND:
            lighting_controller_apply_command(
                    context->zones,
                    &event.data.command);
            break;

        case EVENT_EMERGENCY:
            context->emergency_active =
                    event.data.emergency.active;

            if (context->emergency_active) {
                uint32_t timeout_seconds =
                        event.data.emergency.timeout_seconds;

                if (timeout_seconds == 0U) {
                    timeout_seconds =
                            EMERGENCY_TIMEOUT_SECONDS;
                }

                context->emergency_deadline_ms =
                        current_time_ms +
                        ((uint64_t)timeout_seconds *
                         MILLISECONDS_PER_SECOND);
            } else {
                context->emergency_deadline_ms = 0U;
            }

            break;

        default:
            break;
        }

        if (context->emergency_active &&
            context->emergency_deadline_ms != 0U &&
            current_time_ms >= context->emergency_deadline_ms) {

            context->emergency_active = false;
            context->emergency_deadline_ms = 0U;

            printf("[EMERGENCY] Automatic timeout reached.\n");
        }

        lighting_controller_update(
                context->zones,
                context->emergency_active,
                current_time_ms);

        for (zone_id = 0U; zone_id < ZONE_COUNT; ++zone_id) {
            io_backend_set_brightness(
                    &context->io,
                    zone_id,
                    context->zones[zone_id].brightness_percent);

            if (previous_mode[zone_id] !=
                    context->zones[zone_id].mode ||
                previous_brightness[zone_id] !=
                    context->zones[zone_id].brightness_percent) {

                printf(
                        "[LIGHT] Zone %u -> %-12s %3u%%\n",
                        (unsigned int)(zone_id + 1U),
                        lighting_mode_name(
                                context->zones[zone_id].mode),
                        (unsigned int)
                                context->zones[zone_id]
                                        .brightness_percent);
            }
        }

        fflush(stdout);
        pthread_mutex_unlock(&context->state_lock);
    }

    pthread_mutex_lock(&context->state_lock);

    energy_monitor_update(
            &context->energy,
            context->zones,
            app_time_ms());

    for (zone_id = 0U; zone_id < ZONE_COUNT; ++zone_id) {
        io_backend_set_brightness(
                &context->io,
                zone_id,
                0U);
    }

    pthread_mutex_unlock(&context->state_lock);

    printf("[CONTROLLER] Controller stopped.\n");
    fflush(stdout);

    return NULL;
}

void *emergency_task(void *argument)
{
    AppContext *context = (AppContext *)argument;

    while (true) {
        bool requested_active;
        LightingEvent event = {0};

        pthread_mutex_lock(&context->control_lock);

        while (!context->emergency_request_pending &&
               context->running) {
            pthread_cond_wait(
                    &context->emergency_condition,
                    &context->control_lock);
        }

        if (!context->running) {
            pthread_mutex_unlock(&context->control_lock);
            break;
        }

        requested_active =
                context->emergency_requested_active;

        context->emergency_request_pending = false;

        pthread_mutex_unlock(&context->control_lock);

        event.type = EVENT_EMERGENCY;
        event.data.emergency.active = requested_active;
        event.data.emergency.timeout_seconds =
                requested_active ?
                EMERGENCY_TIMEOUT_SECONDS :
                0U;
        event.data.emergency.timestamp_ms = app_time_ms();

        if (event_queue_send(
                    context->event_queue,
                    &event,
                    PRIORITY_EMERGENCY) == -1 &&
            app_is_running(context)) {
            perror("Emergency task queue send failed");
        } else {
            printf(
                    "[EMERGENCY] Override request: %s\n",
                    requested_active ? "ON" : "OFF");
            fflush(stdout);
        }
    }

    return NULL;
}
