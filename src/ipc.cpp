#include "lumigrid/ipc.hpp"

#include "lumigrid/clock.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>

namespace lumigrid {

mqd_t create_queue(const std::string& name,
                   std::size_t message_size,
                   long maximum_messages) {
    mq_attr attributes{};
    attributes.mq_flags = 0;
    attributes.mq_maxmsg = maximum_messages;
    attributes.mq_msgsize = static_cast<long>(message_size);
    attributes.mq_curmsgs = 0;

    const mqd_t queue = mq_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR,
                                S_IRUSR | S_IWUSR, &attributes);
    if (queue == static_cast<mqd_t>(-1)) {
        throw std::runtime_error("mq_open(create " + name + "): " +
                                 std::strerror(errno));
    }
    return queue;
}

mqd_t open_queue(const std::string& name, int flags) {
    const mqd_t queue = mq_open(name.c_str(), flags);
    if (queue == static_cast<mqd_t>(-1)) {
        throw std::runtime_error("mq_open(" + name + "): " +
                                 std::strerror(errno));
    }
    return queue;
}

bool send_with_timeout(mqd_t queue,
                       const void* data,
                       std::size_t size,
                       unsigned int priority,
                       std::uint32_t timeout_ms) {
    const timespec deadline = realtime_after_ms(timeout_ms);
    int result = -1;
    do {
        result = mq_timedsend(queue, static_cast<const char*>(data), size,
                              priority, &deadline);
    } while (result == -1 && errno == EINTR);
    return result == 0;
}

ReceiveResult receive_with_timeout(mqd_t queue,
                                   void* data,
                                   std::size_t size,
                                   unsigned int* priority,
                                   std::uint32_t timeout_ms) {
    const timespec deadline = realtime_after_ms(timeout_ms);
    const ssize_t received = mq_timedreceive(
        queue, static_cast<char*>(data), size, priority, &deadline);
    if (received >= 0) {
        return static_cast<std::size_t>(received) == size
                   ? ReceiveResult::Received
                   : ReceiveResult::Error;
    }
    if (errno == ETIMEDOUT || errno == EAGAIN) {
        return ReceiveResult::Timeout;
    }
    if (errno == EINTR) {
        return ReceiveResult::Interrupted;
    }
    return ReceiveResult::Error;
}

void close_queue(mqd_t queue) noexcept {
    if (queue != static_cast<mqd_t>(-1)) {
        mq_close(queue);
    }
}

void unlink_queues(const std::vector<std::string>& names) noexcept {
    for (const std::string& name : names) {
        mq_unlink(name.c_str());
    }
}

} // namespace lumigrid
