#include "lumigrid/predictor.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

namespace lumigrid {

namespace {

double clamp_probability(double value) noexcept {
    return std::max(0.0, std::min(1.0, value));
}

double default_hour_probability(int hour) noexcept {
    if (hour >= 6 && hour <= 8) {
        return 0.62;
    }
    if (hour >= 9 && hour <= 16) {
        return 0.30;
    }
    if (hour >= 17 && hour <= 21) {
        return 0.72;
    }
    if (hour == 22 || hour == 23) {
        return 0.35;
    }
    return 0.12;
}

} // namespace

HistoricalProfile::HistoricalProfile() {
    for (int hour = 0; hour < 24; ++hour) {
        for (std::size_t zone = 0; zone < kMaxZones; ++zone) {
            const double zone_bias = (zone % 3U == 0U) ? 0.05 :
                                     (zone % 3U == 2U) ? -0.04 : 0.0;
            values_[static_cast<std::size_t>(hour)][zone] =
                clamp_probability(default_hour_probability(hour) + zone_bias);
        }
    }
}

bool HistoricalProfile::load_csv(const std::string& path,
                                 std::size_t zone_count) {
    std::ifstream input(path);
    if (!input) {
        return false;
    }

    std::string line;
    std::size_t valid_rows = 0;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::stringstream stream(line);
        std::string field;
        if (!std::getline(stream, field, ',')) {
            continue;
        }

        int hour = -1;
        try {
            hour = std::stoi(field);
        } catch (...) {
            continue; // Header row.
        }
        if (hour < 0 || hour > 23) {
            continue;
        }

        bool complete = true;
        for (std::size_t zone = 0; zone < zone_count; ++zone) {
            if (!std::getline(stream, field, ',')) {
                complete = false;
                break;
            }
            try {
                values_[static_cast<std::size_t>(hour)][zone] =
                    clamp_probability(std::stod(field));
            } catch (...) {
                complete = false;
                break;
            }
        }
        if (complete) {
            ++valid_rows;
        }
    }
    return valid_rows == 24U;
}

double HistoricalProfile::probability(std::size_t zone, int hour) const noexcept {
    const int normalized_hour = ((hour % 24) + 24) % 24;
    return values_[static_cast<std::size_t>(normalized_hour)]
                  [std::min(zone, kMaxZones - 1U)];
}

TrafficPredictor::TrafficPredictor(std::size_t zone_count,
                                   HistoricalProfile profile)
    : zone_count_(std::min(zone_count, kMaxZones)),
      profile_(std::move(profile)) {}

PredictionResult TrafficPredictor::observe(std::size_t zone,
                                           bool occupied,
                                           int local_hour) noexcept {
    if (zone >= zone_count_) {
        return {};
    }

    constexpr double kAlpha = 0.25;
    previous_ewma_[zone] = occupancy_ewma_[zone];
    occupancy_ewma_[zone] =
        (1.0 - kAlpha) * occupancy_ewma_[zone] +
        kAlpha * (occupied ? 1.0 : 0.0);
    ++samples_[zone];

    double neighbour_signal = 0.0;
    if (zone > 0U) {
        neighbour_signal = std::max(neighbour_signal, occupancy_ewma_[zone - 1U]);
    }
    if (zone + 1U < zone_count_) {
        neighbour_signal = std::max(neighbour_signal, occupancy_ewma_[zone + 1U]);
    }

    const double history = profile_.probability(zone, local_hour);
    const double positive_trend =
        std::max(0.0, occupancy_ewma_[zone] - previous_ewma_[zone]);
    const double probability = clamp_probability(
        0.35 * occupancy_ewma_[zone] +
        0.30 * history +
        0.20 * neighbour_signal +
        0.15 * (occupied ? 1.0 : 0.0) +
        0.10 * positive_trend);

    const double learned_fraction =
        std::min(1.0, static_cast<double>(samples_[zone]) / 20.0);
    return {probability, 0.45 + 0.55 * learned_fraction};
}

} // namespace lumigrid
