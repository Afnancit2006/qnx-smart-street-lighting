#include "lumigrid/control_engine.hpp"
#include "lumigrid/hardware_input.hpp"
#include "lumigrid/model.hpp"
#include "lumigrid/predictor.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const std::string& description) {
    if (condition) {
        std::cout << "[PASS] " << description << '\n';
    } else {
        std::cerr << "[FAIL] " << description << '\n';
        ++failures;
    }
}

lumigrid::IpcMessage sensor(std::size_t zone,
                            double lux,
                            bool occupied,
                            bool valid,
                            std::uint64_t timestamp) {
    lumigrid::IpcMessage message{};
    message.type = lumigrid::MessageType::SensorSample;
    message.zone = static_cast<std::uint16_t>(zone);
    message.flags = valid ? lumigrid::SensorValid : 0U;
    message.data0 = occupied ? 1 : 0;
    message.lux = lux;
    message.visibility = 0.95;
    message.produced_ns = timestamp;
    return message;
}

void test_state_machine() {
    lumigrid::RuntimeConfig config{};
    config.zone_count = 3;
    const std::uint64_t start = 1'000'000'000ULL;
    lumigrid::ControlEngine engine(config, start);

    engine.tick(start);
    auto status = engine.snapshot(start, false);
    expect(status.zones[0].mode == lumigrid::LampMode::StartupSafe,
           "startup remains at a nonzero safe level before the first sample");

    auto daylight = sensor(0, 500.0, false, true, start + 1U);
    engine.handle_sensor(daylight, start + 1U);
    engine.tick(start + 1U);
    status = engine.snapshot(start + 1U, false);
    expect(status.zones[0].mode == lumigrid::LampMode::DaylightOff,
           "daylight disables an unoccupied lamp");
    expect(status.zones[0].target_percent == 0.0,
           "daylight target is zero percent");

    auto occupied_daylight = sensor(0, 500.0, true, true, start + 5U);
    engine.handle_sensor(occupied_daylight, start + 5U);
    engine.tick(start + 5U);
    status = engine.snapshot(start + 5U, false);
    expect(status.zones[0].mode == lumigrid::LampMode::DaylightOff,
           "daylight remains above routine occupancy");

    auto occupied = sensor(1, 15.0, true, true, start + 10U);
    engine.handle_sensor(occupied, start + 10U);
    engine.tick(start + 10U);
    status = engine.snapshot(start + 10U, false);
    expect(status.zones[1].mode == lumigrid::LampMode::Active,
           "occupancy selects ACTIVE mode");
    expect(status.zones[1].brightness_percent == 100.0,
           "occupancy raises brightness immediately to 100 percent");
    expect(status.zones[0].mode == lumigrid::LampMode::DaylightOff,
           "daylight safety rule remains above neighbour prelight");

    auto dark_neighbour = sensor(2, 12.0, false, true, start + 20U);
    engine.handle_sensor(dark_neighbour, start + 20U);
    engine.tick(start + 20U);
    status = engine.snapshot(start + 20U, false);
    expect(status.zones[2].mode == lumigrid::LampMode::Prelight,
           "adjacent zone is pre-lit as a light wave");

    engine.set_emergency(true, start + 30U);
    engine.tick(start + 30U);
    status = engine.snapshot(start + 30U, false);
    bool every_zone_full = true;
    for (std::size_t zone = 0; zone < status.zone_count; ++zone) {
        every_zone_full = every_zone_full &&
                          status.zones[zone].mode == lumigrid::LampMode::Emergency &&
                          status.zones[zone].brightness_percent == 100.0;
    }
    expect(every_zone_full, "emergency overrides every other state");
}

