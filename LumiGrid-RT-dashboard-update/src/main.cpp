#include "lumigrid/clock.hpp"
#include "lumigrid/control_engine.hpp"
#include "lumigrid/hardware_input.hpp"
#include "lumigrid/ipc.hpp"
#include "lumigrid/model.hpp"
#include "lumigrid/predictor.hpp"
#include "lumigrid/simulator.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

using lumigrid::IpcMessage;
using lumigrid::MessageType;
using lumigrid::ReceiveResult;
using lumigrid::RuntimeConfig;
using lumigrid::StatusSnapshot;

volatile std::sig_atomic_t g_interrupted = 0;

void signal_handler(int /*signal*/) {
    g_interrupted = 1;
}

struct Options {
    RuntimeConfig config{};
    std::string scenario{"demo"};
    std::string output_directory{"run/latest"};
    std::string historical_profile{"config/historical_profile.csv"};
    std::uint32_t duration_seconds{30};
    bool interactive{false};
    bool scripted_emergency{true};
    bool hardware_stdin{false};
};

struct QueueNames {
    std::string events;
    std::string predictions;
    std::string emergency_commands;
    std::string sensor_commands;
    std::string status;

    std::vector<std::string> all() const {
        return {events, predictions, emergency_commands, sensor_commands, status};
    }
};

QueueNames make_queue_names() {
    const std::string id = std::to_string(static_cast<long long>(getpid()));
    return {"/lgrt_" + id + "_evt",
            "/lgrt_" + id + "_prd",
            "/lgrt_" + id + "_emg",
            "/lgrt_" + id + "_sns",
            "/lgrt_" + id + "_sts"};
}

class QueueCleanup {
public:
    explicit QueueCleanup(std::vector<std::string> names)
        : names_(std::move(names)) {}
    ~QueueCleanup() { lumigrid::unlink_queues(names_); }

private:
    std::vector<std::string> names_;
};

void create_runtime_queues(const QueueNames& names) {
    lumigrid::unlink_queues(names.all());
    std::vector<mqd_t> opened;
    try {
        opened.push_back(lumigrid::create_queue(
            names.events, sizeof(IpcMessage)));
        opened.push_back(lumigrid::create_queue(
            names.predictions, sizeof(IpcMessage)));
        opened.push_back(lumigrid::create_queue(
            names.emergency_commands, sizeof(IpcMessage)));
        opened.push_back(lumigrid::create_queue(
            names.sensor_commands, sizeof(IpcMessage)));
        opened.push_back(lumigrid::create_queue(
            names.status, sizeof(StatusSnapshot)));
    } catch (...) {
        for (const mqd_t queue : opened) {
            lumigrid::close_queue(queue);
        }
        lumigrid::unlink_queues(names.all());
        throw;
    }
    for (const mqd_t queue : opened) {
        lumigrid::close_queue(queue);
    }
}

bool request_realtime_priority(int requested_priority) noexcept {
    const int minimum = sched_get_priority_min(SCHED_FIFO);
    const int maximum = sched_get_priority_max(SCHED_FIFO);
    if (minimum == -1 || maximum == -1) {
        return false;
    }
    sched_param parameters{};
    parameters.sched_priority =
        std::max(minimum, std::min(maximum, requested_priority));
    return pthread_setschedparam(pthread_self(), SCHED_FIFO, &parameters) == 0;
}

void set_process_label(const char* label) noexcept {
#if defined(__linux__)
    pthread_setname_np(pthread_self(), label);
#else
    (void)label;
#endif
}

void log_telemetry_header(std::ofstream& output) {
    output << "uptime_ms,zone,lux,visibility,occupied,prediction,confidence,"
              "brightness_percent,target_percent,mode,sensor_valid,sample_age_ms\n";
}

void log_snapshot(std::ofstream& output, const StatusSnapshot& status) {
    output << std::fixed << std::setprecision(3);
    for (std::size_t zone = 0; zone < status.zone_count; ++zone) {
        const auto& value = status.zones[zone];
        output << status.uptime_ms << ',' << (zone + 1U) << ',' << value.lux << ','
               << value.visibility << ',' << static_cast<int>(value.occupied) << ','
               << value.predicted_probability << ',' << value.prediction_confidence
               << ',' << value.brightness_percent << ',' << value.target_percent
               << ',' << lumigrid::mode_name(value.mode) << ','
               << static_cast<int>(value.sensor_valid) << ',' << value.sample_age_ms
               << '\n';
    }
    output.flush();
}

