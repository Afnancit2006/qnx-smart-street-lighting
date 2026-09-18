#include "lumigrid/model.hpp"

namespace lumigrid {

const char* mode_name(LampMode mode) noexcept {
    switch (mode) {
    case LampMode::StartupSafe:
        return "STARTUP_SAFE";
    case LampMode::DaylightOff:
        return "DAYLIGHT_OFF";
    case LampMode::Eco:
        return "ECO";
    case LampMode::Predictive:
        return "PREDICTIVE";
    case LampMode::Prelight:
        return "PRELIGHT";
    case LampMode::Active:
        return "ACTIVE";
    case LampMode::WeatherSafe:
        return "WEATHER_SAFE";
    case LampMode::SensorFailsafe:
        return "SENSOR_FAILSAFE";
    case LampMode::Emergency:
        return "EMERGENCY";
    }
    return "UNKNOWN";
}

} // namespace lumigrid
