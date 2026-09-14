#ifndef APP_TASKS_H_
#define APP_TASKS_H_

#include <stdbool.h>
#include <stdint.h>

#include "app_context.h"

#define EMERGENCY_TIMEOUT_SECONDS 30U

uint64_t app_time_ms(void);

bool app_is_running(AppContext *context);
void app_request_stop(AppContext *context);
void app_request_emergency(AppContext *context, bool active);

void *sensor_task(void *argument);
void *prediction_task(void *argument);
void *lighting_controller_task(void *argument);
void *emergency_task(void *argument);

#endif