void write_summary(const std::string& path,
                   const StatusSnapshot& status,
                   const lumigrid::EngineMetrics& metrics) {
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Unable to create summary file: " + path);
    }
    output << std::fixed << std::setprecision(3)
           << "{\n"
           << "  \"run_duration_ms\": " << status.uptime_ms << ",\n"
           << "  \"zones\": " << status.zone_count << ",\n"
           << "  \"messages_received\": " << metrics.messages_received << ",\n"
           << "  \"sensor_samples\": " << metrics.sensor_samples << ",\n"
           << "  \"predictions\": " << metrics.predictions << ",\n"
           << "  \"emergency_transitions\": "
           << metrics.emergency_transitions << ",\n"
           << "  \"invalid_samples\": " << metrics.invalid_samples << ",\n"
           << "  \"sampling_deadline_misses\": " << metrics.deadline_misses
           << ",\n"
           << "  \"stale_sensor_events\": " << metrics.stale_events << ",\n"
           << "  \"mean_event_latency_us\": "
           << metrics.event_latency.mean_us() << ",\n"
           << "  \"p95_event_latency_us\": "
           << metrics.event_latency.p95_us() << ",\n"
           << "  \"max_event_latency_us\": "
           << metrics.event_latency.max_us() << ",\n"
           << "  \"max_emergency_latency_us\": "
           << metrics.emergency_latency.max_us() << ",\n"
           << "  \"baseline_energy_wh\": " << status.energy_baseline_wh << ",\n"
           << "  \"estimated_energy_wh\": " << status.energy_actual_wh << ",\n"
           << "  \"estimated_savings_percent\": " << status.savings_percent
           << "\n"
           << "}\n";
}

bool publish_status(mqd_t status_queue, const StatusSnapshot& status) {
    return lumigrid::send_with_timeout(status_queue, &status, sizeof(status), 0U, 1U);
}

int controller_process(const Options& options, const QueueNames& names) {
    set_process_label("lgrt-control");
    const bool realtime_active = request_realtime_priority(70);
    const mqd_t events = lumigrid::open_queue(names.events, O_RDONLY);
    const mqd_t predictions = lumigrid::open_queue(names.predictions, O_WRONLY);
    const mqd_t status_queue =
        lumigrid::open_queue(names.status, O_WRONLY | O_NONBLOCK);

    std::ofstream telemetry(options.output_directory + "/telemetry.csv",
                            std::ios::trunc);
    std::ofstream event_log(options.output_directory + "/events.log",
                            std::ios::trunc);
    if (!telemetry || !event_log) {
        throw std::runtime_error("Controller could not create runtime logs");
    }
    log_telemetry_header(telemetry);

    const std::uint64_t start_ns = lumigrid::monotonic_ns();
    std::uint64_t last_status_ns = 0;
    lumigrid::ControlEngine engine(options.config, start_ns);
    bool running = true;

    event_log << "controller_start realtime_fifo="
              << (realtime_active ? "true" : "false") << '\n';
    while (running) {
        IpcMessage message{};
        unsigned int priority = 0;
        const ReceiveResult result = lumigrid::receive_with_timeout(
            events, &message, sizeof(message), &priority,
            options.config.controller_tick_ms);
        const std::uint64_t now_ns = lumigrid::monotonic_ns();
        bool force_status = false;

        if (result == ReceiveResult::Received && lumigrid::valid_message(message)) {
            engine.observe_message(message, now_ns);
            switch (message.type) {
            case MessageType::SensorSample:
                engine.handle_sensor(message, now_ns);
                if (!lumigrid::send_with_timeout(
                        predictions, &message, sizeof(message),
                        lumigrid::kPrioritySensor, 5U)) {
                    event_log << "prediction_queue_drop sequence="
                              << message.sequence << '\n';
                }
                if ((message.flags & lumigrid::SensorValid) == 0U) {
                    event_log << "invalid_sensor zone=" << (message.zone + 1U)
                              << " sequence=" << message.sequence << '\n';
                }
                break;
            case MessageType::Prediction:
                engine.handle_prediction(message, now_ns);
                break;
            case MessageType::EmergencyOn:
                engine.set_emergency(true, now_ns);
                event_log << "emergency_on latency_us="
                          << (now_ns - message.produced_ns) / 1000ULL << '\n';
                force_status = true;
                break;
            case MessageType::EmergencyOff:
                engine.set_emergency(false, now_ns);
                event_log << "emergency_off latency_us="
                          << (now_ns - message.produced_ns) / 1000ULL << '\n';
                force_status = true;
                break;
            case MessageType::RequestStatus:
                force_status = true;
                break;
            case MessageType::Shutdown:
                running = false;
                event_log << "controller_shutdown\n";
                break;
            default:
                break;
            }
        } else if (result == ReceiveResult::Error) {
            event_log << "event_queue_receive_error errno=" << errno << '\n';
        }

        engine.tick(now_ns);
        const std::uint64_t status_period_ns =
            static_cast<std::uint64_t>(options.config.status_period_ms) *
            1'000'000ULL;
        if (force_status || last_status_ns == 0U ||
            now_ns - last_status_ns >= status_period_ns) {
            const StatusSnapshot status = engine.snapshot(now_ns, realtime_active);
            publish_status(status_queue, status);
            log_snapshot(telemetry, status);
            last_status_ns = now_ns;
        }
    }

    const std::uint64_t finish_ns = lumigrid::monotonic_ns();
    engine.tick(finish_ns);
    const StatusSnapshot final_status = engine.snapshot(finish_ns, realtime_active);
    publish_status(status_queue, final_status);
    log_snapshot(telemetry, final_status);
    write_summary(options.output_directory + "/summary.json", final_status,
                  engine.metrics());

    lumigrid::close_queue(status_queue);
    lumigrid::close_queue(predictions);
    lumigrid::close_queue(events);
    return 0;
}