void test_fail_safe() {
    lumigrid::RuntimeConfig config{};
    config.zone_count = 1;
    config.stale_after_ms = 1000;
    const std::uint64_t start = 5'000'000'000ULL;
    lumigrid::ControlEngine engine(config, start);

    auto bad_sample = sensor(0, 10.0, false, false, start + 1U);
    engine.handle_sensor(bad_sample, start + 1U);
    engine.tick(start + 1U);
    auto status = engine.snapshot(start + 1U, false);
    expect(status.zones[0].mode == lumigrid::LampMode::SensorFailsafe,
           "invalid sensor selects fail-safe mode");
    expect(status.zones[0].target_percent == config.failsafe_brightness,
           "fail-safe brightness is bounded and nonzero");

    auto good_sample = sensor(0, 10.0, false, true, start + 2U);
    engine.handle_sensor(good_sample, start + 2U);
    engine.tick(start + 2U);
    engine.tick(start + 1'100'000'003ULL);
    status = engine.snapshot(start + 1'100'000'003ULL, false);
    expect(status.zones[0].mode == lumigrid::LampMode::SensorFailsafe,
           "stale sensor data cannot leave a dark street");
}

void test_prediction() {
    lumigrid::HistoricalProfile profile;
    lumigrid::TrafficPredictor predictor(3, profile);
    double previous_confidence = 0.0;
    lumigrid::PredictionResult result{};
    bool confidence_monotonic = true;
    for (int sample_index = 0; sample_index < 24; ++sample_index) {
        result = predictor.observe(0, sample_index % 2 == 0, 19);
        confidence_monotonic = confidence_monotonic &&
                               result.confidence + 1e-12 >= previous_confidence;
        previous_confidence = result.confidence;
    }
    expect(confidence_monotonic,
           "prediction confidence never decreases as history accumulates");
    expect(result.probability >= 0.0 && result.probability <= 1.0,
           "prediction probability remains normalized");
    expect(std::abs(result.confidence - 1.0) < 1e-9,
           "prediction confidence saturates at one");
}

void test_latency_histogram() {
    lumigrid::LatencyHistogram histogram;
    histogram.observe(90'000ULL);
    histogram.observe(150'000ULL);
    histogram.observe(900'000ULL);
    expect(std::abs(histogram.mean_us() - 380.0) < 0.001,
           "latency mean is measured in microseconds");
    expect(histogram.max_us() == 900.0,
           "latency maximum preserves worst-case observation");
    expect(histogram.p95_us() == 1000.0,
           "latency p95 uses a deterministic fixed histogram");
}

void test_hardware_input_parser() {
    lumigrid::HardwareFrame frame{};
    const std::string valid =
        "Z1_LDR=240,Z1_DARK=1,Z1_OBJECT=0,Z1_LED=25,"
        "Z2_LDR=700,Z2_DARK=0,Z2_OBJECT=1,Z2_LED=100,EMERGENCY=0";
    expect(lumigrid::parse_hardware_frame(valid, frame),
           "two-zone Arduino record is accepted");
    expect(frame.zones[0].ldr_raw == 240 && frame.zones[0].dark &&
               !frame.zones[0].object_detected &&
               frame.zones[0].led_percent == 25,
           "zone one hardware fields are decoded");
    expect(frame.zones[1].ldr_raw == 700 && !frame.zones[1].dark &&
               frame.zones[1].object_detected &&
               frame.zones[1].led_percent == 100 && !frame.emergency,
           "zone two and emergency fields are decoded");

    const lumigrid::HardwareFrame previous = frame;
    expect(!lumigrid::parse_hardware_frame(
               "Z1_LDR=9999,Z1_DARK=1,Z1_OBJECT=0,Z1_LED=25,"
               "Z2_LDR=700,Z2_DARK=0,Z2_OBJECT=1,Z2_LED=100,EMERGENCY=0",
               frame),
           "out-of-range Arduino record is rejected");
    expect(frame.zones[0].ldr_raw == previous.zones[0].ldr_raw,
           "a rejected record does not modify the previous frame");
    expect(!lumigrid::parse_hardware_frame(valid + ",EXTRA=1", frame),
           "trailing Arduino fields are rejected");
}

} // namespace

int main() {
    test_state_machine();
    test_fail_safe();
    test_prediction();
    test_latency_histogram();
    test_hardware_input_parser();
    if (failures != 0) {
        std::cerr << failures << " unit test(s) failed.\n";
        return 1;
    }
    std::cout << "All unit tests passed.\n";
    return 0;
}
