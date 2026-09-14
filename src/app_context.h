#ifndef APP_CONTEXT_H_
#define APP_CONTEXT_H_

#include <mqueue.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "energy_monitor.h"
#include "io_backend.h"
#include "lighting_types.h"
#include "prediction_engine.h"

typedef struct {
    mqd_t event_queue;

    IoBackend io;
    PredictionEngine predictor;
    EnergyMonitor energy;
    ZoneState zones[ZONE_COUNT];

    pthread_mutex_t state_lock;
    pthread_mutex_t prediction_lock;
    pthread_mutex_t control_lock;
    pthread_cond_t emergency_condition;

    bool running;
    bool emergency_active;
    uint64_t emergency_deadline_ms;

    bool emergency_request_pending;
    bool emergency_requested_active;
} AppContext;

#endif