int predictor_process(const Options& options, const QueueNames& names) {
    set_process_label("lgrt-predict");
    request_realtime_priority(30);
    const mqd_t input = lumigrid::open_queue(names.predictions, O_RDONLY);
    const mqd_t events = lumigrid::open_queue(names.events, O_WRONLY);

    lumigrid::HistoricalProfile profile;
    if (!profile.load_csv(options.historical_profile, options.config.zone_count)) {
        std::cerr << "[predictor] Historical CSV unavailable/incomplete; "
                     "using safe built-in profile.\n";
    }
    lumigrid::TrafficPredictor predictor(options.config.zone_count,
                                         std::move(profile));
    bool running = true;
    while (running) {
        IpcMessage input_message{};
        unsigned int priority = 0;
        const ReceiveResult result = lumigrid::receive_with_timeout(
            input, &input_message, sizeof(input_message), &priority, 250U);
        if (result != ReceiveResult::Received ||
            !lumigrid::valid_message(input_message)) {
            continue;
        }
        if (input_message.type == MessageType::Shutdown) {
            running = false;
            continue;
        }
        if (input_message.type != MessageType::SensorSample) {
            continue;
        }

        const int local_hour =
            input_message.data1 >= 0 && input_message.data1 <= 23
                ? static_cast<int>(input_message.data1)
                : 19;
        const auto prediction = predictor.observe(
            input_message.zone, input_message.data0 != 0, local_hour);
        IpcMessage output{};
        output.type = MessageType::Prediction;
        output.zone = input_message.zone;
        output.sequence = input_message.sequence;
        output.produced_ns = lumigrid::monotonic_ns();
        output.probability = prediction.probability;
        output.confidence = prediction.confidence;
        lumigrid::send_with_timeout(events, &output, sizeof(output),
                                    lumigrid::kPriorityPrediction, 10U);
    }

    lumigrid::close_queue(events);
    lumigrid::close_queue(input);
    return 0;
}

bool send_emergency_event(mqd_t events,
                          MessageType type,
                          std::uint64_t produced_ns,
                          std::uint64_t sequence) {
    IpcMessage event{};
    event.type = type;
    event.produced_ns = produced_ns;
    event.sequence = sequence;
    return lumigrid::send_with_timeout(events, &event, sizeof(event),
                                       lumigrid::kPriorityEmergency, 10U);
}

int emergency_process(const QueueNames& names) {
    set_process_label("lgrt-emergency");
    request_realtime_priority(60);
    const mqd_t commands =
        lumigrid::open_queue(names.emergency_commands, O_RDONLY);
    const mqd_t events = lumigrid::open_queue(names.events, O_WRONLY);

    bool running = true;
    bool active = false;
    std::uint64_t expiry_ns = 0;
    std::uint64_t sequence = 0;
    while (running) {
        IpcMessage command{};
        unsigned int priority = 0;
        const ReceiveResult result = lumigrid::receive_with_timeout(
            commands, &command, sizeof(command), &priority, 50U);
        const std::uint64_t now_ns = lumigrid::monotonic_ns();
        if (result == ReceiveResult::Received && lumigrid::valid_message(command)) {
            if (command.type == MessageType::Shutdown) {
                running = false;
            } else if (command.type == MessageType::EmergencyOn) {
                active = true;
                expiry_ns = command.data0 > 0
                                ? now_ns + static_cast<std::uint64_t>(command.data0) *
                                               1'000'000ULL
                                : 0U;
                send_emergency_event(events, MessageType::EmergencyOn,
                                     command.produced_ns, ++sequence);
            } else if (command.type == MessageType::EmergencyOff) {
                active = false;
                expiry_ns = 0;
                send_emergency_event(events, MessageType::EmergencyOff,
                                     command.produced_ns, ++sequence);
            }
        }

        if (active && expiry_ns > 0U && now_ns >= expiry_ns) {
            active = false;
            expiry_ns = 0;
            send_emergency_event(events, MessageType::EmergencyOff,
                                 now_ns, ++sequence);
        }
    }

    lumigrid::close_queue(events);
    lumigrid::close_queue(commands);
    return 0;
}

