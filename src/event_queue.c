#include "event_queue.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>

#define EVENT_QUEUE_MAX_MESSAGES 16L

int event_queue_create(mqd_t *queue)
{
    struct mq_attr attributes;
    mqd_t created_queue;

    if (queue == NULL) {
        errno = EINVAL;
        return -1;
    }

    /*
     * Remove a queue left behind by an earlier execution
     * of the application.
     */
    mq_unlink(EVENT_QUEUE_NAME);

    memset(&attributes, 0, sizeof(attributes));
    attributes.mq_flags = 0;
    attributes.mq_maxmsg = EVENT_QUEUE_MAX_MESSAGES;
    attributes.mq_msgsize = sizeof(LightingEvent);

    created_queue = mq_open(
            EVENT_QUEUE_NAME,
            O_CREAT | O_RDWR,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP,
            &attributes);

    if (created_queue == (mqd_t)-1) {
        return -1;
    }

    *queue = created_queue;
    return 0;
}

int event_queue_send(
        mqd_t queue,
        const LightingEvent *event,
        unsigned int priority)
{
    if (queue == (mqd_t)-1 || event == NULL) {
        errno = EINVAL;
        return -1;
    }

    return mq_send(
            queue,
            (const char *)event,
            sizeof(*event),
            priority);
}

int event_queue_receive(
        mqd_t queue,
        LightingEvent *event,
        unsigned int *priority)
{
    ssize_t received_bytes;

    if (queue == (mqd_t)-1 || event == NULL) {
        errno = EINVAL;
        return -1;
    }

    received_bytes = mq_receive(
            queue,
            (char *)event,
            sizeof(*event),
            priority);

    if (received_bytes == -1) {
        return -1;
    }

    if ((size_t)received_bytes != sizeof(*event)) {
        errno = EMSGSIZE;
        return -1;
    }

    return 0;
}

void event_queue_close(mqd_t queue)
{
    if (queue != (mqd_t)-1) {
        mq_close(queue);
    }
}

void event_queue_destroy(void)
{
    mq_unlink(EVENT_QUEUE_NAME);
}
