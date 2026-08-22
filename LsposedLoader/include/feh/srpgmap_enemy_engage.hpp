#pragma once

#include "feh/srpgmap.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

namespace feh::mod {

struct RuntimeUnit;
struct PersonDefinition;

namespace rva {

// FEH 10.8.0 armeabi-v7a libcocos2dcpp.so
// SHA-256: BCC5B06D439DABA429B5C75869892F9308CCCA9E135B57C7E074135282A3AE6A
inline constexpr std::uintptr_t kBuildUnit = 0x159DB68;
inline constexpr std::uintptr_t kGetMapReferencePerson = 0x153DDE8;
inline constexpr std::uintptr_t kGetMapArrayPointer = 0x153CD3C;
inline constexpr std::uintptr_t kGetMapProtectedInt = 0x12D5E9C;
inline constexpr std::uintptr_t kGetMapProtectedBool = 0x12CF210;
inline constexpr std::uintptr_t kSetMapProtectedBool = 0x12D4174;
inline constexpr std::uintptr_t kGetMapSide = 0x131DDC8;
// Protected map-process state getter used by ProcTurn's readiness check.
// Signature: state = get_state(map_mode + 4).
inline constexpr std::uintptr_t kGetMapProcessState = 0x131DD3C;
inline constexpr std::uintptr_t kGetCoordinateX = 0x1325E00;
inline constexpr std::uintptr_t kGetCoordinateY = 0x1325E0A;
inline constexpr std::uintptr_t kGetPersonByPid = 0x133CD94;
// Cki string storage is XOR-transformed in the game data.  This helper is
// the runtime's symmetric transform used by sub_15A0638 before Cki lookup.
inline constexpr std::uintptr_t kTransformCkiString = 0x15A0624;
// Kept for compatibility with older CFG binding callers. External CFG lookup
// now walks the game's already-loaded Person registry strings directly.
inline constexpr std::uintptr_t kConstructString = 0x1D28670;
inline constexpr std::uintptr_t kDestroyString = 0x1D2875A;
// Game lookup callers first convert a normal Cki::String into the packed
// lookup representation before calling get_person_by_pid.
inline constexpr std::uintptr_t kMakeLookupString = 0x12CAC58;
// The lookup table consumes libc++'s packed string representation.  This is
// the game's own packer and accepts a raw byte range, so external CFG text can
// be converted without first constructing a Cki::String object.
inline constexpr std::uintptr_t kPackLookupString = 0x1D39316;
inline constexpr std::uintptr_t kDestroyLookupString = 0x1D312D0;
inline constexpr std::uintptr_t kDecodeProtectedString = 0x12D36E0;
inline constexpr std::uintptr_t kGetMapContextVector = 0x166F59C;
inline constexpr std::uintptr_t kGetUnitProtectedPointer = 0x12D5DEC;
inline constexpr std::uintptr_t kGetUnitProtectedInt = 0x12DF124;
inline constexpr std::uintptr_t kSetEngagedPerson = 0x17EAA44;
inline constexpr std::uintptr_t kSetUnitProtectedInt = 0x12DFCA8;
// Setter for the compact 12-byte protected integer used by SkyCastleState
// fields such as +0x31C.  kSetUnitProtectedInt above targets the larger
// 24-byte unit wrapper and must not be used for battle-state fields.
inline constexpr std::uintptr_t kSetMapProtectedInt = 0x12DA080;
inline constexpr std::uintptr_t kRecomputeSkillDerivedValue = 0x17E7264;
inline constexpr std::uintptr_t kGetCurrentMapRoot = 0x1613234;
// Returns the active map ID Cki::String from map-mode (+0x30, falling back
// to +0x1C). This is the same helper used by the native SRPGMap loader.
inline constexpr std::uintptr_t kGetCurrentMapId = 0x16132C8;
inline constexpr std::uintptr_t kSelectCurrentMapPartition = 0x177A7E8;
// SkyCastle facility runtime.  The battle data object is initialized globally
// even for ordinary PvE maps; its battle::Map at +0x210 is the native 6x8
// facility grid consumed by FieldSkyCastle's battle-node code.
inline constexpr std::uintptr_t kFacilityLookup = 0x1623894;
// SkyCastle battle-cell query used by the native facility effect walkers.
// Signature: FacilityData* query(GridCoordinate*, side).
inline constexpr std::uintptr_t kSkyCastleFacilityQuery = 0x16A0494;
inline constexpr std::uintptr_t kCreateFacilityNode = 0x1A15644;
// Creates the native FieldSkyCastle node. It internally creates the native
// FacilityNode container, callbacks, per-cell entities, and coordinate hooks.
inline constexpr std::uintptr_t kCreateFacilityFieldNode = 0x16B13D8;
// FieldSkyCastle's coordinate-selection entry. It updates the selected
// facility entity and forwards the state to the top information bar.
inline constexpr std::uintptr_t kSelectSkyCastleFacility = 0x16B158C;
// Normal PvE map input object. The global is a pointer to the live
// srpg::map::node::Input instance; its vtable +664 receives touch events.
inline constexpr std::uintptr_t kMapInputGlobal = 0x29C7FE8;
// Map-input controller global used by the native selection code.  Its +644
// field points at the shared PvE top-information-bar owner.
inline constexpr std::uintptr_t kMapInputControllerGlobal = 0x29C7FEC;
inline constexpr std::uintptr_t kMapInputVtable = 0x26A72BC;
inline constexpr std::uintptr_t kMapInputTouchHandler = 0x16B5C88;
// Normal PvE facility-information path used by Input::UpdateSelection.  The
// native FacilitySelection helper passes the live Input coordinate object at
// +628 and forwards to the shared top information bar without replacing the
// facility entity node.
inline constexpr std::uintptr_t kShowFacilityInfo = 0x16B4EEC;
// Kept for diagnostics and older callers; the field-node entry point above is
// preferred because it exactly matches Field::CreateField's native path.
inline constexpr std::uintptr_t kCreateFacilityContainer = 0x1A1665C;
inline constexpr std::uintptr_t kCreateFacilityEntity = 0x1A16A78;
inline constexpr std::uintptr_t kRegisterFacilityEntity = 0x1A16AE0;
inline constexpr std::uintptr_t kGetFacilityContainerEntity = 0x1A16B0E;
inline constexpr std::uintptr_t kGetFacilityLevel = 0x1A2042C;
inline constexpr std::uintptr_t kGetFacilityId = 0x1A1FAC0;
inline constexpr std::uintptr_t kGetFacilityType = 0x1A1F32C;
inline constexpr std::uintptr_t kGetFacilitySide = 0x1A1FAA4;
inline constexpr std::uintptr_t kFacilityCallback = 0x1A158EC;
inline constexpr std::uintptr_t kFacilityBattleCallback = 0x1A158CC;
inline constexpr std::uintptr_t kDestroyFunctionObject = 0x16B1CEC;
inline constexpr std::uintptr_t kDestroyFacilityLookupFunction = 0x16B1BB6;
// std::function vtable for FieldSkyCastle::CreateField's coordinate lookup
// lambda (the container's first callback). This differs from the plain
// int(int) state callback vtable below.
inline constexpr std::uintptr_t kFacilityLookupCallbackVtable = 0x26A3430;
inline constexpr std::uintptr_t kFacilityStateCallbackVtable = 0x26A3478;
// Kept as an alias for older callers which used the state callback name.
inline constexpr std::uintptr_t kFacilityCallbackVtable =
    kFacilityStateCallbackVtable;
// Builds the protected width/height pair for the currently loaded battle map.
// This is shared by normal PvE and SkyCastle battle logic.
inline constexpr std::uintptr_t kGetCurrentMapGridSize = 0x16A30BC;
// Fills the native map-field bounds rectangle used by Field::CreateField.
// The rectangle's center is the transform origin for field child nodes.
inline constexpr std::uintptr_t kGetMapFieldBounds = 0x16A32D0;
inline constexpr std::uintptr_t kGetMapRectCenterX = 0x221A9BA;
inline constexpr std::uintptr_t kGetMapRectCenterY = 0x221A9F2;
// Returns the live PvE map cell size in scene units. The implementation uses
// srpg::map::Data::scale * 180, so compact 12x16 maps report 90 while maps
// rendered on the native facility lattice report 180.
inline constexpr std::uintptr_t kGetCurrentMapCellSize = 0x16A30D8;
inline constexpr std::uintptr_t kGetGridWidth = 0x147BA60;
inline constexpr std::uintptr_t kGetGridHeight = 0x147BA6A;
inline constexpr std::uintptr_t kConstructGridSize = 0x149390C;
inline constexpr std::uintptr_t kConstructGridCoordinate = 0x147BA76;
// Native SkyCastle grid-to-node-position conversion used by FieldSkyCastle.
inline constexpr std::uintptr_t kFacilityGridToPosition = 0x1A16B40;
inline constexpr std::uintptr_t kFacilityZOrder = 0x1A84BE8;
inline constexpr std::uintptr_t kSkyCastleBattleStateGlobal = 0x29D0F60;
inline constexpr std::uintptr_t kResetSkyCastleBattleBattleMap = 0x1B79634;
inline constexpr std::uintptr_t kSetSkyCastleMapCellKey = 0x1A13184;
inline constexpr std::uintptr_t kGetSkyCastleMapCellKey = 0x16238A8;
inline constexpr std::uintptr_t kActivateSkyCastleFacility = 0x1B7B2F8;
inline constexpr std::uintptr_t kDeactivateSkyCastleFacility = 0x1B7B3BC;
inline constexpr std::uintptr_t kSetSkyCastleFacilityState = 0x1B7B45C;
// Native SkyCastle facility passes. Both functions are guarded by
// map_mode + 556 and walk the live 6x8 facility grid, updating the battle
// state and Field coordinate/effect layers.
inline constexpr std::uintptr_t kSkyCastleFacilityStatePass = 0x16A0B94;
inline constexpr std::uintptr_t kSkyCastleFacilityActivatePass = 0x16A0AB8;
// Native MapProcessSkillTurn construction and its native effect stages.
// The factory links the newly allocated process into the parent passed as
// its only argument.  The facility stage is the FieldSkyCastle path: it
// dispatches FacilityData effects through sub_16F8A14 for each active side.
inline constexpr std::uintptr_t kCreateMapProcessSkillTurn = 0x16F8854;
inline constexpr std::uintptr_t kMapProcessSkillTurnGenericStage = 0x16F82D4;
// Consumes the q120 unit-effect queue produced by the generic FacilityData
// dispatcher and commits the resulting stat/status changes to live units.
inline constexpr std::uintptr_t kMapProcessSkillTurnApplyUnitEffects = 0x16F8364;
inline constexpr std::uintptr_t kMapProcessSkillTurnFacilityStage = 0x16F7EA8;
inline constexpr std::uintptr_t kMapProcessSkillTurnMultiSide = 0x16F89FC;
inline constexpr std::uintptr_t kDestroyMapProcessSkillTurn = 0x170F724;
inline constexpr std::uintptr_t kSetMapSide = 0x153DD14;
// std::function<void()>::operator() used by ProcController. This invokes
// ProcTurn's bound callback, including its native lifecycle preamble.
inline constexpr std::uintptr_t kInvokeProcControllerCallback = 0x12D0652;
// ProcController's normal per-frame lifecycle entry. It validates the
// controller state, invokes the bound ProcTurn callback, and marks it done.
inline constexpr std::uintptr_t kProcControllerLifecycle = 0x178F1BC;
inline constexpr std::uintptr_t kGetMode1Container = 0x12F04F4;
inline constexpr std::uintptr_t kMode1ContainerGetElement = 0x17F3B8C;
inline constexpr std::uintptr_t kInitMapIntermediate = 0x153CF10;
inline constexpr std::uintptr_t kFillMapIntermediate = 0x159CF00;
inline constexpr std::uintptr_t kDestroyMapIntermediate = 0x153D01C;

inline constexpr std::uintptr_t kCurrentMapOwnerGlobal = 0x29C9904;
// Native MapProcessSkillTurn reads the active side from the live map state
// object at dword_29C7C54 + 28, not from map_mode + 4.
inline constexpr std::uintptr_t kCurrentMapStateGlobal = 0x29C7C54;
// Polymorphic live map Field selected by the map-mode dispatcher.  Depending
// on mode this global can point to the generic Field, FieldMjolnir, or
// FieldSkyCastle implementation; it is not itself a SkyCastle facility host.
inline constexpr std::uintptr_t kCurrentFieldGlobal = 0x29C7FA0;
inline constexpr std::uintptr_t kFieldSkyCastleVtable = 0x26A3124;
inline constexpr std::uintptr_t kFacilityManagerGlobal = 0x29CE590;
inline constexpr std::uintptr_t kMapModeGlobal = 0x29C9940;
inline constexpr std::uintptr_t kPersonRegistryGlobal = 0x29C618C;
inline constexpr std::uintptr_t kRuntimeUnitManagerGlobal = 0x29C7020;
inline constexpr std::uintptr_t kMode1ContainerGlobal = 0x29C9AEC;
// srpg::map::Data allocated by sub_166B500. Its +0x18/+0x1C pair is the
// begin/end range of MapUnitIntermediate records (stride 0x260).
inline constexpr std::uintptr_t kMapDataGlobal = 0x29C72EC;

// The first two complete Thumb instructions at BuildUnit for the supported
// 10.8.0 armeabi-v7a image. Check them before replacing the entry point so a
// stale RVA cannot patch an unrelated function after a game update.
inline constexpr std::array<std::uint8_t, 8> kBuildUnitPrologue = {
    0xF0, 0xB5, 0x03, 0xAF, 0x2D, 0xE9, 0xF0, 0x07,
};

}  // namespace rva

namespace offset {

inline constexpr std::size_t kPersonCategory = 0x20;
inline constexpr std::size_t kCategoryKind = 0x18;
inline constexpr std::uint8_t kEngageHeroCategory = 38;

inline constexpr std::size_t kUnitMainPerson = 0x054;
inline constexpr std::size_t kUnitSkillDerivedValue = 0x248;
inline constexpr std::size_t kUnitEngagedPerson = 0x380;
inline constexpr std::size_t kUnitEngageLimitBreak = 0x38C;
inline constexpr std::size_t kRuntimeUnitSize = 0x4F0;

inline constexpr std::size_t kMapOwnerCurrentRoot = 0x050;
inline constexpr std::size_t kMapPartitionSourceVector = 0x078;
inline constexpr std::size_t kMapPartitionSourceCount = 0x090;
inline constexpr std::size_t kSourceSide = 0x12C;
inline constexpr std::size_t kRuntimeUnitRosterBase = 0x404;
inline constexpr std::size_t kRuntimeUnitRosterStride = 0x00C;
inline constexpr std::size_t kRuntimeUnitRosterCount = 4;
inline constexpr std::size_t kMapDataUnitVector = 0x018;
inline constexpr std::size_t kMapDataUnitVectorEnd = 0x01C;
inline constexpr std::size_t kMapDataPayloadSourceVector = 0x018;
inline constexpr std::size_t kMapDataPayloadSourceCount = 0x024;
inline constexpr std::size_t kMapDataDescriptorStride = 0x080;

}  // namespace offset

using BuildUnitFn = std::int32_t (*)(MapUnitIntermediate*, RuntimeUnit*);
using GetMapReferencePersonFn = PersonDefinition* (*)(RuntimeProtectedPointer24*);
using GetMapArrayPointerFn = MapUnitIntermediate* (*)(void*);
using GetMapProtectedIntFn = std::int32_t (*)(RuntimeProtectedScalar32*);
using GetMapProtectedBoolFn = bool (*)(std::uint8_t*);
using GetMapSideFn = std::int32_t (*)(void*);
using GetCoordinateFn = std::int32_t (*)(void*);
using GetPersonByPidFn = PersonDefinition* (*)(void*, void*);
using TransformCkiStringFn = std::int32_t (*)(void*, std::uint32_t, const char*);
using ConstructStringFn = std::int32_t (*)(void*, const char*);
using DestroyStringFn = void (*)(void*);
using MakeLookupStringFn = std::int32_t (*)(void*, void*);
using PackLookupStringFn = std::int32_t (*)(void*, const void*, std::uint32_t);
using DestroyLookupStringFn = void (*)(void*);
using DecodeProtectedStringFn = std::int32_t (*)(void*, void*);
using GetProtectedPointerFn = void* (*)(void*);
using GetUnitProtectedPointerFn = void* (*)(void*);
using SetEngagedPersonFn = std::int32_t (*)(RuntimeUnit*, PersonDefinition*, std::int32_t);
using SetUnitProtectedIntFn = std::int32_t (*)(void*, std::int32_t);
using RecomputeSkillDerivedValueFn = std::int32_t (*)(RuntimeUnit*, std::int32_t, std::int32_t);
using GetCurrentMapRootFn = void* (*)(void*);
using GetCurrentMapIdFn = std::int32_t (*)(void*, void*);
using SelectCurrentMapPartitionFn = void* (*)(void*);
using InitMapIntermediateFn = MapUnitIntermediate* (*)(MapUnitIntermediate*);
using FillMapIntermediateFn = std::int32_t (*)(
    MapUnitIntermediate*, void*, std::int32_t, std::int32_t);
using DestroyMapIntermediateFn = MapUnitIntermediate* (*)(MapUnitIntermediate*);

struct EnemyEngageBindings {
    // Must be the hook framework's trampoline, not the patched function entry.
    BuildUnitFn original_build_unit{};
    GetMapReferencePersonFn get_map_reference_person{};
    GetMapProtectedIntFn get_map_protected_int{};
    GetUnitProtectedPointerFn get_unit_protected_pointer{};
    SetEngagedPersonFn set_engaged_person{};
    SetUnitProtectedIntFn set_unit_protected_int{};
    RecomputeSkillDerivedValueFn recompute_skill_derived_value{};