int sensor_process(const Options& options, const QueueNames& names) {
    set_process_label("lgrt-sensors");
    request_realtime_priority(40);
    const mqd_t events = lumigrid::open_queue(names.events, O_WRONLY);
    const mqd_t commands = lumigrid::open_queue(
        names.sensor_commands, O_RDONLY | O_NONBLOCK);
    lumigrid::SensorSimulator simulator(options.config.zone_count,
                                        options.scenario);

    const std::uint64_t start_ns = lumigrid::monotonic_ns();
    const std::uint64_t period_ns =
        static_cast<std::uint64_t>(options.config.sample_period_ms) *
        1'000'000ULL;
    std::uint64_t next_deadline_ns = start_ns;
    std::uint64_t sequence = 0;
    std::uint64_t dropped = 0;
    bool running = true;

    while (running) {
        IpcMessage command{};
        unsigned int command_priority = 0;
        while (lumigrid::receive_with_timeout(
                   commands, &command, sizeof(command), &command_priority, 0U) ==
               ReceiveResult::Received) {
            if (command.type == MessageType::Shutdown) {
                running = false;
                break;
            }
            simulator.apply_command(command, lumigrid::monotonic_ns());
        }
        if (!running) {
            break;
        }

        const std::uint64_t cycle_start_ns = lumigrid::monotonic_ns();
        const bool deadline_missed =
            cycle_start_ns > next_deadline_ns + 2'000'000ULL;
        const double elapsed_seconds =
            static_cast<double>(cycle_start_ns - start_ns) / 1.0e9;
        const int virtual_hour =
            (18 + static_cast<int>(elapsed_seconds / 10.0)) % 24;

        for (std::size_t zone = 0; zone < options.config.zone_count; ++zone) {
            const lumigrid::SimulatedSample value =
                simulator.sample(zone, elapsed_seconds);
            IpcMessage sample{};
            sample.type = MessageType::SensorSample;
            sample.zone = static_cast<std::uint16_t>(zone);
            sample.flags = value.valid ? lumigrid::SensorValid : 0U;
            if (deadline_missed) {
                sample.flags |= lumigrid::SamplingDeadlineMiss;
            }
            sample.sequence = ++sequence;
            sample.produced_ns = lumigrid::monotonic_ns();
            sample.lux = value.lux;
            sample.visibility = value.visibility;
            sample.data0 = value.occupied ? 1 : 0;
            sample.data1 = virtual_hour;
            if (!lumigrid::send_with_timeout(
                    events, &sample, sizeof(sample), lumigrid::kPrioritySensor,
                    options.config.sample_period_ms / 2U)) {
                ++dropped;
            }
        }

        next_deadline_ns += period_ns;
        const std::uint64_t after_work_ns = lumigrid::monotonic_ns();
        if (after_work_ns > next_deadline_ns + period_ns) {
            next_deadline_ns = after_work_ns + period_ns;
        }
        lumigrid::sleep_until_monotonic(next_deadline_ns);
    }

    if (dropped > 0U) {
        std::cerr << "[sensors] dropped_messages=" << dropped << '\n';
    }
    lumigrid::close_queue(commands);
    lumigrid::close_queue(events);
    return 0;
}

int hardware_sensor_process(const Options& options, const QueueNames& names) {
    set_process_label("lgrt-hardware");
    request_realtime_priority(40);
    const mqd_t events = lumigrid::open_queue(names.events, O_WRONLY);
    const mqd_t commands = lumigrid::open_queue(
        names.sensor_commands, O_RDONLY | O_NONBLOCK);

    std::string input_buffer;
    std::uint64_t sequence = 0;
    std::uint64_t dropped = 0;
    std::uint64_t invalid_lines = 0;
    bool input_closed = false;
    bool emergency_known = false;
    bool previous_emergency = false;
    bool running = true;

    auto publish_frame = [&](const std::string& line) {
        if (!line.empty() && line.front() == '#') {
            return;
        }
        lumigrid::HardwareFrame frame{};
        if (!lumigrid::parse_hardware_frame(line, frame)) {
            ++invalid_lines;
            if (invalid_lines <= 3U) {
                std::cerr << "[hardware] ignored malformed input: " << line
                          << '\n';
            }
            return;
        }

        const std::uint64_t produced_ns = lumigrid::monotonic_ns();
        if (!emergency_known || frame.emergency != previous_emergency) {
            if (!send_emergency_event(
                    events,
                    frame.emergency ? MessageType::EmergencyOn
                                    : MessageType::EmergencyOff,
                    produced_ns, ++sequence)) {
                ++dropped;
            }
            emergency_known = true;
            previous_emergency = frame.emergency;
        }

        for (std::size_t zone = 0; zone < lumigrid::kHardwareZoneCount;
             ++zone) {
            const auto& input = frame.zones[zone];
            IpcMessage sample{};
            sample.type = MessageType::SensorSample;
            sample.zone = static_cast<std::uint16_t>(zone);
            sample.flags = lumigrid::SensorValid;
            sample.sequence = ++sequence;
            sample.produced_ns = produced_ns;

            // The LDR is calibrated as a day/night detector, not a laboratory
            // lux meter. These proxy values preserve the controller boundary.
            sample.lux = input.dark ? 20.0 : 300.0;
            sample.visibility = 1.0;
            sample.value = static_cast<double>(input.ldr_raw);
            sample.data0 = input.object_detected ? 1 : 0;
            sample.data1 = 19;
            if (!lumigrid::send_with_timeout(
                    events, &sample, sizeof(sample), lumigrid::kPrioritySensor,
                    options.config.sample_period_ms / 2U)) {
                ++dropped;
            }
        }
    };

    std::cerr << "[hardware] waiting for two-zone Arduino records on stdin\n";
    while (running) {
        IpcMessage command{};
        unsigned int command_priority = 0;
        while (lumigrid::receive_with_timeout(
                   commands, &command, sizeof(command), &command_priority, 0U) ==
               ReceiveResult::Received) {
            if (command.type == MessageType::Shutdown) {
                running = false;
                break;
            }
        }
        if (!running) {
            break;
        }

        if (input_closed) {
            poll(nullptr, 0, 100);
            continue;
        }

        pollfd input{};
        input.fd = STDIN_FILENO;
        input.events = POLLIN | POLLHUP;
        int poll_result = -1;
        do {
            poll_result = poll(&input, 1, 100);
        } while (poll_result == -1 && errno == EINTR);

        if (poll_result <= 0 ||
            (input.revents & (POLLIN | POLLHUP)) == 0) {
            continue;
        }

        char bytes[1024]{};
        ssize_t received = -1;
        do {
            received = read(STDIN_FILENO, bytes, sizeof(bytes));
        } while (received == -1 && errno == EINTR);

        if (received == 0) {
            input_closed = true;
            std::cerr << "[hardware] input closed; controller fail-safe remains active\n";
            continue;
        }
        if (received < 0) {
            std::cerr << "[hardware] stdin read failed: " << std::strerror(errno)
                      << '\n';
            input_closed = true;
            continue;
        }

        input_buffer.append(bytes, static_cast<std::size_t>(received));
        std::size_t newline = 0;
        while ((newline = input_buffer.find('\n')) != std::string::npos) {
            std::string line = input_buffer.substr(0, newline);
            input_buffer.erase(0, newline + 1U);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (!line.empty()) {
                publish_frame(line);
            }
        }
        if (input_buffer.size() > 8192U) {
            input_buffer.clear();
            ++invalid_lines;
        }
    }

    if (dropped > 0U || invalid_lines > 0U) {
        std::cerr << "[hardware] dropped_messages=" << dropped
                  << " invalid_lines=" << invalid_lines << '\n';
    }
    lumigrid::close_queue(commands);
    lumigrid::close_queue(events);
    return 0;
}

