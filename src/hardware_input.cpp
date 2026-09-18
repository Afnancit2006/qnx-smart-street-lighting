#include "lumigrid/hardware_input.hpp"

#include <cstdio>

namespace lumigrid {

bool parse_hardware_frame(const std::string& line,
                          HardwareFrame& destination) noexcept {
    int z1_ldr = 0;
    int z1_dark = 0;
    int z1_object = 0;
    int z1_led = 0;
    int z2_ldr = 0;
    int z2_dark = 0;
    int z2_object = 0;
    int z2_led = 0;
    int emergency = 0;
    char trailing = '\0';

    const int fields = std::sscanf(
        line.c_str(),
        "Z1_LDR=%d,Z1_DARK=%d,Z1_OBJECT=%d,Z1_LED=%d,"
        "Z2_LDR=%d,Z2_DARK=%d,Z2_OBJECT=%d,Z2_LED=%d,EMERGENCY=%d%c",
        &z1_ldr, &z1_dark, &z1_object, &z1_led, &z2_ldr, &z2_dark,
        &z2_object, &z2_led, &emergency, &trailing);

    if (fields != 9 || z1_ldr < 0 || z1_ldr > 1023 || z2_ldr < 0 ||
        z2_ldr > 1023 || (z1_dark != 0 && z1_dark != 1) ||
        (z2_dark != 0 && z2_dark != 1) ||
        (z1_object != 0 && z1_object != 1) ||
        (z2_object != 0 && z2_object != 1) || z1_led < 0 || z1_led > 100 ||
        z2_led < 0 || z2_led > 100 ||
        (emergency != 0 && emergency != 1)) {
        return false;
    }

    HardwareFrame parsed{};
    parsed.zones[0] = {z1_ldr, z1_dark != 0, z1_object != 0, z1_led};
    parsed.zones[1] = {z2_ldr, z2_dark != 0, z2_object != 0, z2_led};
    parsed.emergency = emergency != 0;
    destination = parsed;
    return true;
}

} // namespace lumigrid
