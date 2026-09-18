#pragma once

#include "lumigrid/model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace lumigrid {

class HistoricalProfile {
public:
    HistoricalProfile();

    bool load_csv(const std::string& path, std::size_t zone_count);
    double probability(std::size_t zone, int hour) const noexcept;

private:
    std::array<std::array<double, kMaxZones>, 24> values_{};
};

struct PredictionResult {
    double probability{0.0};
    double confidence{0.0};
};

class TrafficPredictor {
public:
    explicit TrafficPredictor(std::size_t zone_count,
                              HistoricalProfile profile = HistoricalProfile{});

    PredictionResult observe(std::size_t zone,
                             bool occupied,
                             int local_hour) noexcept;

private:
    std::size_t zone_count_;
    HistoricalProfile profile_;
    std::array<double, kMaxZones> occupancy_ewma_{};
    std::array<double, kMaxZones> previous_ewma_{};
    std::array<std::uint32_t, kMaxZones> samples_{};
};

} // namespace lumigrid