template <typename Function>
pid_t spawn_child(const char* label, Function function) {
    std::cout.flush();
    std::cerr.flush();
    const pid_t child = fork();
    if (child == -1) {
        throw std::runtime_error(std::string("fork(") + label + "): " +
                                 std::strerror(errno));
    }
    if (child == 0) {
        int result = 1;
        try {
            result = function();
        } catch (const std::exception& error) {
            std::cerr << '[' << label << "] " << error.what() << '\n';
        }
        std::cout.flush();
        std::cerr.flush();
        _exit(result);
    }
    return child;
}

IpcMessage command_message(MessageType type) {
    IpcMessage message{};
    message.type = type;
    message.produced_ns = lumigrid::monotonic_ns();
    return message;
}

bool send_command(mqd_t queue,
                  const IpcMessage& message,
                  unsigned int priority = lumigrid::kPriorityCommand) {
    return lumigrid::send_with_timeout(queue, &message, sizeof(message),
                                       priority, 50U);
}

std::optional<StatusSnapshot> receive_latest_status(mqd_t queue,
                                                    std::uint32_t timeout_ms) {
    StatusSnapshot latest{};
    unsigned int priority = 0;
    if (lumigrid::receive_with_timeout(queue, &latest, sizeof(latest), &priority,
                                      timeout_ms) != ReceiveResult::Received ||
        !lumigrid::valid_status(latest)) {
        return std::nullopt;
    }

    mq_attr attributes{};
    while (mq_getattr(queue, &attributes) == 0 && attributes.mq_curmsgs > 0) {
        StatusSnapshot candidate{};
        if (lumigrid::receive_with_timeout(queue, &candidate, sizeof(candidate),
                                          &priority, 1U) !=
            ReceiveResult::Received) {
            break;
        }
        if (lumigrid::valid_status(candidate)) {
            latest = candidate;
        }
    }
    return latest;
}

void print_status(const StatusSnapshot& status) {
    std::cout << "\nLumiGrid-RT  t=" << std::fixed << std::setprecision(1)
              << static_cast<double>(status.uptime_ms) / 1000.0 << " s  "
              << "Emergency=" << (status.emergency_active ? "ON" : "off")
              << "  RT_FIFO="
              << (status.realtime_priority_active ? "active" : "fallback")
              << '\n';
    std::cout << "Zone  Lux     IR   Predict  Bright  Target  Valid  Mode\n";
    for (std::size_t zone = 0; zone < status.zone_count; ++zone) {
        const auto& value = status.zones[zone];
        std::cout << std::setw(4) << (zone + 1U) << "  "
                  << std::setw(6) << std::setprecision(1) << value.lux << "  "
                  << std::setw(3) << (value.occupied ? "YES" : "no") << "  "
                  << std::setw(6) << std::setprecision(0)
                  << value.predicted_probability * 100.0 << "%  "
                  << std::setw(5) << value.brightness_percent << "%  "
                  << std::setw(5) << value.target_percent << "%  "
                  << std::setw(5) << (value.sensor_valid ? "yes" : "NO") << "  "
                  << lumigrid::mode_name(value.mode) << '\n';
    }
    std::cout << std::setprecision(2)
              << "Energy saving=" << status.savings_percent << "%  "
              << "Latency mean/p95/max=" << status.mean_event_latency_us << '/'
              << status.p95_event_latency_us << '/'
              << status.max_event_latency_us << " us  "
              << "Emergency max=" << status.max_emergency_latency_us << " us\n"
              << "Messages=" << status.messages_received
              << "  Sampling misses=" << status.deadline_misses
              << "  Fail-safe entries=" << status.stale_events << '\n';
}

int rounded_pwm_percent(double value) noexcept {
    return static_cast<int>(
        std::lround(std::clamp(value, 0.0, 100.0)));
}

