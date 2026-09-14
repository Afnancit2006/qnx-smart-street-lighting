#ifndef EVENT_QUEUE_H_
#define EVENT_QUEUE_H_

#include <mqueue.h>

#include "lighting_types.h"

#define EVENT_QUEUE_NAME "/smart_lighting_events"

/* QNX message priorities: larger number means higher priority. */
#define PRIORITY_PREDICTION  5U
#define PRIORITY_SENSOR     10U
#define PRIORITY_COMMAND    20U
#define PRIORITY_EMERGENCY  30U

/* Creates the central event queue used by all tasks. */
int event_queue_create(mqd_t *queue);

/* Sends one lighting event with the selected priority. */
int event_queue_send(
        mqd_t queue,
        const LightingEvent *event,
        unsigned int priority);

/* Waits until the next event is received. */
int event_queue_receive(
        mqd_t queue,
        LightingEvent *event,
        unsigned int *priority);

/* Closes and removes the queue. */
void event_queue_close(mqd_t queue);
void event_queue_destroy(void);

#endif /* EVENT_QUEUE_H_ */
