#pragma once

#include "lumigrid/model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace lumigrid {

struct SimulatedSample {
    double lux{0.0};
    double visibility{1.0};
    bool occupied{false};
    bool valid{true};
};

class SensorSimulator {
public:
    SensorSimulator(std::size_t zone_count, std::string scenario);

    SimulatedSample sample(std::size_t zone, double elapsed_seconds) const noexcept;
    void apply_command(const IpcMessage& command, std::uint64_t now_ns) noexcept;

private:
    std::size_t zone_count_;
    std::string scenario_;
    std::array<bool, kMaxZones> forced_fault_{};
    std::array<std::uint64_t, kMaxZones> forced_occupancy_until_ns_{};
    bool lux_override_active_{false};
    double lux_override_{0.0};
};

} // namespace lumigrid
