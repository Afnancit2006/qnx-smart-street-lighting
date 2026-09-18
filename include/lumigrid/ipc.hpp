#pragma once

#include <cstddef>
#include <cstdint>
#include <mqueue.h>
#include <string>
#include <vector>

namespace lumigrid {

enum class ReceiveResult {
    Received,
    Timeout,
    Interrupted,
    Error,
};

mqd_t create_queue(const std::string& name,
                   std::size_t message_size,
                   long maximum_messages = 10);
mqd_t open_queue(const std::string& name, int flags);
bool send_with_timeout(mqd_t queue,
                       const void* data,
                       std::size_t size,
                       unsigned int priority,
                       std::uint32_t timeout_ms = 20);
ReceiveResult receive_with_timeout(mqd_t queue,
                                   void* data,
                                   std::size_t size,
                                   unsigned int* priority,
                                   std::uint32_t timeout_ms);
void close_queue(mqd_t queue) noexcept;
void unlink_queues(const std::vector<std::string>& names) noexcept;

} // namespace lumigrid
