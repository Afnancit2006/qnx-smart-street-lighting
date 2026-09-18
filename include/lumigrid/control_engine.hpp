#pragma once

#include "lumigrid/model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace lumigrid {

class LatencyHistogram {
public:
    void observe(std::uint64_t latency_ns) noexcept;
    double mean_us() const noexcept;
    double max_us() const noexcept;
    double p95_us() const noexcept;

private:
    static constexpr std::array<std::uint64_t, 12> kBoundsUs{
        50, 100, 200, 500, 1'000, 2'000, 5'000, 10'000,
        20'000, 50'000, 100'000, 1'000'000};
    std::array<std::uint64_t, kBoundsUs.size()> buckets_{};
    std::uint64_t count_{0};
    long double total_ns_{0.0};
    std::uint64_t maximum_ns_{0};
};

struct EngineMetrics {
    std::uint64_t messages_received{0};
    std::uint64_t sensor_samples{0};
    std::uint64_t predictions{0};
    std::uint64_t emergency_transitions{0};
    std::uint64_t invalid_samples{0};
    std::uint64_t deadline_misses{0};
    std::uint64_t stale_events{0};
    double baseline_wh{0.0};
    double actual_wh{0.0};
    LatencyHistogram event_latency{};
    LatencyHistogram emergency_latency{};
};

class ControlEngine {
public:
    ControlEngine(RuntimeConfig config, std::uint64_t start_ns);

    void handle_sensor(const IpcMessage& message, std::uint64_t now_ns) noexcept;
    void handle_prediction(const IpcMessage& message, std::uint64_t now_ns) noexcept;
    void set_emergency(bool active, std::uint64_t now_ns) noexcept;
    void tick(std::uint64_t now_ns) noexcept;
    void observe_message(const IpcMessage& message, std::uint64_t now_ns) noexcept;

    StatusSnapshot snapshot(std::uint64_t now_ns, bool realtime_active) const noexcept;
    const EngineMetrics& metrics() const noexcept { return metrics_; }

private:
    struct ZoneState {
        double lux{0.0};
        double visibility{1.0};
        double predicted_probability{0.0};
        double prediction_confidence{0.0};
        double brightness{70.0};
        double target{70.0};
        std::uint64_t last_sample_ns{0};
        std::uint64_t occupied_until_ns{0};
        std::uint64_t prelight_until_ns{0};
        std::uint64_t last_adjust_ns{0};
        LampMode mode{LampMode::StartupSafe};
        bool occupied{false};
        bool valid{false};
        bool was_stale{false};
    };

    void update_energy(std::uint64_t now_ns) noexcept;
    void decide_zone(std::size_t zone, std::uint64_t now_ns) noexcept;

    RuntimeConfig config_;
    std::uint64_t start_ns_;
    std::uint64_t last_energy_ns_;
    bool emergency_active_{false};
    std::array<ZoneState, kMaxZones> zones_{};
    EngineMetrics metrics_{};
};

} // namespace lumigrid
