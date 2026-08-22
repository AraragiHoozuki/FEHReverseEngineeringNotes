#pragma once

#include "feh/srpgmap_enemy_engage.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace feh::mod {

inline constexpr std::size_t kMaximumCfgEngageEntries = 64;
inline constexpr const char* kCfgDirectory = "/data/local/tmp/feh-engage";

struct ExternalEngageEntry {
    std::int16_t x{};
    std::int16_t y{};
    std::int32_t level{};
    std::string target_pid;
    std::string engage_pid;
};

struct ExternalEngageConfig {
    std::array<ExternalEngageEntry, kMaximumCfgEngageEntries> entries{};
    std::size_t count{};
    std::uint64_t fingerprint{};
    bool valid{};
    bool has_facility_section{};
};

enum class ExternalCfgStatus : std::uint8_t {
    applied = 0,
    already_scanned = 1,
    waiting_for_map = 2,
    waiting_for_units = 3,
    no_config = 4,
    invalid_runtime_state = 5,
    missing_binding = 6,
    invalid_config = 7,
};

struct ExternalCfgReport {
    ExternalCfgStatus status{ExternalCfgStatus::waiting_for_map};
    std::uint16_t configured{};
    std::uint16_t applied{};
    std::uint16_t unmatched{};
    std::string map_id;
    std::string path;
};

struct ExternalCfgBindings {
    EnemyEngageBindings engage{};
    GetCoordinateFn get_coordinate_x{};
    GetCoordinateFn get_coordinate_y{};
    GetPersonByPidFn get_person_by_pid{};
    TransformCkiStringFn transform_cki_string{};
    ConstructStringFn construct_string{};
    DestroyStringFn destroy_string{};
    MakeLookupStringFn make_lookup_string{};
    PackLookupStringFn pack_lookup_string{};
    DestroyLookupStringFn destroy_lookup_string{};
    DecodeProtectedStringFn decode_protected_string{};
    GetProtectedPointerFn get_context_vector{};
    GetCurrentMapRootFn get_current_map_root{};
    GetCurrentMapIdFn get_current_map_id{};

    [[nodiscard]] bool ready() const noexcept {
        return engage.ready_for_apply()
            && get_coordinate_x != nullptr
            && get_coordinate_y != nullptr
            && get_person_by_pid != nullptr
            && transform_cki_string != nullptr
            && construct_string != nullptr
            && destroy_string != nullptr
            && make_lookup_string != nullptr
            && pack_lookup_string != nullptr
            && destroy_lookup_string != nullptr
            && decode_protected_string != nullptr
            && get_context_vector != nullptr
            && get_current_map_root != nullptr
            && get_current_map_id != nullptr;
    }
};

[[nodiscard]] ExternalCfgBindings make_external_cfg_bindings(
    std::uintptr_t module_base) noexcept;

[[nodiscard]] bool parse_external_engage_config(
    const char* path,
    ExternalEngageConfig& output) noexcept;

[[nodiscard]] ExternalCfgReport ApplyExternalCfgEngage(
    std::uintptr_t module_base,
    const ExternalCfgBindings& bindings,
    EnemyEngageOptions options = {}) noexcept;

// Returns the map-named CFG selected by the most recent engage pass. The
// facility loader runs immediately afterwards and reuses this exact path so
// both features read one file for the active map.
[[nodiscard]] const char* GetSelectedExternalCfgPath() noexcept;

}  // namespace feh::mod
