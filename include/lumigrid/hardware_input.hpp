#pragma once

#include <array>
#include <string>

namespace lumigrid {

constexpr std::size_t kHardwareZoneCount = 2;

struct HardwareZoneSample {
    int ldr_raw{0};
    bool dark{false};
    bool object_detected{false};
    int led_percent{0};
};

struct HardwareFrame {
    std::array<HardwareZoneSample, kHardwareZoneCount> zones{};
    bool emergency{false};
};

// Parses the exact comma-separated record emitted by the two-zone Nano sketch.
// The destination is changed only when the complete record is valid.
bool parse_hardware_frame(const std::string& line,
                          HardwareFrame& destination) noexcept;

} // namespace lumigrid
