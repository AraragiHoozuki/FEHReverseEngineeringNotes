#pragma once

#include <cstddef>
#include <cstdint>

namespace feh::mod {

enum class FacilityCfgStatus : std::uint8_t {
    attached = 0,
    already_attached = 1,
    waiting_for_field = 2,
    waiting_for_data = 3,
    no_config = 4,
    invalid_config = 5,
    facility_not_found = 6,
    invalid_coordinate = 7,
    create_failed = 8,
    config_changed = 9,
    skycastle_field = 10,
    unsupported_map = 11,
    effect_gate_failed = 12,
};

struct FacilityCfgReport {
    FacilityCfgStatus status{FacilityCfgStatus::waiting_for_field};
    std::uint16_t configured{};
    std::uint16_t attached{};
};

[[nodiscard]] FacilityCfgReport ApplyExternalCfgFacilities(
    std::uintptr_t module_base) noexcept;

}  // namespace feh::mod
