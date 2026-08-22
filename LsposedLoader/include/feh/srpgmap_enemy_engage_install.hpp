#pragma once

#include "feh/srpgmap_enemy_engage.hpp"
#include "feh/thumb_hook.hpp"

#include <cstdint>

namespace feh::mod {

using HookDummyFn = ThumbHookFunction;

extern "C" void HookBuildUnitDispatch() noexcept;
extern "C" std::uintptr_t GetBuildUnitResume() noexcept;
extern "C" std::int32_t HookBuildUnitPostImpl(
    std::int32_t built,
    MapUnitIntermediate* source,
    RuntimeUnit* unit) noexcept;

void ConfigureBuildUnitResume(std::uintptr_t address) noexcept;

struct EnemyEngageInstallResult {
    bool installed{false};
    bool target_verified{false};
    int hook_status{-1};
    BuildUnitFn original_build_unit{};
};

inline constexpr int kEnemyEngageTargetSignatureMismatch = -6;

// Install the normal-unit conversion hook for the current ARM32 build.
// The dispatcher lives in this payload and reruns the overwritten target
// prologue before branching into the untouched function body. This avoids an
// anonymous executable ARM trampoline, which Houdini cannot safely translate.
//
// `module_base` must be libcocos2dcpp.so's ELF load bias, not the file offset
// and not an IDA absolute address. All RVAs in this header are Thumb-2
// function addresses; the low bit is added to each branch destination.
[[nodiscard]] inline EnemyEngageInstallResult InstallEnemyEngageHook(
    std::uintptr_t module_base,
    EnemyEngageOptions options = {}) noexcept {
    EnemyEngageInstallResult result{};
    if (module_base == 0) {
        return result;
    }

    if (!verify_build_unit_target(module_base)) {
        result.hook_status = kEnemyEngageTargetSignatureMismatch;
        return result;
    }
    result.target_verified = true;

    auto* target = reinterpret_cast<void*>(
        (module_base + rva::kBuildUnit) | std::uintptr_t{1});
    ConfigureEnemyEngageHook(
        make_enemy_engage_bindings(module_base, nullptr),
        options);
    ConfigureBuildUnitResume(
        (module_base + rva::kBuildUnit + rva::kBuildUnitPrologue.size())
        | std::uintptr_t{1});

    result.hook_status = InstallThumb32EntryRedirect(
        target,
        reinterpret_cast<HookDummyFn>(&HookBuildUnitDispatch));
    if (result.hook_status != 0) {
        return result;
    }

    result.installed = g_enemy_engage_bindings.ready_for_apply();
    return result;
}

}  // namespace feh::mod
