#pragma once

#include <cerrno>
#include <cstdint>
#include <ctime>

namespace lumigrid {

inline std::uint64_t monotonic_ns() noexcept {
    timespec value{};
    clock_gettime(CLOCK_MONOTONIC, &value);
    return static_cast<std::uint64_t>(value.tv_sec) * 1'000'000'000ULL +
           static_cast<std::uint64_t>(value.tv_nsec);
}

inline timespec realtime_after_ms(std::uint32_t timeout_ms) noexcept {
    timespec value{};
    clock_gettime(CLOCK_REALTIME, &value);
    value.tv_sec += static_cast<time_t>(timeout_ms / 1000U);
    value.tv_nsec += static_cast<long>(timeout_ms % 1000U) * 1'000'000L;
    if (value.tv_nsec >= 1'000'000'000L) {
        ++value.tv_sec;
        value.tv_nsec -= 1'000'000'000L;
    }
    return value;
}

inline timespec ns_to_timespec(std::uint64_t value_ns) noexcept {
    timespec value{};
    value.tv_sec = static_cast<time_t>(value_ns / 1'000'000'000ULL);
    value.tv_nsec = static_cast<long>(value_ns % 1'000'000'000ULL);
    return value;
}

inline int sleep_until_monotonic(std::uint64_t deadline_ns) noexcept {
    const timespec deadline = ns_to_timespec(deadline_ns);
    int result = 0;
    do {
        result = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, nullptr);
    } while (result == EINTR);
    return result;
}

} // namespace lumigrid