void print_hardware_command(const StatusSnapshot& status) {
    if (status.zone_count != lumigrid::kHardwareZoneCount) {
        return;
    }

    // The Windows bridge recognizes this exact line and forwards it to the
    // Nano. Flushing keeps actuator updates responsive when stdout is an SSH
    // pipe rather than an interactive terminal.
    std::cout << "QNX_PWM:Z1="
              << rounded_pwm_percent(status.zones[0].brightness_percent)
              << ",Z2="
              << rounded_pwm_percent(status.zones[1].brightness_percent)
              << '\n'
              << std::flush;
}

void print_dashboard_status(const StatusSnapshot& status) {
    if (status.zone_count != lumigrid::kHardwareZoneCount) {
        return;
    }

    std::ostringstream json;
    json << std::fixed << std::setprecision(3)
         << "{\"uptime_ms\":" << status.uptime_ms
         << ",\"emergency\":"
         << (status.emergency_active ? "true" : "false")
         << ",\"rt_fifo\":"
         << (status.realtime_priority_active ? "true" : "false")
         << ",\"zones\":[";

    for (std::size_t zone = 0; zone < status.zone_count; ++zone) {
        if (zone > 0U) {
            json << ',';
        }
        const auto& value = status.zones[zone];
        json << "{\"id\":" << (zone + 1U)
             << ",\"lux\":" << value.lux
             << ",\"ir\":" << (value.occupied ? "true" : "false")
             << ",\"prediction\":"
             << value.predicted_probability * 100.0
             << ",\"brightness\":" << value.brightness_percent
             << ",\"target\":" << value.target_percent
             << ",\"valid\":"
             << (value.sensor_valid ? "true" : "false")
             << ",\"sample_age_ms\":" << value.sample_age_ms
             << ",\"mode\":\"" << lumigrid::mode_name(value.mode)
             << "\"}";
    }

    json << "]"
         << ",\"energy_saving\":" << status.savings_percent
         << ",\"latency_mean_us\":" << status.mean_event_latency_us
         << ",\"latency_p95_us\":" << status.p95_event_latency_us
         << ",\"latency_max_us\":" << status.max_event_latency_us
         << ",\"emergency_max_us\":" << status.max_emergency_latency_us
         << ",\"messages\":" << status.messages_received
         << ",\"sampling_misses\":" << status.deadline_misses
         << ",\"failsafe_entries\":" << status.stale_events
         << '}';

    // This prefix lets the Windows bridge distinguish dashboard data from the
    // human-readable console table without changing the QNX control path.
    std::cout << "QNX_STATUS:" << json.str() << '\n' << std::flush;
}

void print_cli_help() {
    std::cout
        << "Commands:\n"
        << "  status                         Show latest zone map and metrics\n"
        << "  emergency on [seconds]         Force every lamp to 100%\n"
        << "  emergency off                  Cancel the override\n"
        << "  occupancy <zone> [seconds]     Inject an occupancy event\n"
        << "  fault <zone> on|off            Inject/clear a sensor fault\n"
        << "  lux <value>                    Override daylight for every zone\n"
        << "  auto                           Clear all sensor overrides\n"
        << "  help                           Show these commands\n"
        << "  quit                           Stop all processes cleanly\n";
}

void run_interactive(const Options& options,
                     mqd_t events,
                     mqd_t emergency_commands,
                     mqd_t sensor_commands,
                     mqd_t status_queue) {
    print_cli_help();
    std::string line;
    while (!g_interrupted) {
        std::cout << "lgrt> " << std::flush;
        if (!std::getline(std::cin, line)) {
            break;
        }
        std::stringstream stream(line);
        std::string command;
        stream >> command;
        if (command.empty()) {
            continue;
        }
        if (command == "quit" || command == "exit") {
            break;
        }
        if (command == "help") {
            print_cli_help();
            continue;
        }
        if (command == "status") {
            send_command(events, command_message(MessageType::RequestStatus));
            const auto status = receive_latest_status(status_queue, 1000U);
            if (status) {
                print_status(*status);
            } else {
                std::cout << "No controller status received.\n";
            }
            continue;
        }
        if (command == "emergency") {
            std::string action;
            stream >> action;
            if (action == "on") {
                double seconds = 0.0;
                stream >> seconds;
                IpcMessage message = command_message(MessageType::EmergencyOn);
                message.data0 = seconds > 0.0
                                    ? static_cast<std::int64_t>(seconds * 1000.0)
                                    : 0;
                send_command(emergency_commands, message,
                             lumigrid::kPriorityEmergency);
                std::cout << "Emergency override requested.\n";
            } else if (action == "off") {
                send_command(emergency_commands,
                             command_message(MessageType::EmergencyOff),
                             lumigrid::kPriorityEmergency);
                std::cout << "Emergency override cancellation requested.\n";
            } else {
                std::cout << "Usage: emergency on [seconds] | emergency off\n";
            }
            continue;
        }
        if (command == "occupancy") {
            std::size_t zone = 0;
            double seconds = 2.0;
            if (!(stream >> zone) || zone == 0U ||
                zone > options.config.zone_count) {
                std::cout << "Zone must be 1-" << options.config.zone_count << ".\n";
                continue;
            }
            stream >> seconds;
            IpcMessage message = command_message(MessageType::TriggerOccupancy);
            message.zone = static_cast<std::uint16_t>(zone - 1U);
            message.data0 = static_cast<std::int64_t>(
                std::max(0.1, seconds) * 1000.0);
            send_command(sensor_commands, message);
            continue;
        }
        if (command == "fault") {
            std::size_t zone = 0;
            std::string action;
            if (!(stream >> zone >> action) || zone == 0U ||
                zone > options.config.zone_count ||
                (action != "on" && action != "off")) {
                std::cout << "Usage: fault <zone 1-" << options.config.zone_count
                          << "> on|off\n";
                continue;
            }
            IpcMessage message = command_message(MessageType::SetSensorFault);
            message.zone = static_cast<std::uint16_t>(zone - 1U);
            message.data0 = action == "on" ? 1 : 0;
            send_command(sensor_commands, message);
            continue;
        }
        if (command == "lux") {
            double lux = -1.0;
            if (!(stream >> lux) || lux < 0.0) {
                std::cout << "Usage: lux <non-negative value>\n";
                continue;
            }
            IpcMessage message = command_message(MessageType::SetLuxOverride);
            message.value = lux;
            send_command(sensor_commands, message);
            continue;
        }
        if (command == "auto") {
            send_command(sensor_commands,
                         command_message(MessageType::ResetSensorOverrides));
            continue;
        }
        std::cout << "Unknown command. Type 'help'.\n";
    }
}

