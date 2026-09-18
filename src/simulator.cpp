#include "lumigrid/simulator.hpp"

#include "lumigrid/clock.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace lumigrid {

SensorSimulator::SensorSimulator(std::size_t zone_count, std::string scenario)
    : zone_count_(std::min(zone_count, kMaxZones)),
      scenario_(std::move(scenario)) {}

SimulatedSample SensorSimulator::sample(std::size_t zone,
                                        double elapsed_seconds) const noexcept {
    SimulatedSample result{};
    if (zone >= zone_count_) {
        result.valid = false;
        return result;
    }

    const double deterministic_noise =
        2.5 * std::sin(elapsed_seconds * 1.7 + static_cast<double>(zone));
    result.lux = std::max(4.0, 138.0 - 3.8 * elapsed_seconds +
                               deterministic_noise + 1.5 * zone);
    result.visibility = 0.95;

    if (scenario_ == "daylight") {
        result.lux = 520.0 + 15.0 * std::sin(elapsed_seconds);
    } else if (scenario_ == "quiet") {
        result.lux = 18.0 + deterministic_noise;
        const double phase = std::fmod(elapsed_seconds + 17.0, 28.0);
        result.occupied = zone == 0U && phase < 1.2;
    } else if (scenario_ == "rush") {
        result.lux = 16.0 + deterministic_noise;
        const double phase = std::fmod(
            elapsed_seconds + 1.35 * static_cast<double>(zone), 6.0);
        result.occupied = phase < 2.0;
    } else {
        const double forward_start = 2.0 + 1.8 * static_cast<double>(zone);
        const double reverse_start =
            13.0 + 1.5 * static_cast<double>(zone_count_ - 1U - zone);
        result.occupied =
            (elapsed_seconds >= forward_start &&
             elapsed_seconds < forward_start + 1.5) ||
            (elapsed_seconds >= reverse_start &&
             elapsed_seconds < reverse_start + 1.2);

        if (elapsed_seconds >= 17.0 && elapsed_seconds < 23.0) {
            result.visibility = 0.34;
        }
        const double fault_start = scenario_ == "fault" ? 4.0 : 24.0;
        const double fault_finish = scenario_ == "fault" ? 8.0 : 28.0;
        if ((scenario_ == "fault" || scenario_ == "demo") &&
            zone == std::min<std::size_t>(2U, zone_count_ - 1U) &&
            elapsed_seconds >= fault_start && elapsed_seconds < fault_finish) {
            result.valid = false;
        }
    }

    if (forced_occupancy_until_ns_[zone] > monotonic_ns()) {
        result.occupied = true;
    }
    if (forced_fault_[zone]) {
        result.valid = false;
    }
    if (lux_override_active_) {
        result.lux = lux_override_;
    }
    result.lux = std::max(0.0, result.lux);
    return result;
}

void SensorSimulator::apply_command(const IpcMessage& command,
                                    std::uint64_t now_ns) noexcept {
    const std::size_t zone = command.zone;
    switch (command.type) {
    case MessageType::TriggerOccupancy:
        if (zone < zone_count_) {
            const std::uint64_t duration_ms =
                command.data0 > 0 ? static_cast<std::uint64_t>(command.data0)
                                  : 2000ULL;
            forced_occupancy_until_ns_[zone] =
                now_ns + duration_ms * 1'000'000ULL;
        }
        break;
    case MessageType::SetSensorFault:
        if (zone < zone_count_) {
            forced_fault_[zone] = command.data0 != 0;
        }
        break;
    case MessageType::SetLuxOverride:
        lux_override_active_ = true;
        lux_override_ = std::max(0.0, command.value);
        break;
    case MessageType::ResetSensorOverrides:
        forced_fault_.fill(false);
        forced_occupancy_until_ns_.fill(0);
        lux_override_active_ = false;
        break;
    default:
        break;
    }
}

} // namespace lumigrid
