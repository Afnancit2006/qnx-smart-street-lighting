#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace lumigrid {

constexpr std::uint32_t kMessageMagic = 0x4C475254U; // "LGRT"
constexpr std::uint16_t kProtocolVersion = 1;
constexpr std::size_t kMaxZones = 8;

enum class MessageType : std::uint16_t {
    SensorSample = 1,
    Prediction = 2,
    EmergencyOn = 3,
    EmergencyOff = 4,
    Shutdown = 5,
    RequestStatus = 6,
    TriggerOccupancy = 7,
    SetSensorFault = 8,
    SetLuxOverride = 9,
    ResetSensorOverrides = 10,
};

enum MessageFlags : std::uint32_t {
    SensorValid = 1U << 0U,
    SamplingDeadlineMiss = 1U << 1U,
    BooleanValue = 1U << 2U,
};

enum class LampMode : std::uint8_t {
    StartupSafe = 0,
    DaylightOff,
    Eco,
    Predictive,
    Prelight,
    Active,
    WeatherSafe,
    SensorFailsafe,
    Emergency,
};

struct IpcMessage {
    std::uint32_t magic{kMessageMagic};
    std::uint16_t version{kProtocolVersion};
    MessageType type{MessageType::SensorSample};
    std::uint16_t zone{0};
    std::uint16_t reserved{0};
    std::uint32_t flags{0};
    std::uint64_t sequence{0};
    std::uint64_t produced_ns{0};
    double lux{0.0};
    double visibility{1.0};
    double probability{0.0};
    double confidence{0.0};
    double value{0.0};
    std::int64_t data0{0};
    std::int64_t data1{0};
};

struct ZoneSnapshot {
    double lux{0.0};
    double visibility{1.0};
    double predicted_probability{0.0};
    double prediction_confidence{0.0};
    double brightness_percent{70.0};
    double target_percent{70.0};
    double estimated_watts{70.0};
    std::uint64_t sample_age_ms{0};
    LampMode mode{LampMode::StartupSafe};
    std::uint8_t occupied{0};
    std::uint8_t sensor_valid{0};
    std::uint8_t reserved[5]{};
};

struct StatusSnapshot {
    std::uint32_t magic{kMessageMagic};
    std::uint16_t version{kProtocolVersion};
    std::uint16_t zone_count{0};
    std::uint64_t monotonic_ns{0};
    std::uint64_t uptime_ms{0};
    std::uint64_t messages_received{0};
    std::uint64_t deadline_misses{0};
    std::uint64_t stale_events{0};
    double energy_baseline_wh{0.0};
    double energy_actual_wh{0.0};
    double savings_percent{0.0};
    double mean_event_latency_us{0.0};
    double max_event_latency_us{0.0};
    double p95_event_latency_us{0.0};
    double max_emergency_latency_us{0.0};
    std::uint64_t emergency_transitions{0};
    std::uint8_t emergency_active{0};
    std::uint8_t realtime_priority_active{0};
    std::uint8_t reserved[6]{};
    std::array<ZoneSnapshot, kMaxZones> zones{};
};

struct RuntimeConfig {
    std::size_t zone_count{4};
    std::uint32_t sample_period_ms{250};
    std::uint32_t controller_tick_ms{100};
    std::uint32_t status_period_ms{500};
    std::uint32_t stale_after_ms{1500};
    std::uint32_t active_hold_ms{1800};
    std::uint32_t prelight_hold_ms{2500};
    double daylight_off_lux{160.0};
    double weather_visibility_threshold{0.50};
    double eco_brightness{20.0};
    double predictive_brightness{40.0};
    double prelight_brightness{65.0};
    double weather_brightness{75.0};
    double failsafe_brightness{70.0};
    double full_brightness{100.0};
    double lamp_rated_watts{100.0};
};

constexpr unsigned int kPriorityPrediction = 5;
constexpr unsigned int kPrioritySensor = 10;
constexpr unsigned int kPriorityCommand = 20;
constexpr unsigned int kPriorityShutdown = 30;
constexpr unsigned int kPriorityEmergency = 31;

inline bool valid_message(const IpcMessage& message) noexcept {
    return message.magic == kMessageMagic &&
           message.version == kProtocolVersion &&
           message.zone < kMaxZones;
}

inline bool valid_status(const StatusSnapshot& status) noexcept {
    return status.magic == kMessageMagic &&
           status.version == kProtocolVersion &&
           status.zone_count <= kMaxZones;
}

const char* mode_name(LampMode mode) noexcept;

static_assert(std::is_trivially_copyable<IpcMessage>::value,
              "IPC messages must remain fixed-size POD values");
static_assert(std::is_trivially_copyable<StatusSnapshot>::value,
              "Status snapshots must remain fixed-size POD values");
static_assert(sizeof(IpcMessage) <= 4096U,
              "Event must fit the QNX mqueue default message size");
static_assert(sizeof(StatusSnapshot) <= 4096U,
              "Status must fit the QNX mqueue default message size");

} // namespace lumigrid
