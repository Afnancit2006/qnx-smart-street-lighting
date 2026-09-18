#include "lumigrid/control_engine.hpp"

#include <algorithm>
#include <cmath>

namespace lumigrid {

void LatencyHistogram::observe(std::uint64_t latency_ns) noexcept {
    ++count_;
    total_ns_ += static_cast<long double>(latency_ns);
    maximum_ns_ = std::max(maximum_ns_, latency_ns);
    const std::uint64_t latency_us = latency_ns / 1000ULL;
    for (std::size_t index = 0; index < kBoundsUs.size(); ++index) {
        if (latency_us <= kBoundsUs[index] || index + 1U == kBoundsUs.size()) {
            ++buckets_[index];
            break;
        }
    }
}

double LatencyHistogram::mean_us() const noexcept {
    return count_ == 0U
               ? 0.0
               : static_cast<double>(total_ns_ /
                                     static_cast<long double>(count_) / 1000.0L);
}

double LatencyHistogram::max_us() const noexcept {
    return static_cast<double>(maximum_ns_) / 1000.0;
}

double LatencyHistogram::p95_us() const noexcept {
    if (count_ == 0U) {
        return 0.0;
    }
    const std::uint64_t target = (count_ * 95U + 99U) / 100U;
    std::uint64_t cumulative = 0;
    for (std::size_t index = 0; index < buckets_.size(); ++index) {
        cumulative += buckets_[index];
        if (cumulative >= target) {
            return static_cast<double>(kBoundsUs[index]);
        }
    }
    return static_cast<double>(kBoundsUs.back());
}

ControlEngine::ControlEngine(RuntimeConfig config, std::uint64_t start_ns)
    : config_(config), start_ns_(start_ns), last_energy_ns_(start_ns) {
    config_.zone_count = std::min(config_.zone_count, kMaxZones);
    for (std::size_t zone = 0; zone < config_.zone_count; ++zone) {
        zones_[zone].brightness = config_.failsafe_brightness;
        zones_[zone].target = config_.failsafe_brightness;
        zones_[zone].last_adjust_ns = start_ns;
    }
}

void ControlEngine::observe_message(const IpcMessage& message,
                                    std::uint64_t now_ns) noexcept {
    ++metrics_.messages_received;
    if (message.produced_ns > 0U && now_ns >= message.produced_ns) {
        const std::uint64_t latency_ns = now_ns - message.produced_ns;
        metrics_.event_latency.observe(latency_ns);
        if (message.type == MessageType::EmergencyOn ||
            message.type == MessageType::EmergencyOff) {
            metrics_.emergency_latency.observe(latency_ns);
        }
    }
}

void ControlEngine::handle_sensor(const IpcMessage& message,
                                  std::uint64_t now_ns) noexcept {
    if (message.zone >= config_.zone_count) {
        return;
    }
    ++metrics_.sensor_samples;
    if ((message.flags & SamplingDeadlineMiss) != 0U) {
        ++metrics_.deadline_misses;
    }

    ZoneState& state = zones_[message.zone];
    state.lux = std::max(0.0, message.lux);
    state.visibility = std::max(0.0, std::min(1.0, message.visibility));
    state.valid = (message.flags & SensorValid) != 0U;
    state.last_sample_ns = now_ns;
    if (!state.valid) {
        ++metrics_.invalid_samples;
    }

    if (message.data0 != 0) {
        state.occupied = true;
        state.occupied_until_ns =
            now_ns + static_cast<std::uint64_t>(config_.active_hold_ms) *
                         1'000'000ULL;
        const std::uint64_t prelight_deadline =
            now_ns + static_cast<std::uint64_t>(config_.prelight_hold_ms) *
                         static_cast<std::uint64_t>(1'000'000U);
        if (message.zone > 0U) {
            zones_[message.zone - 1U].prelight_until_ns = std::max(
                zones_[message.zone - 1U].prelight_until_ns,
                prelight_deadline);
        }
        if (message.zone + 1U < config_.zone_count) {
            zones_[message.zone + 1U].prelight_until_ns = std::max(
                zones_[message.zone + 1U].prelight_until_ns,
                prelight_deadline);
        }
    } else if (now_ns >= state.occupied_until_ns) {
        state.occupied = false;
    }
}

void ControlEngine::handle_prediction(const IpcMessage& message,
                                      std::uint64_t /*now_ns*/) noexcept {
    if (message.zone >= config_.zone_count) {
        return;
    }
    ++metrics_.predictions;
    ZoneState& state = zones_[message.zone];
    state.predicted_probability =
        std::max(0.0, std::min(1.0, message.probability));
    state.prediction_confidence =
        std::max(0.0, std::min(1.0, message.confidence));
}

void ControlEngine::set_emergency(bool active,
                                  std::uint64_t /*now_ns*/) noexcept {
    if (emergency_active_ != active) {
        emergency_active_ = active;
        ++metrics_.emergency_transitions;
    }
}

void ControlEngine::update_energy(std::uint64_t now_ns) noexcept {
    if (now_ns <= last_energy_ns_) {
        return;
    }
    const double hours =
        static_cast<double>(now_ns - last_energy_ns_) / 3.6e12;
    metrics_.baseline_wh += config_.lamp_rated_watts *
                            static_cast<double>(config_.zone_count) * hours;
    double current_watts = 0.0;
    for (std::size_t zone = 0; zone < config_.zone_count; ++zone) {
        current_watts += config_.lamp_rated_watts *
                         zones_[zone].brightness / 100.0;
    }
    metrics_.actual_wh += current_watts * hours;
    last_energy_ns_ = now_ns;
}

void ControlEngine::decide_zone(std::size_t zone,
                                std::uint64_t now_ns) noexcept {
    ZoneState& state = zones_[zone];
    const bool fresh = state.last_sample_ns != 0U && now_ns >= state.last_sample_ns &&
                       now_ns - state.last_sample_ns <=
                           static_cast<std::uint64_t>(config_.stale_after_ms) *
                               1'000'000ULL;

    if (emergency_active_) {
        state.target = config_.full_brightness;
        state.mode = LampMode::Emergency;
    } else if (state.last_sample_ns == 0U) {
        state.target = config_.failsafe_brightness;
        state.mode = LampMode::StartupSafe;
    } else if (!fresh || !state.valid) {
        state.target = config_.failsafe_brightness;
        state.mode = LampMode::SensorFailsafe;
        if (!state.was_stale) {
            ++metrics_.stale_events;
            state.was_stale = true;
        }
    } else if (state.lux >= config_.daylight_off_lux) {
        state.target = 0.0;
        state.mode = LampMode::DaylightOff;
        state.was_stale = false;
    } else if (state.occupied || now_ns < state.occupied_until_ns) {
        state.target = config_.full_brightness;
        state.mode = LampMode::Active;
        state.was_stale = false;
    } else if (state.visibility < config_.weather_visibility_threshold) {
        state.target = config_.weather_brightness;
        state.mode = LampMode::WeatherSafe;
        state.was_stale = false;
    } else if (now_ns < state.prelight_until_ns ||
               state.predicted_probability >= 0.65) {
        state.target = config_.prelight_brightness;
        state.mode = LampMode::Prelight;
        state.was_stale = false;
    } else if (state.predicted_probability >= 0.35) {
        state.target = config_.predictive_brightness;
        state.mode = LampMode::Predictive;
        state.was_stale = false;
    } else {
        state.target = config_.eco_brightness;
        state.mode = LampMode::Eco;
        state.was_stale = false;
    }

    if (state.target >= state.brightness || emergency_active_) {
        state.brightness = state.target;
        state.last_adjust_ns = now_ns;
    } else if (now_ns - state.last_adjust_ns >= 500'000'000ULL) {
        state.brightness = std::max(state.target, state.brightness - 15.0);
        state.last_adjust_ns = now_ns;
    }
}

void ControlEngine::tick(std::uint64_t now_ns) noexcept {
    update_energy(now_ns);
    for (std::size_t zone = 0; zone < config_.zone_count; ++zone) {
        if (zones_[zone].occupied && now_ns >= zones_[zone].occupied_until_ns) {
            zones_[zone].occupied = false;
        }
        decide_zone(zone, now_ns);
    }
}

StatusSnapshot ControlEngine::snapshot(std::uint64_t now_ns,
                                       bool realtime_active) const noexcept {
    StatusSnapshot status{};
    status.zone_count = static_cast<std::uint16_t>(config_.zone_count);
    status.monotonic_ns = now_ns;
    status.uptime_ms = now_ns >= start_ns_ ? (now_ns - start_ns_) / 1'000'000ULL : 0U;
    status.messages_received = metrics_.messages_received;
    status.deadline_misses = metrics_.deadline_misses;
    status.stale_events = metrics_.stale_events;
    status.energy_baseline_wh = metrics_.baseline_wh;
    status.energy_actual_wh = metrics_.actual_wh;
    status.savings_percent = metrics_.baseline_wh > 0.0
                                 ? 100.0 * (1.0 - metrics_.actual_wh /
                                                      metrics_.baseline_wh)
                                 : 0.0;
    status.mean_event_latency_us = metrics_.event_latency.mean_us();
    status.max_event_latency_us = metrics_.event_latency.max_us();
    status.p95_event_latency_us = metrics_.event_latency.p95_us();
    status.max_emergency_latency_us = metrics_.emergency_latency.max_us();
    status.emergency_transitions = metrics_.emergency_transitions;
    status.emergency_active = emergency_active_ ? 1U : 0U;
    status.realtime_priority_active = realtime_active ? 1U : 0U;

    for (std::size_t zone = 0; zone < config_.zone_count; ++zone) {
        const ZoneState& state = zones_[zone];
        ZoneSnapshot& output = status.zones[zone];
        output.lux = state.lux;
        output.visibility = state.visibility;
        output.predicted_probability = state.predicted_probability;
        output.prediction_confidence = state.prediction_confidence;
        output.brightness_percent = state.brightness;
        output.target_percent = state.target;
        output.estimated_watts =
            config_.lamp_rated_watts * state.brightness / 100.0;
        output.sample_age_ms = state.last_sample_ns > 0U && now_ns >= state.last_sample_ns
                                   ? (now_ns - state.last_sample_ns) / 1'000'000ULL
                                   : 0U;
        output.mode = state.mode;
        output.occupied = state.occupied ? 1U : 0U;
        output.sensor_valid = state.valid ? 1U : 0U;
    }
    return status;
}

} // namespace lumigrid