void run_automatic(const Options& options,
                   mqd_t emergency_commands,
                   mqd_t status_queue) {
    const std::uint64_t start_ns = lumigrid::monotonic_ns();
    const std::uint64_t finish_ns =
        start_ns + static_cast<std::uint64_t>(options.duration_seconds) *
                       1'000'000'000ULL;
    const double emergency_start =
        std::max(3.0, static_cast<double>(options.duration_seconds) * 0.40);
    const double emergency_duration =
        std::min(4.0, std::max(1.0,
                              static_cast<double>(options.duration_seconds) * 0.18));
    bool emergency_sent = false;
    std::uint64_t last_printed_second = static_cast<std::uint64_t>(-1);

    if (options.hardware_stdin) {
        std::cout << "Starting live two-zone hardware run for "
                  << options.duration_seconds << " seconds.\n";
    } else {
        std::cout << "Starting deterministic '" << options.scenario
                  << "' scenario for " << options.duration_seconds
                  << " seconds.\n";
    }
    while (!g_interrupted && lumigrid::monotonic_ns() < finish_ns) {
        const double elapsed =
            static_cast<double>(lumigrid::monotonic_ns() - start_ns) / 1.0e9;
        if (options.scripted_emergency && !emergency_sent &&
            elapsed >= emergency_start) {
            IpcMessage message = command_message(MessageType::EmergencyOn);
            message.data0 = static_cast<std::int64_t>(emergency_duration * 1000.0);
            send_command(emergency_commands, message,
                         lumigrid::kPriorityEmergency);
            emergency_sent = true;
        }

        const auto status = receive_latest_status(status_queue, 400U);
        if (status) {
            if (options.hardware_stdin) {
                print_hardware_command(*status);
                print_dashboard_status(*status);
            }
            const std::uint64_t second = status->uptime_ms / 1000ULL;
            if (second != last_printed_second) {
                print_status(*status);
                last_printed_second = second;
            }
        }
    }
}

void send_shutdown(mqd_t events,
                   mqd_t predictions,
                   mqd_t emergency_commands,
                   mqd_t sensor_commands) {
    const IpcMessage shutdown = command_message(MessageType::Shutdown);
    send_command(sensor_commands, shutdown, lumigrid::kPriorityShutdown);
    send_command(predictions, shutdown, lumigrid::kPriorityShutdown);
    send_command(emergency_commands, shutdown, lumigrid::kPriorityShutdown);
    send_command(events, shutdown, lumigrid::kPriorityShutdown);
}

void wait_for_children(const std::vector<pid_t>& children) {
    for (const pid_t child : children) {
        int status = 0;
        while (waitpid(child, &status, 0) == -1 && errno == EINTR) {
        }
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            std::cerr << "Child process " << child << " exited with code "
                      << WEXITSTATUS(status) << ".\n";
        } else if (WIFSIGNALED(status)) {
            std::cerr << "Child process " << child << " received signal "
                      << WTERMSIG(status) << ".\n";
        }
    }
}

void print_usage(const char* executable) {
    std::cout
        << "Usage: " << executable << " [options]\n\n"
        << "Options:\n"
        << "  --interactive             Start the mandatory command-line UI\n"
        << "  --scenario NAME           demo|quiet|rush|daylight|fault (default demo)\n"
        << "  --duration SECONDS        Automatic-demo duration (default 30)\n"
        << "  --zones COUNT             Number of lighting zones, 1-8 (default 4)\n"
        << "  --hardware-stdin          Read two-zone Nano records from standard input\n"
        << "  --sample-ms MS            Sensor period, 50-5000 ms (default 250)\n"
        << "  --output DIRECTORY        Runtime logs directory (default run/latest)\n"
        << "  --profile FILE            24-hour historical traffic CSV\n"
        << "  --no-scripted-emergency   Disable automatic emergency injection\n"
        << "  --help                    Show this help\n";
}

