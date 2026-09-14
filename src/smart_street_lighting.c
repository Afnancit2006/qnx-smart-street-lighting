#include <mqueue.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_context.h"
#include "app_tasks.h"
#include "cli.h"
#include "energy_monitor.h"
#include "event_queue.h"
#include "lighting_controller.h"
#include "prediction_engine.h"

#define PRIORITY_CLI 10
#define PRIORITY_PREDICTION_TASK 15
#define PRIORITY_SENSOR_TASK 25
#define PRIORITY_CONTROLLER_TASK 35
#define PRIORITY_EMERGENCY_TASK 40

static int initialize_context(AppContext *context)
{
    int result;

    memset(context, 0, sizeof(*context));
    context->event_queue = (mqd_t)-1;

    result = pthread_mutex_init(
            &context->state_lock,
            NULL);

    if (result != 0) {
        fprintf(
                stderr,
                "State mutex initialization failed: %s\n",
                strerror(result));
        return -1;
    }

    result = pthread_mutex_init(
            &context->prediction_lock,
            NULL);

    if (result != 0) {
        fprintf(
                stderr,
                "Prediction mutex initialization failed: %s\n",
                strerror(result));

        pthread_mutex_destroy(&context->state_lock);
        return -1;
    }

    result = pthread_mutex_init(
            &context->control_lock,
            NULL);

    if (result != 0) {
        fprintf(
                stderr,
                "Control mutex initialization failed: %s\n",
                strerror(result));

        pthread_mutex_destroy(&context->prediction_lock);
        pthread_mutex_destroy(&context->state_lock);
        return -1;
    }

    result = pthread_cond_init(
            &context->emergency_condition,
            NULL);

    if (result != 0) {
        fprintf(
                stderr,
                "Emergency condition initialization failed: %s\n",
                strerror(result));

        pthread_mutex_destroy(&context->control_lock);
        pthread_mutex_destroy(&context->prediction_lock);
        pthread_mutex_destroy(&context->state_lock);
        return -1;
    }

    if (io_backend_init(&context->io) == -1) {
        perror("I/O backend initialization failed");

        pthread_cond_destroy(&context->emergency_condition);
        pthread_mutex_destroy(&context->control_lock);
        pthread_mutex_destroy(&context->prediction_lock);
        pthread_mutex_destroy(&context->state_lock);
        return -1;
    }

    if (event_queue_create(&context->event_queue) == -1) {
        perror("Event queue creation failed");

        io_backend_destroy(&context->io);
        pthread_cond_destroy(&context->emergency_condition);
        pthread_mutex_destroy(&context->control_lock);
        pthread_mutex_destroy(&context->prediction_lock);
        pthread_mutex_destroy(&context->state_lock);
        return -1;
    }

    prediction_engine_init(&context->predictor);
    lighting_controller_init(context->zones);
    energy_monitor_init(&context->energy);

    context->running = true;
    context->emergency_active = false;
    context->emergency_deadline_ms = 0U;
    context->emergency_request_pending = false;
    context->emergency_requested_active = false;

    return 0;
}

static void destroy_context(AppContext *context)
{
    event_queue_close(context->event_queue);
    event_queue_destroy();

    io_backend_destroy(&context->io);

    pthread_cond_destroy(&context->emergency_condition);
    pthread_mutex_destroy(&context->control_lock);
    pthread_mutex_destroy(&context->prediction_lock);
    pthread_mutex_destroy(&context->state_lock);
}

static int create_task(
        pthread_t *thread,
        void *(*task_function)(void *),
        AppContext *context,
        const char *task_name)
{
    int result = pthread_create(
            thread,
            NULL,
            task_function,
            context);

    if (result != 0) {
        fprintf(
                stderr,
                "Unable to create %s: %s\n",
                task_name,
                strerror(result));
        return -1;
    }

    return 0;
}

static void set_task_priority(
        pthread_t thread,
        int priority,
        const char *task_name)
{
    struct sched_param scheduling;
    int result;

    memset(&scheduling, 0, sizeof(scheduling));
    scheduling.sched_priority = priority;

    result = pthread_setschedparam(
            thread,
            SCHED_FIFO,
            &scheduling);

    if (result != 0) {
        fprintf(
                stderr,
                "[WARNING] Could not set %s priority: %s. "
                "Continuing with the default priority.\n",
                task_name,
                strerror(result));
    }
}

int main(void)
{
    AppContext context;

    pthread_t controller_thread;
    pthread_t emergency_thread;
    pthread_t sensor_thread;
    pthread_t prediction_thread;
    pthread_t cli_thread;

    bool controller_started = false;
    bool emergency_started = false;
    bool sensor_started = false;
    bool prediction_started = false;

    int exit_status = EXIT_FAILURE;

    printf("\n");
    printf("============================================\n");
    printf("  QNX Smart Street Lighting System\n");
    printf("  Event-Driven Predictive Control Demo\n");
    printf("============================================\n");

    if (initialize_context(&context) == -1) {
        fprintf(stderr, "Application initialization failed.\n");
        return EXIT_FAILURE;
    }

    if (create_task(
                &controller_thread,
                lighting_controller_task,
                &context,
                "lighting controller task") == -1) {
        goto shutdown;
    }

    controller_started = true;

    set_task_priority(
            controller_thread,
            PRIORITY_CONTROLLER_TASK,
            "lighting controller task");

    if (create_task(
                &emergency_thread,
                emergency_task,
                &context,
                "emergency task") == -1) {
        goto shutdown;
    }

    emergency_started = true;

    set_task_priority(
            emergency_thread,
            PRIORITY_EMERGENCY_TASK,
            "emergency task");

    if (create_task(
                &sensor_thread,
                sensor_task,
                &context,
                "sensor task") == -1) {
        goto shutdown;
    }

    sensor_started = true;

    set_task_priority(
            sensor_thread,
            PRIORITY_SENSOR_TASK,
            "sensor task");

    if (create_task(
                &prediction_thread,
                prediction_task,
                &context,
                "prediction task") == -1) {
        goto shutdown;
    }

    prediction_started = true;

    set_task_priority(
            prediction_thread,
            PRIORITY_PREDICTION_TASK,
            "prediction task");

    if (create_task(
                &cli_thread,
                cli_task,
                &context,
                "CLI task") == -1) {
        goto shutdown;
    }

    set_task_priority(
            cli_thread,
            PRIORITY_CLI,
            "CLI task");

    printf("\nAll QNX application tasks started successfully.\n");
    printf("The simulated sensor task updates every second.\n");
    printf("The prediction task updates every five seconds.\n\n");
    fflush(stdout);

    pthread_join(cli_thread, NULL);
    exit_status = EXIT_SUCCESS;

shutdown:
    app_request_stop(&context);

    if (sensor_started) {
        pthread_join(sensor_thread, NULL);
    }

    if (prediction_started) {
        pthread_join(prediction_thread, NULL);
    }

    if (emergency_started) {
        pthread_join(emergency_thread, NULL);
    }

    if (controller_started) {
        pthread_join(controller_thread, NULL);
    }

    destroy_context(&context);

    printf("\nSmart Street Lighting application stopped.\n");

    return exit_status;
}