    [[nodiscard]] constexpr bool ready_for_apply() const noexcept {
        return get_map_reference_person != nullptr
               && get_map_protected_int != nullptr
               && get_unit_protected_pointer != nullptr
               && set_engaged_person != nullptr
               && set_unit_protected_int != nullptr
               && recompute_skill_derived_value != nullptr;
    }
};

struct EnemyEngageOptions {
    // -1 disables the optional upper-bound check. This header deliberately
    // does not guess the version-specific master-data maximum.
    std::int32_t maximum_limit_break_level{-1};
};

struct LoadedMapEngageBindings {
    EnemyEngageBindings engage{};
    GetMapArrayPointerFn get_map_array_pointer{};
    GetMapSideFn get_map_side{};
    GetMapProtectedBoolFn get_map_protected_bool{};
    GetCurrentMapRootFn get_current_map_root{};
    SelectCurrentMapPartitionFn select_current_map_partition{};
    InitMapIntermediateFn init_map_intermediate{};
    FillMapIntermediateFn fill_map_intermediate{};
    DestroyMapIntermediateFn destroy_map_intermediate{};

    [[nodiscard]] constexpr bool ready() const noexcept {
        return engage.ready_for_apply()
               && get_map_array_pointer != nullptr
               && get_map_side != nullptr
        && get_current_map_root != nullptr
               && select_current_map_partition != nullptr;
    }
};

enum class LoadedMapEngageStatus : std::uint8_t {
    applied = 0,
    already_scanned = 1,
    waiting_for_map = 2,
    waiting_for_units = 3,
    no_configured_units = 4,
    invalid_runtime_state = 5,
    missing_binding = 6,
};

struct LoadedMapEngageReport {
    LoadedMapEngageStatus status{LoadedMapEngageStatus::waiting_for_map};
    std::uint16_t configured{};
    std::uint16_t applied{};
    std::uint16_t unmatched{};
};

enum class EnemyEngageApplyResult {
    applied,
    missing_binding,
    null_input,
    not_fixed_record,
    no_reference_person,
    reference_is_not_engage_hero,
    same_as_main_person,
    invalid_limit_break_level,
};

[[nodiscard]] inline bool is_engage_hero(const PersonDefinition* person) noexcept;

// CFG-driven application deliberately does not take a MapUnitIntermediate.
// The target unit is selected from the external X,Y,pid row and the Engage
// hero is resolved through the game's Person registry.
[[nodiscard]] inline EnemyEngageApplyResult TryApplyDirectEngage(
    RuntimeUnit* unit,
    PersonDefinition* engaged_person,
    std::int32_t level,
    const EnemyEngageBindings& bindings,
    EnemyEngageOptions options = {}) noexcept {
    if (!bindings.ready_for_apply()) {
        return EnemyEngageApplyResult::missing_binding;
    }
    if (unit == nullptr || engaged_person == nullptr) {
        return EnemyEngageApplyResult::null_input;
    }
    if (!is_engage_hero(engaged_person)) {
        return EnemyEngageApplyResult::reference_is_not_engage_hero;
    }
    auto* unit_bytes = reinterpret_cast<std::byte*>(unit);
    auto* main_person = static_cast<PersonDefinition*>(
        bindings.get_unit_protected_pointer(
            unit_bytes + offset::kUnitMainPerson));
    if (main_person == engaged_person) {
        return EnemyEngageApplyResult::same_as_main_person;
    }
    if (level < 0
        || (options.maximum_limit_break_level >= 0
            && level > options.maximum_limit_break_level)) {
        return EnemyEngageApplyResult::invalid_limit_break_level;
    }

    bindings.set_engaged_person(unit, engaged_person, 1);
    bindings.set_unit_protected_int(
        unit_bytes + offset::kUnitEngageLimitBreak, level);
    const auto derived = bindings.recompute_skill_derived_value(unit, 0, 1);
    bindings.set_unit_protected_int(
        unit_bytes + offset::kUnitSkillDerivedValue,
        static_cast<std::int32_t>(derived));
    return EnemyEngageApplyResult::applied;
}

[[nodiscard]] inline bool matches_build_unit_prologue(
    std::span<const std::byte> bytes) noexcept {
    if (bytes.size() < rva::kBuildUnitPrologue.size()) {
        return false;
    }
    for (std::size_t index = 0; index < rva::kBuildUnitPrologue.size(); ++index) {
        if (std::to_integer<std::uint8_t>(bytes[index])
            != rva::kBuildUnitPrologue[index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool verify_build_unit_target(
    std::uintptr_t module_base) noexcept {
    if (module_base == 0) {
        return false;
    }
    const auto* target = reinterpret_cast<const std::byte*>(
        module_base + rva::kBuildUnit);
    return matches_build_unit_prologue(
        std::span<const std::byte>{target, rva::kBuildUnitPrologue.size()});
}

template <typename Function>
[[nodiscard]] inline Function resolve_game_function(
    std::uintptr_t module_base,
    std::uintptr_t function_rva) noexcept {
    static_assert(std::is_pointer_v<Function>);
    // Every confirmed target in this armeabi-v7a binary is Thumb code.  The
    // low bit is part of an ARM32 function pointer even though IDA displays an
    // aligned instruction address.
    return reinterpret_cast<Function>((module_base + function_rva) | 1U);
}

[[nodiscard]] inline EnemyEngageBindings make_enemy_engage_bindings(
    std::uintptr_t module_base,
    BuildUnitFn original_build_unit) noexcept {
    EnemyEngageBindings result{};
    result.original_build_unit = original_build_unit;
    result.get_map_reference_person = resolve_game_function<GetMapReferencePersonFn>(
        module_base, rva::kGetMapReferencePerson);
    result.get_map_protected_int = resolve_game_function<GetMapProtectedIntFn>(
        module_base, rva::kGetMapProtectedInt);
    result.get_unit_protected_pointer = resolve_game_function<GetUnitProtectedPointerFn>(
        module_base, rva::kGetUnitProtectedPointer);
    result.set_engaged_person = resolve_game_function<SetEngagedPersonFn>(
        module_base, rva::kSetEngagedPerson);
    result.set_unit_protected_int = resolve_game_function<SetUnitProtectedIntFn>(
        module_base, rva::kSetUnitProtectedInt);
    result.recompute_skill_derived_value =
        resolve_game_function<RecomputeSkillDerivedValueFn>(
            module_base, rva::kRecomputeSkillDerivedValue);
    return result;
}

[[nodiscard]] inline LoadedMapEngageBindings make_loaded_map_engage_bindings(
    std::uintptr_t module_base) noexcept {
    LoadedMapEngageBindings result{};
    result.engage = make_enemy_engage_bindings(module_base, nullptr);
    result.get_map_array_pointer = resolve_game_function<GetMapArrayPointerFn>(
        module_base, rva::kGetMapArrayPointer);
    result.get_map_side = resolve_game_function<GetMapSideFn>(
        module_base, rva::kGetMapSide);
    result.get_map_protected_bool =
        resolve_game_function<GetMapProtectedBoolFn>(
            module_base, rva::kGetMapProtectedBool);
    result.get_current_map_root = resolve_game_function<GetCurrentMapRootFn>(
        module_base, rva::kGetCurrentMapRoot);
    result.select_current_map_partition =
        resolve_game_function<SelectCurrentMapPartitionFn>(
            module_base, rva::kSelectCurrentMapPartition);
    result.init_map_intermediate = resolve_game_function<InitMapIntermediateFn>(
        module_base, rva::kInitMapIntermediate);
    result.fill_map_intermediate = resolve_game_function<FillMapIntermediateFn>(
        module_base, rva::kFillMapIntermediate);
    result.destroy_map_intermediate =
        resolve_game_function<DestroyMapIntermediateFn>(
            module_base, rva::kDestroyMapIntermediate);
    return result;
}

[[nodiscard]] inline bool is_engage_hero(const PersonDefinition* person) noexcept {
    if (person == nullptr) {
        return false;
    }

    const auto* person_bytes = reinterpret_cast<const std::byte*>(person);
    const void* category = nullptr;
    std::memcpy(&category, person_bytes + offset::kPersonCategory, sizeof(category));
    if (category == nullptr) {
        return false;
    }

    const auto* category_bytes = static_cast<const std::byte*>(category);
    return std::to_integer<std::uint8_t>(category_bytes[offset::kCategoryKind])
           == offset::kEngageHeroCategory;
}

[[nodiscard]] inline EnemyEngageApplyResult TryApplyMapEngage(
    MapUnitIntermediate* source,
    RuntimeUnit* unit,
    const EnemyEngageBindings& bindings,
    EnemyEngageOptions options = {}) noexcept {
    if (!bindings.ready_for_apply()) {
        return EnemyEngageApplyResult::missing_binding;
    }
    if (source == nullptr || unit == nullptr) {
        return EnemyEngageApplyResult::null_input;
    }

    // sub_177A6D4 immediately builds records with quota <= 0. Records with a
    // positive quota enter the reinforcement-template path where these tail
    // fields retain their original meanings and must not be reinterpreted.
    if (bindings.get_map_protected_int(&source->spawn_quota) >= 1) {
        return EnemyEngageApplyResult::not_fixed_record;
    }

    auto* engaged_person = bindings.get_map_reference_person(&source->reference_person);
    if (engaged_person == nullptr) {
        return EnemyEngageApplyResult::no_reference_person;
    }
    if (!is_engage_hero(engaged_person)) {
        return EnemyEngageApplyResult::reference_is_not_engage_hero;
    }

    auto* unit_bytes = reinterpret_cast<std::byte*>(unit);
    auto* main_person = static_cast<PersonDefinition*>(bindings.get_unit_protected_pointer(
        unit_bytes + offset::kUnitMainPerson));
    if (main_person == engaged_person) {
        return EnemyEngageApplyResult::same_as_main_person;
    }

    // For fixed records the protocol reuses record+0x79 / intermediate+0x1E4
    // as the Engage limit-break level.
    const auto level = bindings.get_map_protected_int(&source->not_before_turn);
    if (level < 0
        || (options.maximum_limit_break_level >= 0
            && level > options.maximum_limit_break_level)) {
        return EnemyEngageApplyResult::invalid_limit_break_level;
    }

    // force=1 is required because an enemy Engage hero usually is not present
    // in the player's Engage manager. The setter may consequently store -1 in
    // +0x38C, so overwrite that protected integer with the map-supplied level.
    bindings.set_engaged_person(unit, engaged_person, 1);
    bindings.set_unit_protected_int(
        unit_bytes + offset::kUnitEngageLimitBreak,
        level);

    // sub_17E9F54 uses (unit, 0, 1) and stores the result at Unit+0x248.
    const auto derived = bindings.recompute_skill_derived_value(unit, 0, 1);
    bindings.set_unit_protected_int(
        unit_bytes + offset::kUnitSkillDerivedValue,
        static_cast<std::int32_t>(derived));
    return EnemyEngageApplyResult::applied;
}

namespace detail {

struct RuntimeUnitCandidate {
    RuntimeUnit* unit{};
    std::int32_t side{-1};
    bool used{};
};

inline constexpr std::size_t kMaximumSourceCount = 512;
inline constexpr std::size_t kMaximumUnitsPerRoster = 64;
inline constexpr std::size_t kMaximumRuntimeUnits =
    offset::kRuntimeUnitRosterCount * kMaximumUnitsPerRoster;

[[nodiscard]] inline std::uintptr_t mix_signature(
    std::uintptr_t current,
    std::uintptr_t value) noexcept {
    return (current ^ value) * std::uintptr_t{16777619U};
}

}  // namespace detail

// Mode 1 keeps the serialized unit descriptors in an internal payload rather
// than publishing the converted 0x260-byte records in srpg::map::Data. Build a
// temporary intermediate with the game's own conversion routines, use it only
// for matching/applying Engage, then destroy it through the matching game
// destructor.
[[nodiscard]] inline LoadedMapEngageReport ApplyMode1TemplateEngage(
    std::uintptr_t module_base,
    const LoadedMapEngageBindings& bindings,
    EnemyEngageOptions options = {}) noexcept {
    LoadedMapEngageReport report{};
    if (module_base == 0
        || !bindings.ready()
        || bindings.init_map_intermediate == nullptr
        || bindings.fill_map_intermediate == nullptr
        || bindings.destroy_map_intermediate == nullptr) {
        report.status = LoadedMapEngageStatus::missing_binding;
        return report;
    }

    auto* map_data = *reinterpret_cast<std::byte**>(
        module_base + rva::kMapDataGlobal);
    if (map_data == nullptr) {
        report.status = LoadedMapEngageStatus::waiting_for_map;
        return report;
    }

    std::byte* map_data_owner = nullptr;
    std::memcpy(&map_data_owner, map_data + 4, sizeof(map_data_owner));
    if (map_data_owner == nullptr) {
        report.status = LoadedMapEngageStatus::waiting_for_map;
        return report;
    }

    std::byte* payload = nullptr;
    std::memcpy(&payload, map_data_owner + 8, sizeof(payload));
    if (payload == nullptr) {
        report.status = LoadedMapEngageStatus::waiting_for_map;
        return report;
    }

    std::byte* descriptor_begin = nullptr;
    std::memcpy(
        &descriptor_begin,
        payload + offset::kMapDataPayloadSourceVector,
        sizeof(descriptor_begin));
    std::uint32_t encoded_count = 0;
    std::memcpy(
        &encoded_count,
        payload + offset::kMapDataPayloadSourceCount,
        sizeof(encoded_count));
    const auto descriptor_count = static_cast<std::int32_t>(
        encoded_count ^ 0xAC6710EEU);
    if (descriptor_count < 0
        || descriptor_count > 64
        || (descriptor_count != 0 && descriptor_begin == nullptr)) {
        report.status = LoadedMapEngageStatus::invalid_runtime_state;
        return report;
    }

    auto* mode1_owner = *reinterpret_cast<std::byte**>(
        module_base + rva::kMode1ContainerGlobal);
    if (mode1_owner == nullptr) {
        report.status = LoadedMapEngageStatus::waiting_for_map;
        return report;
    }
    using GetMode1ContainerFn = void* (*)(void*);
    using GetMode1ElementFn = RuntimeUnit* (*)(void*, std::int32_t);
    const auto get_mode1_container =
        resolve_game_function<GetMode1ContainerFn>(
            module_base, rva::kGetMode1Container);
    auto* mode1_container = static_cast<std::byte*>(
        get_mode1_container(mode1_owner + 168));
    if (mode1_container == nullptr) {
        report.status = LoadedMapEngageStatus::waiting_for_map;
        return report;
    }

    std::uintptr_t mode1_vtable = 0;
    std::memcpy(&mode1_vtable, mode1_container, sizeof(mode1_vtable));
    const auto expected_getter =
        (module_base + rva::kMode1ContainerGetElement) | 1U;
    if (mode1_vtable == 0) {
        report.status = LoadedMapEngageStatus::invalid_runtime_state;
        return report;
    }
    std::uintptr_t mode1_getter = 0;
    std::memcpy(&mode1_getter, reinterpret_cast<const std::byte*>(mode1_vtable),
                sizeof(mode1_getter));
    if (mode1_getter != expected_getter) {
        report.status = LoadedMapEngageStatus::invalid_runtime_state;
        return report;
    }

    std::int32_t mode1_count = -1;
    std::memcpy(&mode1_count, mode1_container + 12, sizeof(mode1_count));
    if (mode1_count < 0 || mode1_count > 64) {
        report.status = LoadedMapEngageStatus::invalid_runtime_state;
        return report;
    }

    std::uintptr_t signature = detail::mix_signature(
        reinterpret_cast<std::uintptr_t>(descriptor_begin),
        static_cast<std::uintptr_t>(descriptor_count));
    signature = detail::mix_signature(
        signature, reinterpret_cast<std::uintptr_t>(mode1_container));

    std::array<RuntimeUnit*, detail::kMaximumRuntimeUnits> templates{};
    std::size_t template_count = 0;
    // In mode 1 the container at kMode1ContainerGlobal is a template/cache
    // and may hold PersonDefinition copies unrelated to the live enemy units.
    // The converted map descriptor, however, resolves to the same Person
    // object stored in the runtime roster. Collect those live units first so
    // pointer identity remains the strongest available match key.
    auto* runtime_unit_manager = *reinterpret_cast<std::byte**>(
        module_base + rva::kRuntimeUnitManagerGlobal);
    if (runtime_unit_manager != nullptr) {
        for (std::size_t side = 0;
             side < offset::kRuntimeUnitRosterCount;
             ++side) {
            auto* roster = runtime_unit_manager
                + offset::kRuntimeUnitRosterBase
                + side * offset::kRuntimeUnitRosterStride;
            RuntimeUnit** begin = nullptr;
            RuntimeUnit** end = nullptr;
            RuntimeUnit** capacity = nullptr;
            std::memcpy(&begin, roster, sizeof(begin));
            std::memcpy(&end, roster + sizeof(begin), sizeof(end));
            std::memcpy(
                &capacity,
                roster + sizeof(begin) + sizeof(end),
                sizeof(capacity));
            if (begin == nullptr && end == nullptr && capacity == nullptr) {
                continue;
            }
            if (begin == nullptr || end < begin || capacity < end) {
                report.status = LoadedMapEngageStatus::invalid_runtime_state;
                return report;
            }
            const auto count = static_cast<std::size_t>(end - begin);
            if (count > detail::kMaximumUnitsPerRoster
                || template_count + count > templates.size()) {
                report.status = LoadedMapEngageStatus::invalid_runtime_state;
                return report;
            }
            for (std::size_t index = 0; index < count; ++index) {
                auto* unit = begin[index];
                if (unit == nullptr) {
                    continue;
                }
                templates[template_count++] = unit;
                signature = detail::mix_signature(
                    signature, reinterpret_cast<std::uintptr_t>(unit));
            }
        }
    }
    const auto get_element = reinterpret_cast<GetMode1ElementFn>(mode1_getter);
    for (std::int32_t index = 0; index < mode1_count; ++index) {
        auto* unit = get_element(mode1_container, index);
        if (unit == nullptr) {
            continue;
        }
        if (template_count >= templates.size()) {
            report.status = LoadedMapEngageStatus::invalid_runtime_state;
            return report;
        }
        templates[template_count++] = unit;
        signature = detail::mix_signature(
            signature, reinterpret_cast<std::uintptr_t>(unit));
    }
    if (template_count == 0) {
        report.status = LoadedMapEngageStatus::waiting_for_units;
        return report;
    }

    static std::uintptr_t last_completed_signature = 0;
    if (signature == last_completed_signature) {
        report.status = LoadedMapEngageStatus::already_scanned;
        return report;
    }

    std::array<bool, detail::kMaximumRuntimeUnits> used{};
    for (std::int32_t index = 0; index < descriptor_count; ++index) {
        auto* descriptor = descriptor_begin
            + static_cast<std::size_t>(index)
                * offset::kMapDataDescriptorStride;
        MapUnitIntermediate source{};
        bindings.init_map_intermediate(&source);
        bindings.fill_map_intermediate(&source, descriptor, 0, 0);

        auto* engage_person = bindings.engage.get_map_reference_person(
            &source.reference_person);
        if (!is_engage_hero(engage_person)) {
            bindings.destroy_map_intermediate(&source);
            continue;
        }
        ++report.configured;

        auto* main_person = bindings.engage.get_map_reference_person(
            reinterpret_cast<RuntimeProtectedPointer24*>(&source));
        RuntimeUnit* match = nullptr;
        std::size_t match_index = 0;
        for (std::size_t unit_index = 0;
             unit_index < template_count;
             ++unit_index) {
            if (used[unit_index]) {
                continue;
            }
            auto* unit_bytes = reinterpret_cast<std::byte*>(templates[unit_index]);
            auto* unit_main_person = static_cast<PersonDefinition*>(
                bindings.engage.get_unit_protected_pointer(
                    unit_bytes + offset::kUnitMainPerson));
            if (unit_main_person == main_person) {
                match = templates[unit_index];
                match_index = unit_index;
                break;
            }
        }

        if (match == nullptr) {
            ++report.unmatched;
        } else {
            used[match_index] = true;
            if (TryApplyMapEngage(
                    &source,
                    match,
                    bindings.engage,
                    options) == EnemyEngageApplyResult::applied) {
                ++report.applied;
            }
        }
        bindings.destroy_map_intermediate(&source);
    }

    if (report.configured == 0) {
        last_completed_signature = signature;
        report.status = LoadedMapEngageStatus::no_configured_units;
    } else if (report.unmatched != 0) {
        report.status = LoadedMapEngageStatus::waiting_for_units;
    } else {
        last_completed_signature = signature;
        report.status = LoadedMapEngageStatus::applied;
    }
    return report;
}

// Apply Engage metadata to units which have already been built by FEH. This
// path is intended for Native Bridge environments such as Houdini: it only
// reads game-owned containers and invokes existing game functions. It never
// patches an ARM instruction or redirects guest control flow.
[[nodiscard]] inline LoadedMapEngageReport ApplyLoadedMapEngage(
    std::uintptr_t module_base,
    const LoadedMapEngageBindings& bindings,
    EnemyEngageOptions options = {}) noexcept {
    LoadedMapEngageReport report{};
    if (module_base == 0 || !bindings.ready()) {
        report.status = LoadedMapEngageStatus::missing_binding;
        return report;
    }

    auto* map_mode = *reinterpret_cast<std::byte**>(
        module_base + rva::kMapModeGlobal);
    if (map_mode != nullptr
        && bindings.get_map_side != nullptr
        && bindings.get_map_side(map_mode + 4) == 1) {
        return ApplyMode1TemplateEngage(module_base, bindings, options);
    }

    auto* map_owner = *reinterpret_cast<std::byte**>(
        module_base + rva::kCurrentMapOwnerGlobal);
    auto* unit_manager = *reinterpret_cast<std::byte**>(
        module_base + rva::kRuntimeUnitManagerGlobal);
    if (map_owner == nullptr || unit_manager == nullptr) {
        report.status = LoadedMapEngageStatus::waiting_for_map;
        return report;
    }

    auto* map_root = bindings.get_current_map_root(
        map_owner + offset::kMapOwnerCurrentRoot);
    if (map_root == nullptr) {
        report.status = LoadedMapEngageStatus::waiting_for_map;
        return report;
    }
    auto* partition = bindings.select_current_map_partition(map_root);
    if (partition == nullptr) {
        report.status = LoadedMapEngageStatus::waiting_for_map;
        return report;
    }

    auto* partition_bytes = static_cast<std::byte*>(partition);
    const auto source_count = bindings.engage.get_map_protected_int(
        reinterpret_cast<RuntimeProtectedScalar32*>(
            partition_bytes + offset::kMapPartitionSourceCount));
    auto* sources = bindings.get_map_array_pointer(
        partition_bytes + offset::kMapPartitionSourceVector);
    if (source_count < 0
        || static_cast<std::size_t>(source_count)
               > detail::kMaximumSourceCount
        || (source_count != 0 && sources == nullptr)) {
        report.status = LoadedMapEngageStatus::invalid_runtime_state;
        return report;
    }

    std::array<detail::RuntimeUnitCandidate, detail::kMaximumRuntimeUnits>
        runtime_units{};
    std::size_t runtime_unit_count = 0;
    std::uintptr_t signature = detail::mix_signature(
        reinterpret_cast<std::uintptr_t>(sources),
        static_cast<std::uintptr_t>(source_count));

    for (std::size_t side = 0;
         side < offset::kRuntimeUnitRosterCount;
         ++side) {
        auto* roster = unit_manager + offset::kRuntimeUnitRosterBase
                       + side * offset::kRuntimeUnitRosterStride;
        RuntimeUnit** begin = nullptr;
        RuntimeUnit** end = nullptr;
        RuntimeUnit** capacity = nullptr;
        std::memcpy(&begin, roster, sizeof(begin));
        std::memcpy(&end, roster + sizeof(begin), sizeof(end));
        std::memcpy(
            &capacity,
            roster + sizeof(begin) + sizeof(end),
            sizeof(capacity));

        if (begin == nullptr && end == nullptr && capacity == nullptr) {
            continue;
        }
        if (begin == nullptr || end < begin || capacity < end) {
            report.status = LoadedMapEngageStatus::invalid_runtime_state;
            return report;
        }
        const auto count = static_cast<std::size_t>(end - begin);
        if (count > detail::kMaximumUnitsPerRoster
            || runtime_unit_count + count > runtime_units.size()) {
            report.status = LoadedMapEngageStatus::invalid_runtime_state;
            return report;
        }
        for (std::size_t index = 0; index < count; ++index) {
            auto* unit = begin[index];
            if (unit == nullptr) {
                continue;
            }
            runtime_units[runtime_unit_count++] = {
                unit,
                static_cast<std::int32_t>(side),
                false,
            };
            signature = detail::mix_signature(
                signature, reinterpret_cast<std::uintptr_t>(unit));
        }
    }

    if (runtime_unit_count == 0) {
        report.status = LoadedMapEngageStatus::waiting_for_units;
        return report;
    }

    static std::uintptr_t last_completed_signature = 0;
    if (signature == last_completed_signature) {
        report.status = LoadedMapEngageStatus::already_scanned;
        return report;
    }

    for (std::int32_t index = 0; index < source_count; ++index) {
        auto* source = reinterpret_cast<MapUnitIntermediate*>(
            reinterpret_cast<std::byte*>(sources)
            + static_cast<std::size_t>(index) * sizeof(MapUnitIntermediate));
        if (bindings.engage.get_map_protected_int(&source->spawn_quota) >= 1) {
            continue;
        }

        auto* engage_person = bindings.engage.get_map_reference_person(
            &source->reference_person);
        if (!is_engage_hero(engage_person)) {
            continue;
        }
        ++report.configured;

        auto* main_person = bindings.engage.get_map_reference_person(
            reinterpret_cast<RuntimeProtectedPointer24*>(source));
        const auto side = bindings.get_map_side(
            reinterpret_cast<std::byte*>(source) + offset::kSourceSide);
        detail::RuntimeUnitCandidate* match = nullptr;
        for (std::size_t unit_index = 0;
             unit_index < runtime_unit_count;
             ++unit_index) {
            auto& candidate = runtime_units[unit_index];
            if (candidate.used || candidate.side != side) {
                continue;
            }
            auto* unit_bytes = reinterpret_cast<std::byte*>(candidate.unit);
            auto* unit_main_person = static_cast<PersonDefinition*>(
                bindings.engage.get_unit_protected_pointer(
                    unit_bytes + offset::kUnitMainPerson));
            if (unit_main_person == main_person) {
                match = &candidate;
                break;
            }
        }

        if (match == nullptr) {
            ++report.unmatched;
            continue;
        }
        match->used = true;
        if (TryApplyMapEngage(
                source,
                match->unit,
                bindings.engage,
                options) == EnemyEngageApplyResult::applied) {
            ++report.applied;
        }
    }

    if (report.configured == 0) {
        last_completed_signature = signature;
        report.status = LoadedMapEngageStatus::no_configured_units;
    } else if (report.unmatched != 0) {
        report.status = LoadedMapEngageStatus::waiting_for_units;
    } else {
        last_completed_signature = signature;
        report.status = LoadedMapEngageStatus::applied;
    }
    return report;
}

// Minimal state used by a normal function-entry hook. Configure it after the
// hook framework has produced a callable trampoline for sub_159DB68.
inline EnemyEngageBindings g_enemy_engage_bindings{};
inline EnemyEngageOptions g_enemy_engage_options{};

inline void ConfigureEnemyEngageHook(
    EnemyEngageBindings bindings,
    EnemyEngageOptions options = {}) noexcept {
    g_enemy_engage_bindings = bindings;
    g_enemy_engage_options = options;
}

[[nodiscard]] inline std::int32_t HookBuildUnit(
    MapUnitIntermediate* source,
    RuntimeUnit* unit) noexcept {
    const auto original = g_enemy_engage_bindings.original_build_unit;
    if (original == nullptr) {
        return 0;
    }

    const auto built = original(source, unit);
    if (built != 0) {
        (void)TryApplyMapEngage(
            source,
            unit,
            g_enemy_engage_bindings,
            g_enemy_engage_options);
    }
    return built;
}

}  // namespace feh::mod