std::uint32_t parse_unsigned(const std::string& value, const char* option) {
    std::size_t consumed = 0;
    unsigned long parsed = 0;
    try {
        parsed = std::stoul(value, &consumed);
    } catch (...) {
        throw std::invalid_argument(std::string("Invalid value for ") + option);
    }
    if (consumed != value.size() || parsed > 1'000'000UL) {
        throw std::invalid_argument(std::string("Invalid value for ") + option);
    }
    return static_cast<std::uint32_t>(parsed);
}

Options parse_options(int argc, char* argv[]) {
    Options options{};
    bool zones_explicit = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        auto require_value = [&](const char* option) -> std::string {
            if (index + 1 >= argc) {
                throw std::invalid_argument(std::string("Missing value for ") + option);
            }
            return argv[++index];
        };
        if (argument == "--interactive") {
            options.interactive = true;
        } else if (argument == "--scenario") {
            options.scenario = require_value("--scenario");
        } else if (argument == "--duration") {
            options.duration_seconds =
                parse_unsigned(require_value("--duration"), "--duration");
        } else if (argument == "--zones") {
            options.config.zone_count =
                parse_unsigned(require_value("--zones"), "--zones");
            zones_explicit = true;
        } else if (argument == "--hardware-stdin") {
            options.hardware_stdin = true;
        } else if (argument == "--sample-ms") {
            options.config.sample_period_ms =
                parse_unsigned(require_value("--sample-ms"), "--sample-ms");
        } else if (argument == "--output") {
            options.output_directory = require_value("--output");
        } else if (argument == "--profile") {
            options.historical_profile = require_value("--profile");
        } else if (argument == "--no-scripted-emergency") {
            options.scripted_emergency = false;
        } else if (argument == "--help" || argument == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        } else {
            throw std::invalid_argument("Unknown option: " + argument);
        }
    }

    if (options.config.zone_count == 0U ||
        options.config.zone_count > lumigrid::kMaxZones) {
        throw std::invalid_argument("--zones must be between 1 and 8");
    }
    if (options.hardware_stdin && !zones_explicit) {
        options.config.zone_count = lumigrid::kHardwareZoneCount;
    }
    if (options.hardware_stdin &&
        options.config.zone_count != lumigrid::kHardwareZoneCount) {
        throw std::invalid_argument(
            "--hardware-stdin requires exactly two zones");
    }
    if (options.hardware_stdin && options.interactive) {
        throw std::invalid_argument(
            "--hardware-stdin cannot share stdin with --interactive");
    }
    if (options.config.sample_period_ms < 50U ||
        options.config.sample_period_ms > 5000U) {
        throw std::invalid_argument("--sample-ms must be between 50 and 5000");
    }
    if (!options.interactive &&
        (options.duration_seconds == 0U || options.duration_seconds > 3600U)) {
        throw std::invalid_argument("--duration must be between 1 and 3600");
    }
    const std::vector<std::string> scenarios{
        "demo", "quiet", "rush", "daylight", "fault"};
    if (std::find(scenarios.begin(), scenarios.end(), options.scenario) ==
        scenarios.end()) {
        throw std::invalid_argument("Unsupported scenario: " + options.scenario);
    }
    return options;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        const Options options = parse_options(argc, argv);
        std::filesystem::create_directories(options.output_directory);
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        const QueueNames names = make_queue_names();
        QueueCleanup cleanup(names.all());
        create_runtime_queues(names);

        std::vector<pid_t> children;
        children.push_back(spawn_child("controller", [&] {
            return controller_process(options, names);
        }));
        children.push_back(spawn_child("predictor", [&] {
            return predictor_process(options, names);
        }));
        children.push_back(spawn_child("emergency", [&] {
            return emergency_process(names);
        }));
        children.push_back(spawn_child(
            options.hardware_stdin ? "hardware" : "sensors", [&] {
                return options.hardware_stdin
                           ? hardware_sensor_process(options, names)
                           : sensor_process(options, names);
            }));

        const mqd_t events = lumigrid::open_queue(names.events, O_WRONLY);
        const mqd_t predictions =
            lumigrid::open_queue(names.predictions, O_WRONLY);
        const mqd_t emergency_commands =
            lumigrid::open_queue(names.emergency_commands, O_WRONLY);
        const mqd_t sensor_commands =
            lumigrid::open_queue(names.sensor_commands, O_WRONLY);
        const mqd_t status_queue = lumigrid::open_queue(names.status, O_RDONLY);

        if (options.interactive) {
            run_interactive(options, events, emergency_commands, sensor_commands,
                            status_queue);
        } else {
            run_automatic(options, emergency_commands, status_queue);
        }

        send_shutdown(events, predictions, emergency_commands, sensor_commands);
        wait_for_children(children);

        lumigrid::close_queue(status_queue);
        lumigrid::close_queue(sensor_commands);
        lumigrid::close_queue(emergency_commands);
        lumigrid::close_queue(predictions);
        lumigrid::close_queue(events);

        std::cout << "\nRun complete. Metrics: " << options.output_directory
                  << "/summary.json\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "lumigrid: " << error.what() << '\n';
        return 1;
    }
}
