#include "feh/skycastle_facility_cfg.hpp"

#include "feh/srpgmap_enemy_engage_cfg.hpp"
#include "feh/srpgmap_enemy_engage.hpp"

#include <array>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace feh::mod {
namespace {

constexpr std::size_t kMaximumFacilities = 16;
constexpr std::size_t kMaximumConfigBytes = 4096;
constexpr std::size_t kMaximumFacilityKeyBytes = 255;
constexpr std::size_t kMaximumFacilityRecords = 8192;
constexpr std::int32_t kNativeFacilityWidth = 6;
constexpr std::int32_t kNativeFacilityHeight = 8;
constexpr std::size_t kSkyCastleMapOffset = 0x210;
// battle::Map + 0x10C (SkyCastleState + 0x31C) is initialized to zero by
// the native SkyCastle map constructor.  In a normal PvE map that value must
// remain negative: zero tells the SkyCastle battle-data code to resolve the
// optional castle context through SkyCastleState + 0x2FC, which is empty in
// PvE and leads to a null dereference when a menu path rebuilds the helper.
constexpr std::size_t kSkyCastleMapContextGateOffset = 0x10C;
constexpr std::size_t kMapModeFacilityFlagOffset = 556;

struct FacilitySelector {
    std::string key;
    std::int32_t category{-1};
    std::int32_t occurrence{};
};

struct FacilityEntry {
    FacilitySelector selector;
    std::int32_t level{-1};
    std::int32_t grid_x{};
    std::int32_t grid_y{};
    bool enabled{true};
    // -1 keeps the FacilityData owner, 0 forces enemy, 1 forces player.
    std::int32_t side{-1};
};

struct FacilityConfig {
    std::array<FacilityEntry, kMaximumFacilities> entries{};
    std::size_t count{};
    std::uint64_t fingerprint{};
};

enum class ConfigSection : std::uint8_t {
    none,
    engage,
    facility,
};

struct FacilityAttachState {
    struct SideOverride {
        std::byte* record{};
        std::uint8_t original{};
    };

    std::byte* field{};
    std::byte* map_object{};
    std::byte* facility_container{};
    std::uintptr_t module_base{};
    std::uint64_t fingerprint{};
    std::size_t attached{};
    bool query_probed{};
    // The bridge may be called before the normal PvE unit rosters are built.
    // Keep the native effect pass retryable until one invocation runs with a
    // live roster, instead of permanently consuming the first early call.
    bool effect_pass_completed{};
    std::uint8_t effect_pass_attempts{};
    bool effect_diagnostics_logged{};
    bool selection_layout_logged{};
    std::byte* input{};
    std::byte* original_input_vtable{};
    std::byte* cloned_input_vtable{};
    std::uintptr_t original_input_touch{};
    bool selection_bridge_installed{};
    bool pending_facility_selection{};
    std::int32_t pending_grid_x{-1};
    std::int32_t pending_grid_y{-1};
    alignas(8) std::array<std::byte, 16> pending_coordinate{};
    alignas(8) std::array<std::byte, 608> selection_owner{};
    std::array<SideOverride, kMaximumFacilities> side_overrides{};
    std::size_t side_override_count{};
};

template <typename Value>
[[nodiscard]] Value read_value(const std::byte* address) noexcept {
    Value value{};
    if (address != nullptr) {
        std::memcpy(&value, address, sizeof(value));
    }
    return value;
}

[[nodiscard]] std::uint64_t hash_bytes(
    std::uint64_t hash,
    const void* data,
    std::size_t size) noexcept {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] std::string trim_copy(std::string_view value) {
    while (!value.empty()
           && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty()
           && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return std::string(value);
}

[[nodiscard]] bool parse_section_header(
    std::string_view line,
    ConfigSection& output) noexcept {
    if (line.size() < 3 || line.front() != '[' || line.back() != ']') {
        return false;
    }
    const auto name = line.substr(1, line.size() - 2);
    if (name == "engage") {
        output = ConfigSection::engage;
        return true;
    }
    if (name == "facility") {
        output = ConfigSection::facility;
        return true;
    }
    return false;
}

[[nodiscard]] bool looks_like_engage_line(std::string_view line) {
    std::array<std::string, 5> fields{};
    std::size_t begin = 0;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        const auto comma = line.find(',', begin);
        if (index + 1 == fields.size()) {
            if (comma != std::string_view::npos) {
                return false;
            }
            fields[index] = trim_copy(line.substr(begin));
            break;
        }
        if (comma == std::string_view::npos) {
            return false;
        }
        fields[index] = trim_copy(line.substr(begin, comma - begin));
        if (fields[index].empty()) {
            return false;
        }
        begin = comma + 1;
    }
    return !fields[2].empty()
        && !fields[3].empty()
        && fields[2].rfind("PID_", 0) == 0
        && fields[3].rfind("PID_", 0) == 0;
}

[[nodiscard]] bool parse_integer(
    std::string_view text,
    std::int32_t& output) noexcept {
    const auto value = trim_copy(text);
    if (value.empty()) {
        return false;
    }
    const auto result = std::from_chars(
        value.data(), value.data() + value.size(), output, 10);
    return result.ec == std::errc{}
           && result.ptr == value.data() + value.size();
}

[[nodiscard]] bool parse_selector(
    const std::string& text,
    FacilitySelector& output) {
    constexpr std::string_view prefix = "type:";
    if (text.rfind(prefix, 0) != 0) {
        output.key = text;
        return !output.key.empty();
    }

    const std::string_view value{text};
    const auto separator = value.find(':', prefix.size());
    if (separator == std::string_view::npos) {
        return false;
    }
    return parse_integer(
               value.substr(prefix.size(), separator - prefix.size()),
               output.category)
           && parse_integer(value.substr(separator + 1), output.occurrence)
           && output.category >= 0
           && output.occurrence >= 0;
}

[[nodiscard]] bool parse_line(
    std::string_view line,
    FacilityEntry& output) {
    std::array<std::string, 6> fields{};
    std::size_t begin = 0;
    std::size_t field_count = 0;
    while (field_count < fields.size()) {
        const auto comma = line.find(',', begin);
        fields[field_count] = trim_copy(
            comma == std::string_view::npos
                ? line.substr(begin)
                : line.substr(begin, comma - begin));
        if (fields[field_count].empty()) {
            return false;
        }
        ++field_count;
        if (comma == std::string_view::npos) {
            break;
        }
        begin = comma + 1;
    }
    if (field_count < 5 || field_count > fields.size()) {
        return false;
    }

    std::int32_t enabled = 0;
    std::int32_t side = -1;
    return parse_selector(fields[0], output.selector)
           && parse_integer(fields[1], output.level)
           && parse_integer(fields[2], output.grid_x)
           && parse_integer(fields[3], output.grid_y)
           && parse_integer(fields[4], enabled)
           && (field_count < 6 || parse_integer(fields[5], side))
           && output.level >= -1
           && output.level <= 16
           && output.grid_x >= 0
           && output.grid_x < kNativeFacilityWidth
           && output.grid_y >= 0
           && output.grid_y < kNativeFacilityHeight
           && (enabled == 0 || enabled == 1)
           && (side >= -1 && side <= 1)
           && (output.enabled = enabled != 0, true)
           && (output.side = side, true);
}

[[nodiscard]] bool read_config(
    const char* path,
    FacilityConfig& output) {
    output = {};
    if (path == nullptr) {
        return false;
    }
    auto* file = std::fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }

    std::array<char, kMaximumConfigBytes + 1> buffer{};
    const auto size = std::fread(buffer.data(), 1, kMaximumConfigBytes, file);
    const bool at_end = std::feof(file) != 0;
    std::fclose(file);
    if (!at_end) {
        return false;
    }

    output.fingerprint = hash_bytes(
        1469598103934665603ULL, buffer.data(), size);
    std::string_view contents{buffer.data(), size};
    ConfigSection section = ConfigSection::none;
    std::size_t line_begin = 0;
    while (line_begin <= contents.size()) {
        const auto line_end = contents.find('\n', line_begin);
        auto line = contents.substr(
            line_begin,
            line_end == std::string_view::npos
                ? contents.size() - line_begin
                : line_end - line_begin);
        const auto comment = line.find('#');
        if (comment != std::string_view::npos) {
            line = line.substr(0, comment);
        }
        const auto trimmed = trim_copy(line);
        if (!trimmed.empty()) {
            if (trimmed.front() == '[' || trimmed.back() == ']') {
                if (!parse_section_header(trimmed, section)) {
                    return false;
                }
            } else if (section == ConfigSection::engage
                       || (section == ConfigSection::none
                           && looks_like_engage_line(trimmed))) {
                // Engage rows belong to the other parser in this merged CFG.
            } else {
                if (section == ConfigSection::none
                    || section == ConfigSection::facility) {
                    if (output.count >= output.entries.size()
                        || !parse_line(
                            trimmed, output.entries[output.count])) {
                        return false;
                    }
                    ++output.count;
                }
            }
        }
        if (line_end == std::string_view::npos) {
            break;
        }
        line_begin = line_end + 1;
    }
    // The native battle::Map stores one FacilityData key per cell. Reject
    // duplicate coordinates instead of silently letting a later CFG row
    // overwrite an earlier facility while the attachment count advances.
    for (std::size_t left = 0; left < output.count; ++left) {
        for (std::size_t right = left + 1; right < output.count; ++right) {
            if (output.entries[left].grid_x == output.entries[right].grid_x
                && output.entries[left].grid_y == output.entries[right].grid_y) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool decode_facility_key(
    std::uintptr_t module_base,
    const std::byte* record,
    std::array<char, kMaximumFacilityKeyBytes + 1>& output) noexcept {
    if (record == nullptr) {
        return false;
    }
    const auto* encoded_key = read_value<const char*>(record);
    if (encoded_key == nullptr) {
        return false;
    }
    const auto transform = resolve_game_function<TransformCkiStringFn>(
        module_base, rva::kTransformCkiString);
    transform(
        output.data(),
        static_cast<std::uint32_t>(output.size()),
        encoded_key);
    return output[0] != '\0';
}

[[nodiscard]] std::byte* find_facility_record(
    std::uintptr_t module_base,
    std::byte* manager,
    const FacilitySelector& selector,
    std::string& resolved_key) noexcept {
    if (manager == nullptr) {
        return nullptr;
    }
    const auto record_count = read_value<std::uint32_t>(manager + 16);
    auto* node = read_value<std::byte*>(manager + 12);
    if (record_count == 0 || record_count > kMaximumFacilityRecords) {
        return nullptr;
    }

    constexpr std::uint32_t kFacilityIdXor = 0x94DD6C4AU;
    std::int32_t matching_occurrence = 0;
    for (std::uint32_t index = 0;
         index < record_count && node != nullptr;
         ++index) {
        auto* record = read_value<std::byte*>(node + 20);
        if (record != nullptr) {
            bool matches = false;
            if (!selector.key.empty()) {
                std::array<char, kMaximumFacilityKeyBytes + 1> decoded{};
                if (decode_facility_key(module_base, record, decoded)
                    && selector.key == decoded.data()) {
                    resolved_key = decoded.data();
                    matches = true;
                }
            } else {
                const auto category = static_cast<std::int32_t>(
                    read_value<std::uint32_t>(record + 0x6C)
                    ^ kFacilityIdXor);
                if (category == selector.category) {
                    if (matching_occurrence == selector.occurrence) {
                        std::array<char, kMaximumFacilityKeyBytes + 1> decoded{};
                        if (decode_facility_key(
                                module_base, record, decoded)) {
                            resolved_key = decoded.data();
                        }
                        matches = true;
                    }
                    ++matching_occurrence;
                }
            }
            if (matches) {
                return record;
            }
        }
        node = read_value<std::byte*>(node);
    }
    return nullptr;
}

void log_facility_manager_candidates(
    std::uintptr_t module_base,
    std::byte* manager) noexcept {
#if defined(__ANDROID__)
    static std::byte* last_manager = nullptr;
    if (manager == nullptr || manager == last_manager) {
        return;
    }

    const auto record_count = read_value<std::uint32_t>(manager + 16);
    auto* node = read_value<std::byte*>(manager + 12);
    if (record_count == 0 || record_count > kMaximumFacilityRecords
        || node == nullptr) {
        return;
    }
    last_manager = manager;

    using FacilityIdFn = std::uint32_t (*)(std::byte*);
    using FacilityTypeFn = std::uint32_t (*)(std::byte*);
    using FacilitySideFn = std::int32_t (*)(std::byte*);
    const auto get_id = resolve_game_function<FacilityIdFn>(
        module_base, rva::kGetFacilityId);
    const auto get_type = resolve_game_function<FacilityTypeFn>(
        module_base, rva::kGetFacilityType);
    const auto get_side = resolve_game_function<FacilitySideFn>(
        module_base, rva::kGetFacilitySide);
    if (get_id == nullptr || get_type == nullptr || get_side == nullptr) {
        return;
    }

    std::uint32_t category_occurrence = 0;
    for (std::uint32_t index = 0;
         index < record_count && node != nullptr;
         ++index) {
        auto* record = read_value<std::byte*>(node + 20);
        if (record != nullptr) {
            const auto category = static_cast<std::uint32_t>(
                read_value<std::uint32_t>(record + 0x6C)
                ^ 0x94DD6C4AU);
            const auto decoded_side = static_cast<unsigned>(
                read_value<std::uint8_t>(record + 0x94) ^ 0x61U);
            const auto decoded_type = static_cast<unsigned>(
                read_value<std::uint8_t>(record + 0x96) ^ 0x8DU);
            if (category == 34U || decoded_type == 8U) {
                std::array<char, kMaximumFacilityKeyBytes + 1> key{};
                (void)decode_facility_key(module_base, record, key);
                __android_log_print(
                    ANDROID_LOG_INFO,
                    "feh-engage",
                    "facility candidate index=%u category=%u occurrence=%u "
                    "key=%s id=%u type=%u get-side=%d decoded-side=%u "
                    "decoded-type=%u record=%p",
                    static_cast<unsigned>(index),
                    static_cast<unsigned>(category),
                    static_cast<unsigned>(category == 34U
                        ? category_occurrence : 0xFFFFFFFFU),
                    key.data(),
                    static_cast<unsigned>(get_id(record)),
                    static_cast<unsigned>(get_type(record)),
                    static_cast<int>(get_side(record)),
                    decoded_side,
                    decoded_type,
                    static_cast<void*>(record));
            }
            if (category == 34U) {
                ++category_occurrence;
            }
        }
        node = read_value<std::byte*>(node);
    }
#else
    (void)module_base;
    (void)manager;
#endif
}

void log_report(const FacilityCfgReport& report) noexcept {
#if defined(__ANDROID__)
    static std::uint32_t last = 0xFFFFFFFFU;
    const auto packed = static_cast<std::uint32_t>(report.status)
        | (static_cast<std::uint32_t>(report.configured) << 8U)
        | (static_cast<std::uint32_t>(report.attached) << 20U);
    if (packed != last) {
        last = packed;
        __android_log_print(
            ANDROID_LOG_INFO,
            "feh-engage",
            "facility cfg status=%u configured=%u attached=%u",
            static_cast<unsigned>(report.status),
            static_cast<unsigned>(report.configured),
            static_cast<unsigned>(report.attached));
    }
#else
    (void)report;
#endif
}

void log_stage(
    const char* stage,
    std::uintptr_t value_a = 0,
    std::uintptr_t value_b = 0) noexcept {
#if defined(__ANDROID__)
    // Facility application runs from the render loop. Keep one diagnostic
    // line per distinct stage/value tuple so steady-state frames do not fill
    // logcat and hide the one-time creation path.
    struct StageLogState {
        const char* stage{};
        std::uintptr_t value_a{};
        std::uintptr_t value_b{};
        bool valid{};
    };
    static StageLogState states[32]{};
    StageLogState* state = nullptr;
    for (auto& candidate : states) {
        if (candidate.valid && std::strcmp(candidate.stage, stage) == 0) {
            state = &candidate;
            break;
        }
        if (!candidate.valid && state == nullptr) {
            state = &candidate;
        }
    }
    if (state != nullptr && state->valid
        && state->value_a == value_a && state->value_b == value_b) {
        return;
    }
    if (state != nullptr) {
        state->stage = stage;
        state->value_a = value_a;
        state->value_b = value_b;
        state->valid = true;
    }
    __android_log_print(
        ANDROID_LOG_INFO,
        "feh-engage",
        "facility stage=%s a=%p b=%p",
        stage,
        reinterpret_cast<void*>(value_a),
        reinterpret_cast<void*>(value_b));
#else
    (void)stage;
    (void)value_a;
    (void)value_b;
#endif
}

[[nodiscard]] FacilityAttachState& attach_state() noexcept {
    static FacilityAttachState state{};
    return state;
}

void flush_pending_facility_selection(
    FacilityAttachState& state) noexcept {
    if (!state.pending_facility_selection
        || state.facility_container == nullptr
        || state.module_base == 0) {
        return;
    }
    const auto grid_x = state.pending_grid_x;
    const auto grid_y = state.pending_grid_y;

    // Do not call sub_16B158C.  IDA shows that its native path reaches
    // FacilityNode::SetState (sub_1A1685E), which first hides entity +632 and
    // then attempts to rebuild a node through sub_1A15644.  The ordinary PvE
    // host does not provide the complete SkyCastle helper context, so that
    // rebuild can return null and leave the facility invisible.
    //
    // Input::UpdateSelection uses a separate, safe path: query FacilityData
    // with sub_16A0494 and feed the result to sub_16B4EEC, which owns the
    // shared top information bar.  The query is guarded by map_mode +556 in
    // the game, so scope that flag to this call and restore it immediately.
    using SetProtectedBoolFn = std::int32_t (*)(void*, bool);
    using FacilityLookupFn = std::byte* (*)(std::int16_t*, std::int32_t);
    using ShowFacilityInfoFn = std::int32_t (*)(
        std::byte*,
        std::byte*,
        const std::byte*,
        std::int32_t);

    auto* map_mode = read_value<std::byte*>(reinterpret_cast<const std::byte*>(
        state.module_base + rva::kMapModeGlobal));
    auto* map_controller = read_value<std::byte*>(
        reinterpret_cast<const std::byte*>(
            state.module_base + rva::kMapInputControllerGlobal));
    auto* info_owner = map_controller == nullptr
        ? nullptr
        : read_value<std::byte*>(map_controller + 644);
    const auto set_bool = resolve_game_function<SetProtectedBoolFn>(
        state.module_base, rva::kSetMapProtectedBool);
    const auto get_bool = resolve_game_function<GetMapProtectedBoolFn>(
        state.module_base, rva::kGetMapProtectedBool);
    const auto query = resolve_game_function<FacilityLookupFn>(
        state.module_base, rva::kSkyCastleFacilityQuery);
    const auto show_info = resolve_game_function<ShowFacilityInfoFn>(
        state.module_base, rva::kShowFacilityInfo);
    if (map_mode == nullptr || set_bool == nullptr || get_bool == nullptr
        || query == nullptr || show_info == nullptr || info_owner == nullptr) {
        state.pending_facility_selection = false;
        log_stage(
            "facility-info-unavailable",
            reinterpret_cast<std::uintptr_t>(info_owner),
            reinterpret_cast<std::uintptr_t>(map_controller));
        return;
    }

    auto* flag = map_mode + kMapModeFacilityFlagOffset;
    const bool previous_mode = get_bool(reinterpret_cast<std::uint8_t*>(flag));
    (void)set_bool(flag, true);
    auto* facility_record = query(
        reinterpret_cast<std::int16_t*>(state.pending_coordinate.data()), 2);
    std::int32_t show_result = 0;
    if (facility_record != nullptr) {
        // sub_16B6F8C is the native facility-selection caller.  It passes the
        // live Input coordinate object at +628 to sub_16B4EEC; preserve that
        // exact argument shape instead of using the unit-selection wrapper.
        log_stage(
            "facility-info-before",
            reinterpret_cast<std::uintptr_t>(info_owner),
            reinterpret_cast<std::uintptr_t>(facility_record));
        show_result = show_info(
            info_owner,
            facility_record,
            state.pending_coordinate.data(),
            1);
        log_stage(
            "facility-info-after",
            static_cast<std::uintptr_t>(static_cast<std::uint32_t>(show_result)),
            reinterpret_cast<std::uintptr_t>(state.pending_coordinate.data()));
    }
    (void)set_bool(flag, previous_mode);

    auto* entity_grid = read_value<std::byte*>(
        state.facility_container + 600);
    using GetFacilityEntityFn = std::byte* (*)(
        std::byte*,
        std::int32_t,
        std::int32_t);
    const auto get_entity = resolve_game_function<GetFacilityEntityFn>(
        state.module_base, rva::kGetFacilityContainerEntity);
    auto* entity_slot = entity_grid == nullptr || get_entity == nullptr
        ? nullptr
        : get_entity(entity_grid, grid_x, grid_y);
    auto* entity = read_value<std::byte*>(entity_slot);
    const auto old_node = read_value<std::uintptr_t>(
        entity == nullptr ? nullptr : entity + 632);
    const auto state_value = read_value<std::int32_t>(
        entity == nullptr ? nullptr : entity + 636);
    const auto state_flag = read_value<std::uint8_t>(
        entity == nullptr ? nullptr : entity + 640);
    state.pending_facility_selection = false;
    log_stage(
        "facility-info",
        reinterpret_cast<std::uintptr_t>(facility_record),
        static_cast<std::uintptr_t>(static_cast<std::uint32_t>(show_result)));
#if defined(__ANDROID__)
    __android_log_print(
        ANDROID_LOG_INFO,
        "feh-engage",
        "facility info grid=%d,%d record=%p result=%d entity=%p node=%p state=%d flag=%u",
        grid_x,
        grid_y,
        static_cast<void*>(facility_record),
        show_result,
        static_cast<void*>(entity),
        reinterpret_cast<void*>(old_node),
        state_value,
        static_cast<unsigned>(state_flag));
#endif
}

using MapInputTouchCallbackFn = std::int32_t (*)(
    std::byte*,
    std::byte*,
    std::int32_t,
    std::int32_t);

std::int32_t facility_map_input_touch_bridge(
    std::byte* input,
    std::byte* event,
    std::int32_t event_arg3,
    std::int32_t event_arg4) noexcept {
    auto& state = attach_state();
    auto original = state.original_input_touch == 0
        ? static_cast<MapInputTouchCallbackFn>(nullptr)
        : reinterpret_cast<MapInputTouchCallbackFn>(
            state.original_input_touch);
    const auto native_result = original == nullptr
        ? 0
        : original(input, event, event_arg3, event_arg4);

    if (native_result != 0
        && state.selection_bridge_installed
        && state.input == input
        && state.facility_container != nullptr
        && state.module_base != 0) {
        using GetCoordinateFn = std::int32_t (*)(const std::byte*);
        using GetFacilityEntityFn = std::byte* (*)(
            std::byte*,
            std::int32_t,
            std::int32_t);
        const auto get_x = resolve_game_function<GetCoordinateFn>(
            state.module_base,
            rva::kGetCoordinateX);
        const auto get_y = resolve_game_function<GetCoordinateFn>(
            state.module_base,
            rva::kGetCoordinateY);
        const auto get_entity = resolve_game_function<GetFacilityEntityFn>(
            state.module_base,
            rva::kGetFacilityContainerEntity);
        if (get_x != nullptr
            && get_y != nullptr
            && get_entity != nullptr) {
            // sub_16B5D18 stores the converted GridCoordinate at Input+628.
            // Passing this game-owned object preserves the exact native
            // coordinate representation expected by sub_16B158C.
            const auto* coordinate = input + 628;
            const auto grid_x = get_x(coordinate);
            const auto grid_y = get_y(coordinate);
            if (grid_x >= 0 && grid_x < kNativeFacilityWidth
                && grid_y >= 0 && grid_y < kNativeFacilityHeight) {
                auto* entity_grid = read_value<std::byte*>(
                    state.facility_container + 600);
                auto* entity_slot = entity_grid == nullptr
                    ? nullptr
                    : get_entity(entity_grid, grid_x, grid_y);
                auto* entity = read_value<std::byte*>(entity_slot);
                if (entity != nullptr) {
                    state.pending_grid_x = grid_x;
                    state.pending_grid_y = grid_y;
                    std::memcpy(
                        state.pending_coordinate.data(),
                        coordinate,
                        state.pending_coordinate.size());
                    state.pending_facility_selection = true;
                    log_stage(
                        "facility-touch",
                        static_cast<std::uintptr_t>(
                            static_cast<std::uint32_t>(
                                (grid_x << 16) ^ (grid_y & 0xFFFF))),
                        reinterpret_cast<std::uintptr_t>(entity));
                }
            }
        }
    }
    return native_result;
}

[[nodiscard]] bool install_selection_bridge(
    std::uintptr_t module_base,
    FacilityAttachState& state) noexcept {
    if (module_base == 0) {
        return false;
    }
    auto* input = read_value<std::byte*>(reinterpret_cast<const std::byte*>(
        module_base + rva::kMapInputGlobal));
    if (input == nullptr) {
        return false;
    }
    if (state.selection_bridge_installed && state.input == input) {
        return true;
    }

    auto* input_vtable = read_value<std::byte*>(input);
    if (input_vtable == nullptr) {
        return false;
    }
    if (reinterpret_cast<std::uintptr_t>(input_vtable)
        != module_base + rva::kMapInputVtable) {
        log_stage(
            "input-vtable-mismatch",
            reinterpret_cast<std::uintptr_t>(input_vtable),
            module_base + rva::kMapInputVtable);
        return false;
    }
    const auto original_touch = read_value<std::uintptr_t>(
        input_vtable + 664);
    if (original_touch == 0
        || original_touch != ((module_base + rva::kMapInputTouchHandler) | 1U)) {
        return false;
    }

    constexpr std::size_t kClonedVtableBytes = 0x1000;
    auto* cloned_vtable = static_cast<std::byte*>(
        std::malloc(kClonedVtableBytes));
    if (cloned_vtable == nullptr) {
        return false;
    }
    std::memcpy(cloned_vtable, input_vtable, kClonedVtableBytes);
    const auto bridge = reinterpret_cast<std::uintptr_t>(
        &facility_map_input_touch_bridge) | 1U;
    std::memcpy(cloned_vtable + 664, &bridge, sizeof(bridge));
    std::memcpy(input, &cloned_vtable, sizeof(cloned_vtable));

    state.module_base = module_base;
    state.input = input;
    state.original_input_vtable = input_vtable;
    state.cloned_input_vtable = cloned_vtable;
    state.original_input_touch = original_touch;
    state.selection_bridge_installed = true;
    log_stage(
        "input-bridge-installed",
        original_touch,
        bridge);
    return true;
}

void uninstall_selection_bridge(FacilityAttachState& state) noexcept {
    if (!state.selection_bridge_installed) {
        return;
    }
    if (state.input != nullptr && state.cloned_input_vtable != nullptr) {
        auto* current_vtable = read_value<std::byte*>(state.input);
        if (current_vtable == state.cloned_input_vtable
            && state.original_input_vtable != nullptr) {
            std::memcpy(
                state.input,
                &state.original_input_vtable,
                sizeof(state.original_input_vtable));
        }
    }
    std::free(state.cloned_input_vtable);
    state.input = nullptr;
    state.original_input_vtable = nullptr;
    state.cloned_input_vtable = nullptr;
    state.original_input_touch = 0;
    state.selection_bridge_installed = false;
}

[[nodiscard]] bool read_facility_mode(
    std::uintptr_t module_base,
    bool& enabled) noexcept {
    auto* map_mode = read_value<std::byte*>(reinterpret_cast<const std::byte*>(
        module_base + rva::kMapModeGlobal));
    if (map_mode == nullptr) {
        return false;
    }
    const auto get_bool = resolve_game_function<GetMapProtectedBoolFn>(
        module_base, rva::kGetMapProtectedBool);
    enabled = get_bool(reinterpret_cast<std::uint8_t*>(
        map_mode + kMapModeFacilityFlagOffset));
    return true;
}

[[nodiscard]] bool set_normal_pve_skycastle_context_gate(
    std::uintptr_t module_base,
    std::byte* map_object) noexcept {
    if (map_object == nullptr) {
        return false;
    }
    using SetProtectedIntFn = std::int32_t (*)(void*, std::int32_t);
    using GetProtectedIntFn = std::int32_t (*)(void*);
    const auto set_value = resolve_game_function<SetProtectedIntFn>(
        module_base, rva::kSetMapProtectedInt);
    const auto get_value = resolve_game_function<GetProtectedIntFn>(
        module_base, rva::kGetMapProtectedInt);
    auto* gate = map_object + kSkyCastleMapContextGateOffset;
    (void)set_value(gate, -1);
    return get_value(gate) < 0;
}

void probe_native_facility_queries(
    std::uintptr_t module_base,
    std::byte* map_object,
    const FacilityConfig& config,
    FacilityAttachState& state) noexcept {
#if defined(__ANDROID__)
    if (map_object == nullptr || state.query_probed) {
        return;
    }

    using SetProtectedBoolFn = std::int32_t (*)(void*, bool);
    using ConstructGridCoordinateFn = void* (*)(
        void*, std::int32_t, std::int32_t);
    using FacilityLookupFn = std::byte* (*)(
        std::int16_t*, std::int32_t);
    using FacilityIdFn = std::uint32_t (*)(std::byte*);
    using FacilityTypeFn = std::uint32_t (*)(std::byte*);
    using FacilitySideFn = std::int32_t (*)(std::byte*);

    const auto map_mode = read_value<std::byte*>(reinterpret_cast<const std::byte*>(
        module_base + rva::kMapModeGlobal));
    const auto set_bool = resolve_game_function<SetProtectedBoolFn>(
        module_base, rva::kSetMapProtectedBool);
    const auto construct_coordinate =
        resolve_game_function<ConstructGridCoordinateFn>(
            module_base, rva::kConstructGridCoordinate);
    const auto query = resolve_game_function<FacilityLookupFn>(
        module_base, rva::kSkyCastleFacilityQuery);
    const auto get_id = resolve_game_function<FacilityIdFn>(
        module_base, rva::kGetFacilityId);
    const auto get_type = resolve_game_function<FacilityTypeFn>(
        module_base, rva::kGetFacilityType);
    const auto get_side = resolve_game_function<FacilitySideFn>(
        module_base, rva::kGetFacilitySide);
    if (map_mode == nullptr || set_bool == nullptr
        || construct_coordinate == nullptr || query == nullptr
        || get_id == nullptr || get_type == nullptr || get_side == nullptr) {
        log_stage("query-probe-unavailable");
        state.query_probed = true;
        return;
    }

    auto* flag = map_mode + kMapModeFacilityFlagOffset;
    bool previous = false;
    const auto get_bool = resolve_game_function<GetMapProtectedBoolFn>(
        module_base, rva::kGetMapProtectedBool);
    if (get_bool != nullptr) {
        previous = get_bool(reinterpret_cast<std::uint8_t*>(flag));
    }
    (void)set_bool(flag, true);
    log_stage("query-probe-on", previous ? 1U : 0U, config.count);
    for (std::size_t index = 0; index < config.count; ++index) {
        const auto& entry = config.entries[index];
        alignas(8) std::array<std::byte, 16> coordinate{};
        construct_coordinate(
            coordinate.data(), entry.grid_x, entry.grid_y);
        for (std::int32_t side = 0; side <= 2; ++side) {
            auto* record = query(
                reinterpret_cast<std::int16_t*>(coordinate.data()), side);
            if (record == nullptr) {
                __android_log_print(
                    ANDROID_LOG_INFO,
                    "feh-engage",
                    "facility query-probe xy=%d,%d side=%d record=null",
                    entry.grid_x,
                    entry.grid_y,
                    side);
                continue;
            }
            __android_log_print(
                ANDROID_LOG_INFO,
                "feh-engage",
                "facility query-probe xy=%d,%d side=%d record=%p id=%u type=%u record-side=%d",
                entry.grid_x,
                entry.grid_y,
                side,
                static_cast<void*>(record),
                static_cast<unsigned>(get_id(record)),
                static_cast<unsigned>(get_type(record)),
                static_cast<int>(get_side(record)));
        }
    }
    (void)set_bool(flag, previous);
    log_stage("query-probe-off", previous ? 1U : 0U);
    state.query_probed = true;
#else
    (void)module_base;
    (void)map_object;
    (void)config;
    (void)state;
#endif
}

void run_native_facility_effect_pass(
    std::uintptr_t module_base,
    FacilityAttachState& state) noexcept {
#if defined(__ANDROID__)
    if (state.effect_pass_completed) {
        return;
    }
    if (state.effect_pass_attempts >= 8) {
        return;
    }

    auto* map_mode = read_value<std::byte*>(reinterpret_cast<const std::byte*>(
        module_base + rva::kMapModeGlobal));
    if (map_mode == nullptr) {
        log_stage("effect-pass-unavailable");
        return;
    }

    using SetProtectedBoolFn = std::int32_t (*)(void*, bool);
    using CreateProcTurnFn = std::int32_t (*)(void*);
    using FacilityEffectStageFn = bool (*)(void*);
    using FacilityGenericStageFn = bool (*)(void*);
    using ApplyUnitEffectsStageFn = std::int32_t (*)(void*);
    using MultiSideModeFn = bool (*)();
    using SetMapSideFn = std::int32_t (*)(void*, std::int32_t);
    using FacilityPassFn = std::int32_t (*)();
    using ProcControllerLifecycleFn = std::int32_t (*)(void*);
    using InvokeProcControllerCallbackFn = std::int32_t (*)(void*);
    using DestroyProcTurnFn = void (*)(void*);
    const auto set_bool = resolve_game_function<SetProtectedBoolFn>(
        module_base, rva::kSetMapProtectedBool);
    const auto get_bool = resolve_game_function<GetMapProtectedBoolFn>(
        module_base, rva::kGetMapProtectedBool);
    const auto create_proc = resolve_game_function<CreateProcTurnFn>(
        module_base, rva::kCreateMapProcessSkillTurn);
    const auto facility_effect_stage =
        resolve_game_function<FacilityEffectStageFn>(
            module_base, rva::kMapProcessSkillTurnFacilityStage);
    const auto facility_generic_stage =
        resolve_game_function<FacilityGenericStageFn>(
            module_base, rva::kMapProcessSkillTurnGenericStage);
    const auto apply_unit_effects_stage =
        resolve_game_function<ApplyUnitEffectsStageFn>(
            module_base, rva::kMapProcessSkillTurnApplyUnitEffects);
    const auto multi_side_mode = resolve_game_function<MultiSideModeFn>(
        module_base, rva::kMapProcessSkillTurnMultiSide);
    const auto set_map_side = resolve_game_function<SetMapSideFn>(
        module_base, rva::kSetMapSide);
    const auto facility_state_pass = resolve_game_function<FacilityPassFn>(
        module_base, rva::kSkyCastleFacilityStatePass);
    const auto facility_activate_pass = resolve_game_function<FacilityPassFn>(
        module_base, rva::kSkyCastleFacilityActivatePass);
    const auto proc_controller_lifecycle =
        resolve_game_function<ProcControllerLifecycleFn>(
            module_base, rva::kProcControllerLifecycle);
    const auto invoke_proc_callback =
        resolve_game_function<InvokeProcControllerCallbackFn>(
            module_base, rva::kInvokeProcControllerCallback);
    const auto destroy_proc = resolve_game_function<DestroyProcTurnFn>(
        module_base, rva::kDestroyMapProcessSkillTurn);
    if (set_bool == nullptr || get_bool == nullptr || create_proc == nullptr
        || facility_effect_stage == nullptr
        || facility_generic_stage == nullptr || multi_side_mode == nullptr
        || apply_unit_effects_stage == nullptr
        || set_map_side == nullptr || facility_state_pass == nullptr
        || facility_activate_pass == nullptr
        || proc_controller_lifecycle == nullptr
        || invoke_proc_callback == nullptr
        || destroy_proc == nullptr) {
        log_stage("effect-pass-unavailable");
        return;
    }

    // Capture the exact native inputs once.  The facility effect walker is
    // heavily gated by unit state/skill predicates, so a zero return alone
    // cannot distinguish "no matching unit" from "wrong facility record".
    if (!state.effect_diagnostics_logged) {
        using ConstructGridCoordinateFn = void* (*)(
            void*, std::int32_t, std::int32_t);
        using FacilityLookupFn = std::byte* (*)(std::int16_t*, std::int32_t);
        using FacilityUnitEligibleFn = bool (*)(std::byte*, std::byte*);
        using FacilityMatcherCreateFn = std::byte* (*)(std::byte*, std::byte*);
        using FacilityMatcherCheckFn = bool (*)(std::byte*, std::byte*, std::byte*, std::int32_t);
        using GetUnitStateFn = std::byte* (*)(std::byte*);
        using GetSkillCountFn = std::int32_t (*)(std::byte*);
        using GetCoordinateComponentFn = std::int32_t (*)(const std::byte*);
        const auto construct_coordinate =
            resolve_game_function<ConstructGridCoordinateFn>(
                module_base, rva::kConstructGridCoordinate);
        const auto query = resolve_game_function<FacilityLookupFn>(
            module_base, rva::kSkyCastleFacilityQuery);
        const auto set_diagnostic_bool = resolve_game_function<SetProtectedBoolFn>(
            module_base, rva::kSetMapProtectedBool);
        const auto get_diagnostic_bool = resolve_game_function<GetMapProtectedBoolFn>(
            module_base, rva::kGetMapProtectedBool);
        const auto get_unit_state = resolve_game_function<GetUnitStateFn>(
            module_base, 0x1325F14);
        const auto get_skill_count = resolve_game_function<GetSkillCountFn>(
            module_base, 0x17E7F10);
        const auto facility_unit_eligible =
            resolve_game_function<FacilityUnitEligibleFn>(
                module_base, 0x16A0748);
        const auto create_facility_matcher =
            resolve_game_function<FacilityMatcherCreateFn>(
                module_base, 0x16A077C);
        const auto check_facility_matcher =
            resolve_game_function<FacilityMatcherCheckFn>(
                module_base, 0x161F3C4);
        const auto get_coordinate_x =
            resolve_game_function<GetCoordinateComponentFn>(
                module_base, rva::kGetCoordinateX);
        const auto get_coordinate_y =
            resolve_game_function<GetCoordinateComponentFn>(
                module_base, rva::kGetCoordinateY);
        bool diagnostic_record_found = false;
        std::byte* diagnostic_record = nullptr;
        std::int32_t diagnostic_x = -1;
        std::int32_t diagnostic_y = -1;
        bool diagnostic_previous_mode = false;
        bool diagnostic_mode_scoped = false;
        auto* diagnostic_map_mode = read_value<std::byte*>(
            reinterpret_cast<const std::byte*>(module_base + rva::kMapModeGlobal));
        if (diagnostic_map_mode != nullptr
            && set_diagnostic_bool != nullptr
            && query != nullptr) {
            auto* diagnostic_flag = diagnostic_map_mode
                + kMapModeFacilityFlagOffset;
            if (get_diagnostic_bool != nullptr) {
                diagnostic_previous_mode = get_diagnostic_bool(
                    reinterpret_cast<std::uint8_t*>(diagnostic_flag));
            }
            (void)set_diagnostic_bool(
                diagnostic_flag, true);
            diagnostic_mode_scoped = true;
        }
        if (construct_coordinate != nullptr && query != nullptr
            && diagnostic_mode_scoped) {
            for (std::int32_t y = 0; y < kNativeFacilityHeight; ++y) {
                for (std::int32_t x = 0; x < kNativeFacilityWidth; ++x) {
                    alignas(8) std::array<std::byte, 16> coordinate{};
                    construct_coordinate(coordinate.data(), x, y);
                    for (std::int32_t side = 0; side <= 2; ++side) {
                        auto* record = query(
                            reinterpret_cast<std::int16_t*>(coordinate.data()),
                            side);
                        if (record == nullptr) {
                            continue;
                        }
                        diagnostic_record_found = true;
                        if (diagnostic_record == nullptr) {
                            diagnostic_record = record;
                            diagnostic_x = x;
                            diagnostic_y = y;
                        }
                        __android_log_print(
                            ANDROID_LOG_INFO,
                            "feh-engage",
                            "effect-facility-input xy=%d,%d query-side=%d "
                            "record=%p raw8c=%08x raw90=%08x raw94=%02x "
                            "raw95=%02x raw96=%02x raw97=%02x decoded-side=%d "
                            "decoded-category=%d decoded-type=%d "
                            "decoded-subtype=%d subtype-supported=%d",
                            x,
                            y,
                            side,
                            static_cast<void*>(record),
                            read_value<std::uint32_t>(record + 0x8C),
                            read_value<std::uint32_t>(record + 0x90),
                            static_cast<unsigned>(read_value<std::uint8_t>(
                                record + 0x94)),
                            static_cast<unsigned>(read_value<std::uint8_t>(
                                record + 0x95)),
                            static_cast<unsigned>(read_value<std::uint8_t>(
                                record + 0x96)),
                            static_cast<unsigned>(read_value<std::uint8_t>(
                                record + 0x97)),
                            static_cast<int>(read_value<std::int8_t>(
                                record + 0x94) ^ 0x61),
                            static_cast<int>(read_value<std::int8_t>(
                                record + 0x95) ^ 0x5D),
                            static_cast<int>(read_value<std::int8_t>(
                                record + 0x96) ^ static_cast<std::int8_t>(
                                    0x8D)),
                            static_cast<int>(read_value<std::int8_t>(
                                record + 0x97) ^ static_cast<std::int8_t>(
                                    0xB2)),
                            ((read_value<std::int8_t>(record + 0x97)
                                ^ static_cast<std::int8_t>(0xB2)) <= 6)
                                ? 1 : 0);
                    }
                }
            }
        }
        if (diagnostic_mode_scoped) {
            auto* diagnostic_flag = diagnostic_map_mode
                + kMapModeFacilityFlagOffset;
            (void)set_diagnostic_bool(
                diagnostic_flag, diagnostic_previous_mode);
        }
        auto* diagnostic_manager = read_value<std::byte*>(
            reinterpret_cast<const std::byte*>(
                module_base + rva::kRuntimeUnitManagerGlobal));
        if (diagnostic_manager != nullptr && get_unit_state != nullptr) {
            for (std::int32_t side = 0; side < 2; ++side) {
                auto* roster = diagnostic_manager + 12 * side + 1028;
                auto* begin = read_value<std::byte*>(roster);
                auto* end = read_value<std::byte*>(roster + 4);
                if (begin == nullptr || end < begin) {
                    continue;
                }
                const auto count = static_cast<std::size_t>(
                    (end - begin) / sizeof(std::byte*));
                for (std::size_t index = 0; index < count; ++index) {
                    auto* unit = read_value<std::byte*>(
                        begin + index * sizeof(std::byte*));
                    auto* unit_state = unit == nullptr
                        ? nullptr : get_unit_state(unit + 1004);
                    const auto state_word = unit_state == nullptr
                        ? 0U : static_cast<unsigned>(read_value<std::uint8_t>(
                            unit_state + 7));
                    const auto skill_count = get_skill_count == nullptr
                        || unit == nullptr ? -1 : get_skill_count(unit);
                    __android_log_print(
                        ANDROID_LOG_INFO,
                        "feh-engage",
                        "effect-unit-input side=%d index=%u unit=%p "
                        "state=%p state-byte7=%02x has20=%d skill-count=%d",
                        side,
                        static_cast<unsigned>(index),
                        static_cast<void*>(unit),
                        static_cast<void*>(unit_state),
                        state_word,
                        (state_word & 0x20U) != 0 ? 1 : 0,
                        skill_count);
                    if (diagnostic_record != nullptr
                        && facility_unit_eligible != nullptr
                        && unit != nullptr) {
                        const auto* position = unit + 0x168;
                        const auto position_x = get_coordinate_x == nullptr
                            ? -1 : get_coordinate_x(position);
                        const auto position_y = get_coordinate_y == nullptr
                            ? -1 : get_coordinate_y(position);
                        const bool eligible = facility_unit_eligible(
                            diagnostic_record, unit);
                        bool matcher_result = false;
                        if (construct_coordinate != nullptr
                            && create_facility_matcher != nullptr
                            && check_facility_matcher != nullptr) {
                            alignas(8) std::array<std::byte, 20> matcher{};
                            alignas(8) std::array<std::byte, 16> facility_coordinate{};
                            construct_coordinate(
                                facility_coordinate.data(),
                                diagnostic_x,
                                diagnostic_y);
                            auto* matcher_result_object =
                                create_facility_matcher(
                                    matcher.data(), diagnostic_record);
                            if (matcher_result_object != nullptr) {
                                matcher_result = check_facility_matcher(
                                    matcher.data(),
                                    facility_coordinate.data(),
                                    unit + 0x168,
                                    static_cast<std::int32_t>(
                                        read_value<std::uint32_t>(
                                            diagnostic_record + 0x90)
                                        ^ 0xEBBB944CU));
                            }
                        }
                        __android_log_print(
                            ANDROID_LOG_INFO,
                            "feh-engage",
                            "effect-type8-unit side=%d index=%u pos=%d,%d "
                            "eligible=%d matcher=%d raw98=%02x decoded-range=%d "
                            "decoded-value=%d",
                            side,
                            static_cast<unsigned>(index),
                            position_x,
                            position_y,
                            eligible ? 1 : 0,
                            matcher_result ? 1 : 0,
                            static_cast<unsigned>(read_value<std::uint8_t>(
                                diagnostic_record + 0x98)),
                            static_cast<int>(read_value<std::uint32_t>(
                                diagnostic_record + 0x90)
                                ^ 0xEBBB944CU),
                            static_cast<int>(read_value<std::uint32_t>(
                                diagnostic_record + 0x8C)
                                ^ 0x470C8300U));
                    }
                }
            }
        }
        // Do not consume the one-shot diagnostic before FacilityData has been
        // attached.  During scene construction the first effect pass can run
        // with an empty native facility table; retry on the next pass until a
        // real record is observed.
        if (diagnostic_record_found) {
            state.effect_diagnostics_logged = true;
        }
    }

    // MapProcessSkillTurn normally runs after both side rosters exist.  The
    // external bridge can be called earlier during scene construction, so do
    // not consume that attempt while there are no live runtime units yet.
    auto* unit_manager = read_value<std::byte*>(reinterpret_cast<const std::byte*>(
        module_base + rva::kRuntimeUnitManagerGlobal));
    std::size_t roster_count[2]{};
    if (unit_manager != nullptr) {
        for (std::size_t side = 0; side < 2; ++side) {
            auto* roster = unit_manager + 12 * side + 1028;
            auto* begin = read_value<std::byte*>(roster);
            auto* end = read_value<std::byte*>(roster + 4);
            if (begin != nullptr && end >= begin) {
                roster_count[side] = static_cast<std::size_t>(
                    (end - begin) / sizeof(std::byte*));
            }
        }
    }
    log_stage(
        "effect-unit-count",
        static_cast<std::uintptr_t>(roster_count[0]),
        static_cast<std::uintptr_t>(roster_count[1]));
    if (roster_count[0] == 0 && roster_count[1] == 0) {
        log_stage("effect-wait-roster");
        return;
    }

    using GetProtectedStateFn = std::int32_t (*)(void*);
    const auto get_process_state = resolve_game_function<GetProtectedStateFn>(
        module_base, rva::kGetMapProcessState);
    const auto get_map_side = resolve_game_function<GetMapSideFn>(
        module_base, rva::kGetMapSide);
    auto* map_state = read_value<std::byte*>(reinterpret_cast<const std::byte*>(
        module_base + rva::kCurrentMapStateGlobal));
    auto* native_side_state = map_state == nullptr
        ? nullptr : map_state + 28;
    std::int32_t process_state = -1;
    std::int32_t current_side = -1;
    if (map_mode != nullptr && get_process_state != nullptr) {
        process_state = get_process_state(map_mode + 4);
    }
    if (native_side_state != nullptr && get_map_side != nullptr) {
        current_side = get_map_side(native_side_state);
    }
    log_stage(
        "effect-map-state",
        static_cast<std::uintptr_t>(static_cast<std::uint32_t>(process_state)),
        static_cast<std::uintptr_t>(static_cast<std::uint32_t>(current_side)));
    log_stage(
        "effect-side-owner",
        reinterpret_cast<std::uintptr_t>(map_state),
        reinterpret_cast<std::uintptr_t>(native_side_state));
    const bool native_multi_side = multi_side_mode();
    log_stage("effect-multi-side", native_multi_side ? 1U : 0U);

    // ProcTurn's factory links the new process into parent + 0x1C.  A small
    // private parent lets us reuse the complete native constructor without
    // inserting a process into FEH's live scheduler.  The process is executed
    // synchronously below and destroyed before this function returns.
    alignas(8) static std::array<std::byte, 96> proc_parent{};
    std::memset(proc_parent.data(), 0, proc_parent.size());
    (void)create_proc(proc_parent.data());
    auto* proc = read_value<std::byte*>(proc_parent.data() + 28);
    if (proc == nullptr) {
        log_stage("effect-proc-create-null");
        return;
    }
    auto* flag = map_mode + kMapModeFacilityFlagOffset;
    const bool previous = get_bool(reinterpret_cast<std::uint8_t*>(flag));
    (void)set_bool(flag, true);
    log_stage(
        "effect-proc-on",
        reinterpret_cast<std::uintptr_t>(proc),
        reinterpret_cast<std::uintptr_t>(facility_effect_stage));

    const auto log_proc_queues = [&](const char* phase) {
        __android_log_print(
            ANDROID_LOG_INFO,
            "feh-engage",
            "effect-proc-queues phase=%s "
            "q60=%p/%p q76=%p/%p q92=%p/%p q120=%p/%p q160=%p/%p "
            "controller=%p state=%u executed=%u",
            phase,
            static_cast<void*>(read_value<std::byte*>(proc + 60)),
            static_cast<void*>(read_value<std::byte*>(proc + 64)),
            static_cast<void*>(read_value<std::byte*>(proc + 76)),
            static_cast<void*>(read_value<std::byte*>(proc + 80)),
            static_cast<void*>(read_value<std::byte*>(proc + 92)),
            static_cast<void*>(read_value<std::byte*>(proc + 96)),
            static_cast<void*>(read_value<std::byte*>(proc + 120)),
            static_cast<void*>(read_value<std::byte*>(proc + 124)),
            static_cast<void*>(read_value<std::byte*>(proc + 160)),
            static_cast<void*>(read_value<std::byte*>(proc + 164)),
            static_cast<void*>(read_value<std::byte*>(proc + 56)),
            static_cast<unsigned>(read_value<std::uint8_t>(
                read_value<std::byte*>(proc + 56) == nullptr
                    ? nullptr : read_value<std::byte*>(proc + 56) + 60)),
            static_cast<unsigned>(read_value<std::uint8_t>(
                read_value<std::byte*>(proc + 56) == nullptr
                    ? nullptr : read_value<std::byte*>(proc + 56) + 61)));
    };
    log_proc_queues("before-passes");

    // Reuse the exact native passes used by FieldSkyCastle before running the
    // turn-effect stage. They reconcile FacilityData state and field layers;
    // the temporary flag scope keeps ordinary PvE mode unchanged afterwards.
    const auto state_result = facility_state_pass();
    const auto activate_result = facility_activate_pass();
    log_stage(
        "effect-facility-passes",
        static_cast<std::uintptr_t>(static_cast<std::uint32_t>(state_result)),
        static_cast<std::uintptr_t>(static_cast<std::uint32_t>(activate_result)));
    log_proc_queues("after-passes");

    // The generic native stage owns the FacilityData type dispatch. In
    // particular, sub_16FAD2C (called by sub_16F82D4) handles facility type 8
    // at 0x16FF01A. The existing facility stage below is a separate skill/
    // animation pass and does not reach that branch.
    bool any_effect = false;
    const auto run_generic_for_side = [&](std::int32_t side) {
        if (native_side_state != nullptr) {
            (void)set_map_side(native_side_state, side);
        }
        // sub_16F82D4 starts by clearing the ProcTurn queues.  In ordinary
        // PvE we invoke it once per side, so submit the queue immediately
        // after each invocation instead of allowing the next side to clear
        // the previous side's records.
        const bool dispatch_result = facility_generic_stage(proc);
        const auto apply_result = apply_unit_effects_stage(proc);
        any_effect = any_effect || dispatch_result || apply_result != 0;
        log_stage(
            "effect-generic-side-result",
            static_cast<std::uintptr_t>(static_cast<std::uint32_t>(side)),
            dispatch_result ? 1U : 0U);
        log_stage(
            "effect-generic-side-apply",
            static_cast<std::uintptr_t>(static_cast<std::uint32_t>(side)),
            static_cast<std::uintptr_t>(
                static_cast<std::uint32_t>(apply_result)));
        log_proc_queues("after-generic-side");
    };
    if (native_multi_side || current_side < 0) {
        const bool dispatch_result = facility_generic_stage(proc);
        const auto apply_result = apply_unit_effects_stage(proc);
        any_effect = any_effect || dispatch_result || apply_result != 0;
        log_stage(
            "effect-generic-side-result",
            0xFFFFFFFFU,
            dispatch_result ? 1U : 0U);
        log_stage(
            "effect-generic-side-apply",
            0xFFFFFFFFU,
            static_cast<std::uintptr_t>(
                static_cast<std::uint32_t>(apply_result)));
        log_proc_queues("after-generic-side");
    } else {
        run_generic_for_side(0);
        run_generic_for_side(1);
        if (native_side_state != nullptr) {
            (void)set_map_side(native_side_state, current_side);
        }
    }
    log_proc_queues("after-generic");

    // In ordinary PvE the native multi-side predicate is false. Run the
    // existing native stage once for each side and restore the original side.
    const auto run_for_side = [&](std::int32_t side) {
        if (native_side_state != nullptr) {
            (void)set_map_side(native_side_state, side);
        }
        const bool result = facility_effect_stage(proc);
        any_effect = any_effect || result;
        log_stage(
            "effect-side-result",
            static_cast<std::uintptr_t>(static_cast<std::uint32_t>(side)),
            result ? 1U : 0U);
        log_stage(
            "effect-side-counters",
            read_value<std::uint32_t>(proc + 188),
            read_value<std::uint32_t>(proc + 192));
    };
    if (native_multi_side || current_side < 0) {
        const bool result = facility_effect_stage(proc);
        any_effect = result;
        log_stage("effect-side-result", 0xFFFFFFFFU, result ? 1U : 0U);
        log_stage(
            "effect-side-counters",
            read_value<std::uint32_t>(proc + 188),
            read_value<std::uint32_t>(proc + 192));
    } else {
        run_for_side(0);
        run_for_side(1);
        if (native_side_state != nullptr) {
            (void)set_map_side(native_side_state, current_side);
        }
    }
    log_proc_queues("after-effect-stage");

    // The native stages enqueue effect events on ProcTurn. In the normal
    // scheduler, ProcController's lifecycle entry validates state, invokes
    // the bound ProcTurn callback, and marks the controller executed. The
    // private process is not inserted into that scheduler, so reproduce this
    // small state transition before falling back to a direct callback call.
    auto* controller = read_value<std::byte*>(proc + 56);
    if (controller != nullptr) {
        log_proc_queues("before-controller");
        const auto controller_state = read_value<std::uint8_t>(controller + 60);
        const auto controller_executed =
            read_value<std::uint8_t>(controller + 61);
        log_stage(
            "effect-controller-before",
            static_cast<std::uintptr_t>(controller_state),
            static_cast<std::uintptr_t>(controller_executed));

        const std::uint8_t active_state = 1;
        const std::uint8_t not_executed = 0;
        std::memcpy(controller + 60, &active_state, sizeof(active_state));
        std::memcpy(controller + 61, &not_executed, sizeof(not_executed));
        const auto lifecycle_result = proc_controller_lifecycle(controller);
        log_stage(
            "effect-proc-lifecycle",
            reinterpret_cast<std::uintptr_t>(controller),
            static_cast<std::uintptr_t>(
                static_cast<std::uint32_t>(lifecycle_result)));
        log_proc_queues("after-controller-lifecycle");

        // A private parent may not satisfy the controller's scheduler-owner
        // check. If the lifecycle did not mark the callback executed, invoke
        // the already-bound function object directly as a compatibility
        // fallback, preserving the previous behavior.
        if (read_value<std::uint8_t>(controller + 61) == 0) {
            const auto callback_result = invoke_proc_callback(controller + 64);
            log_stage(
                "effect-proc-callback-fallback",
                reinterpret_cast<std::uintptr_t>(controller),
                static_cast<std::uintptr_t>(
                    static_cast<std::uint32_t>(callback_result)));
        }
        log_proc_queues("after-controller");
    } else {
        log_stage("effect-proc-controller-null");
    }
    (void)set_bool(flag, previous);
    log_stage("effect-proc-off", previous ? 1U : 0U);

    // The destructor unlinks the process from the private parent and releases
    // its native controller.  Clear the link as well so a later scene cannot
    // mistake the freed pointer for a live process.
    destroy_proc(proc);
    std::memset(proc_parent.data(), 0, proc_parent.size());
    log_stage("effect-proc-destroyed");
    ++state.effect_pass_attempts;
    state.effect_pass_completed = any_effect;
    log_stage(
        any_effect ? "effect-pass-complete" : "effect-pass-retry",
        state.effect_pass_attempts);
#else
    (void)module_base;
    (void)state;
#endif
}

[[nodiscard]] bool is_native_skycastle_field(
    std::uintptr_t module_base,
    const FacilityAttachState& state,
    const std::byte* field) noexcept {
    if (field == nullptr || field == state.facility_container) {
        return false;
    }
    const auto vtable = read_value<std::uintptr_t>(field);
    return vtable == module_base + rva::kFieldSkyCastleVtable;
}

[[nodiscard]] bool current_battle_is_six_by_eight(
    std::uintptr_t module_base,
    std::int32_t& width,
    std::int32_t& height) noexcept {
    alignas(8) std::array<std::byte, 16> grid{};
    using GetCurrentMapGridSizeFn = void (*)(void*);
    using GetGridDimensionFn = std::int32_t (*)(void*);
    const auto get_grid_size = resolve_game_function<GetCurrentMapGridSizeFn>(
        module_base, rva::kGetCurrentMapGridSize);
    const auto get_width = resolve_game_function<GetGridDimensionFn>(
        module_base, rva::kGetGridWidth);
    const auto get_height = resolve_game_function<GetGridDimensionFn>(
        module_base, rva::kGetGridHeight);
    get_grid_size(grid.data());
    width = get_width(grid.data());
    height = get_height(grid.data());
    return width == kNativeFacilityWidth && height == kNativeFacilityHeight;
}

[[nodiscard]] bool set_map_cell_key(
    std::uintptr_t module_base,
    std::byte* map_object,
    const FacilityEntry& entry,
    std::string_view key,
    bool prefer_native_cki) noexcept {
    using PackLookupStringFn = std::int32_t (*)(void*, const void*, std::uint32_t);
    using TransformCkiStringFn = std::int32_t (*)(
        void*, std::uint32_t, const char*);
    using DestroyLookupStringFn = void (*)(void*);
    using SetMapCellKeyFn = std::int32_t (*)(
        std::byte*, std::int32_t, std::int32_t, void*);
    const auto pack = resolve_game_function<PackLookupStringFn>(
        module_base, rva::kPackLookupString);
    const auto transform = resolve_game_function<TransformCkiStringFn>(
        module_base, rva::kTransformCkiString);
    const auto destroy = resolve_game_function<DestroyLookupStringFn>(
        module_base, rva::kDestroyLookupString);
    const auto set_key = resolve_game_function<SetMapCellKeyFn>(
        module_base, rva::kSetSkyCastleMapCellKey);

    alignas(8) std::array<std::byte, 12> packed{};
    if (prefer_native_cki && transform != nullptr) {
        // Cki's transform is symmetric.  FacilityManager keys are stored as
        // transformed bytes, so encode the readable selector before packing
        // the libc++ lookup string (the same path used for person PID CFG).
        std::array<char, kMaximumFacilityKeyBytes + 1> encoded{};
        transform(
            encoded.data(),
            static_cast<std::uint32_t>(encoded.size()),
            key.data());
        pack(
            packed.data(),
            encoded.data(),
            static_cast<std::uint32_t>(key.size()));
    } else {
        pack(
            packed.data(),
            key.data(),
            static_cast<std::uint32_t>(key.size()));
    }
    // 0x1A13184 operates on the embedded battle::Map string-grid object.
    // The enclosing battle::Map stores that grid at +4; passing the outer
    // object makes the function read dimensions/vtable fields as a grid and
    // can corrupt unrelated battle-state memory.
    const auto result = set_key(
        map_object + 4,
        entry.grid_x,
        entry.grid_y,
        packed.data());
    destroy(packed.data());
    return result != 0;
}

[[nodiscard]] const std::byte* get_map_cell_key(
    std::uintptr_t module_base,
    std::byte* map_object,
    const FacilityEntry& entry) noexcept {
    using GetMapCellKeyFn = const std::byte* (*)(
        std::byte*, std::int32_t, std::int32_t);
    const auto get_key = resolve_game_function<GetMapCellKeyFn>(
        module_base, rva::kGetSkyCastleMapCellKey);
    return get_key(map_object + 4, entry.grid_x, entry.grid_y);
}

[[nodiscard]] bool set_map_cell_state(
    std::uintptr_t module_base,
    std::byte* map_object,
    const FacilityEntry& entry) noexcept {
    using ConstructGridCoordinateFn = void* (*)(void*, std::int32_t, std::int32_t);
    using FacilityStateFn = std::int32_t (*)(std::byte*, std::byte*);
    const auto construct_coordinate =
        resolve_game_function<ConstructGridCoordinateFn>(
            module_base, rva::kConstructGridCoordinate);
    const auto set_active = resolve_game_function<FacilityStateFn>(
        module_base,
        entry.enabled
            ? rva::kActivateSkyCastleFacility
            : rva::kDeactivateSkyCastleFacility);
    alignas(8) std::array<std::byte, 16> coordinate{};
    construct_coordinate(
        coordinate.data(), entry.grid_x, entry.grid_y);
    // The native setter returns an internal protected-string scratch value,
    // not a success boolean. Bounds were validated before this call, so the
    // call itself is the success condition.
    (void)set_active(map_object, coordinate.data());
    return true;
}

[[nodiscard]] bool apply_facility_side_override(
    const FacilityEntry& entry,
    std::byte* record,
    FacilityAttachState& state) noexcept {
    if (record == nullptr || entry.side < -1 || entry.side > 1) {
        return false;
    }
    if (entry.side < 0) {
        return true;
    }

    auto* encoded_side = record + 0x94;
    const auto requested = static_cast<std::uint8_t>(
        0x61U ^ static_cast<std::uint8_t>(entry.side));
    for (std::size_t index = 0;
         index < state.side_override_count;
         ++index) {
        auto& override = state.side_overrides[index];
        if (override.record != record) {
            continue;
        }
        // A single native FacilityData record can be shared by multiple map
        // cells. Refuse conflicting per-cell owners instead of silently
        // changing the first cell's faction when the second one is attached.
        return read_value<std::uint8_t>(encoded_side) == requested;
    }
    if (state.side_override_count >= state.side_overrides.size()) {
        return false;
    }

    auto& override = state.side_overrides[state.side_override_count++];
    override.record = record;
    override.original = read_value<std::uint8_t>(encoded_side);
    std::memcpy(encoded_side, &requested, sizeof(requested));
#if defined(__ANDROID__)
    __android_log_print(
        ANDROID_LOG_INFO,
        "feh-engage",
        "facility side override record=%p requested=%d encoded=%u original=%u",
        static_cast<void*>(record),
        static_cast<int>(entry.side),
        static_cast<unsigned>(requested),
        static_cast<unsigned>(override.original));
#endif
    return true;
}

[[nodiscard]] std::byte* create_native_facility_container(
    std::uintptr_t module_base,
    std::byte* field,
    std::byte* map_object) noexcept {
    using CreateFacilityContainerFn = void* (*)(void*, void*, void*);
    using AddChildFn = void (*)(void*, void*, std::int32_t);
    using SetPositionFn = void (*)(void*, const float*);
    using GetMapFieldBoundsFn = void (*)(void*);
    using GetRectCenterFn = float (*)(const float*);

    if (field == nullptr || map_object == nullptr) {
        return nullptr;
    }

    // Reproduce only FieldSkyCastle::CreateField's FacilityContainer call.
    // Creating the entire FieldSkyCastle object also installs its terrain,
    // background, unit layers, and overwrites the global current Field. The
    // container below is the native facility renderer/interaction object and
    // retains the original lookup/state callbacks without duplicating those
    // unrelated layers in an ordinary PvE map.
    alignas(8) std::array<std::uintptr_t, 8> lookup_callback{};
    lookup_callback[0] = module_base + rva::kFacilityLookupCallbackVtable;
    lookup_callback[1] = reinterpret_cast<std::uintptr_t>(map_object);
    lookup_callback[4] = reinterpret_cast<std::uintptr_t>(
        lookup_callback.data());

    alignas(8) std::array<std::uintptr_t, 6> state_callback{};
    state_callback[0] = module_base + rva::kFacilityStateCallbackVtable;
    state_callback[1] = (module_base + rva::kFacilityBattleCallback) | 1U;
    state_callback[4] = reinterpret_cast<std::uintptr_t>(
        state_callback.data());

    const auto create_container =
        resolve_game_function<CreateFacilityContainerFn>(
            module_base, rva::kCreateFacilityContainer);
    auto* container = static_cast<std::byte*>(create_container(
        lookup_callback.data(), state_callback.data(), map_object + 4));
    const auto destroy_state = resolve_game_function<void (*)(void*)>(
        module_base, rva::kDestroyFunctionObject);
    const auto destroy_lookup = resolve_game_function<void (*)(void*)>(
        module_base, rva::kDestroyFacilityLookupFunction);
    destroy_state(state_callback.data());
    destroy_lookup(lookup_callback.data());
    if (container == nullptr || field == nullptr) {
        return nullptr;
    }

    // Field::CreateField positions every root child at the center of the
    // current map rectangle before attaching it. Reuse the same game helpers
    // so the native facility grid (which is centered around that origin) is
    // aligned with ordinary PvE maps of the supported 6x8 format.
    const auto get_bounds = resolve_game_function<GetMapFieldBoundsFn>(
        module_base, rva::kGetMapFieldBounds);
    const auto get_center_x = resolve_game_function<GetRectCenterFn>(
        module_base, rva::kGetMapRectCenterX);
    const auto get_center_y = resolve_game_function<GetRectCenterFn>(
        module_base, rva::kGetMapRectCenterY);
    auto* node_vtable = read_value<std::byte*>(container);
    if (node_vtable == nullptr
        || get_bounds == nullptr
        || get_center_x == nullptr
        || get_center_y == nullptr) {
        return nullptr;
    }
    alignas(8) std::array<float, 4> bounds{};
    get_bounds(bounds.data());
    const std::array<float, 2> origin{
        get_center_x(bounds.data()),
        get_center_y(bounds.data()),
    };
    const auto set_position = read_value<SetPositionFn>(node_vtable + 76);
    if (set_position == nullptr) {
        return nullptr;
    }
    set_position(container, origin.data());

    auto* field_vtable = read_value<std::byte*>(field);
    if (field_vtable == nullptr) {
        return nullptr;
    }
    const auto add_child = read_value<AddChildFn>(field_vtable + 252);
    if (add_child == nullptr) {
        return nullptr;
    }
    // FieldSkyCastle adds its FacilityContainer at z=1 inside the field node.
    add_child(field, container, 1);

#if defined(__ANDROID__)
    __android_log_print(
        ANDROID_LOG_INFO,
        "feh-engage",
        "native facility container field=%p node=%p map=%p grid=%p origin=%.1f,%.1f",
        static_cast<void*>(field),
        static_cast<void*>(container),
        static_cast<void*>(map_object),
        static_cast<void*>(map_object + 4),
        origin[0],
        origin[1]);
#endif
    return container;
}

void log_native_facility_selection_layout(
    std::uintptr_t module_base,
    std::byte* container,
    std::byte* entity_grid,
    std::int32_t grid_x,
    std::int32_t grid_y) noexcept {
#if defined(__ANDROID__)
    if (container == nullptr || entity_grid == nullptr) {
        return;
    }
    using GetFacilityEntityFn = std::byte* (*)(
        std::byte*, std::int32_t, std::int32_t);
    const auto get_entity = resolve_game_function<GetFacilityEntityFn>(
        module_base, rva::kGetFacilityContainerEntity);
    if (get_entity == nullptr) {
        return;
    }
    auto* entity_slot = get_entity(entity_grid, grid_x, grid_y);
    auto* entity = read_value<std::byte*>(entity_slot);
    auto* container_vtable = read_value<std::byte*>(container);
    auto* entity_vtable = read_value<std::byte*>(entity);
    const auto entity_callback = entity_vtable == nullptr
        ? static_cast<std::uintptr_t>(0)
        : read_value<std::uintptr_t>(entity_vtable + 596);
    const auto entity_state = read_value<std::uintptr_t>(
        entity == nullptr ? nullptr : entity + 600);
    const auto container_grid = read_value<std::uintptr_t>(
        container + 600);
    const auto container_outer_layer = read_value<std::uintptr_t>(
        container + 604);
    __android_log_print(
        ANDROID_LOG_INFO,
        "feh-engage",
        "facility selection layout container=%p vtable=%p "
        "container_grid=%p outer=%p entity=%p entity_vtable=%p "
        "entity_cb596=%p entity_state=%p",
        static_cast<void*>(container),
        static_cast<void*>(container_vtable),
        reinterpret_cast<void*>(container_grid),
        reinterpret_cast<void*>(container_outer_layer),
        static_cast<void*>(entity),
        static_cast<void*>(entity_vtable),
        reinterpret_cast<void*>(entity_callback),
        reinterpret_cast<void*>(entity_state));
#else
    (void)module_base;
    (void)container;
    (void)entity_grid;
    (void)grid_x;
    (void)grid_y;
#endif
}


void reset_state(
    std::uintptr_t module_base,
    FacilityAttachState& state) noexcept {
    (void)module_base;
    for (std::size_t index = 0;
         index < state.side_override_count;
         ++index) {
        const auto& override = state.side_overrides[index];
        if (override.record != nullptr) {
            std::memcpy(
                override.record + 0x94,
                &override.original,
                sizeof(override.original));
        }
    }
    uninstall_selection_bridge(state);
    state = {};
}

void suspend_state(
    std::uintptr_t module_base,
    FacilityAttachState& state) noexcept {
    (void)module_base;
    for (std::size_t index = 0;
         index < state.side_override_count;
         ++index) {
        const auto& override = state.side_overrides[index];
        if (override.record != nullptr) {
            std::memcpy(
                override.record + 0x94,
                &override.original,
                sizeof(override.original));
        }
    }
    uninstall_selection_bridge(state);
    state = {};
}

}  // namespace

FacilityCfgReport ApplyExternalCfgFacilities(
    std::uintptr_t module_base) noexcept {
    FacilityCfgReport report{};
    try {
        log_stage("enter", module_base);
        auto& state = attach_state();
        auto* observed_field = read_value<std::byte*>(
            reinterpret_cast<const std::byte*>(
                module_base + rva::kCurrentFieldGlobal));

        // A real SkyCastle battle owns both the mode flag and its Field. Never
        // reset its battle map, replace its current Field, or release its flag.
        // If a normal-PvE attachment was active in the preceding scene, the
        // new native Field takes ownership and the old child dies with its
        // parent, so only forget our bookkeeping here.
        bool native_facility_mode = false;
        const bool have_mode = read_facility_mode(
            module_base, native_facility_mode);
        if (is_native_skycastle_field(
                module_base, state, observed_field)
            || (have_mode
                && native_facility_mode
                && observed_field == nullptr
                && state.field != nullptr)) {
            log_stage(
                "native-skycastle",
                reinterpret_cast<std::uintptr_t>(observed_field),
                native_facility_mode ? 1U : 0U);
            state = {};
            report.status = FacilityCfgStatus::skycastle_field;
            log_report(report);
            return report;
        }

        FacilityConfig config{};
        const auto* selected_cfg_path = GetSelectedExternalCfgPath();
        if (!read_config(selected_cfg_path, config)) {
            // The loader can observe one or more frames with no map-specific
            // CFG while FEH is transitioning between scenes. Drop the
            // attachment state; the ordinary Field owns the child lifetime.
            suspend_state(module_base, state);
            auto* probe = selected_cfg_path == nullptr
                ? nullptr : std::fopen(selected_cfg_path, "rb");
            report.status = probe == nullptr
                ? FacilityCfgStatus::no_config
                : FacilityCfgStatus::invalid_config;
            if (probe != nullptr) {
                std::fclose(probe);
            }
            log_report(report);
            return report;
        }
        report.configured = static_cast<std::uint16_t>(config.count);
        log_stage("config", config.count, config.fingerprint);
        if (config.count == 0) {
            suspend_state(module_base, state);
            report.status = FacilityCfgStatus::no_config;
            log_report(report);
            return report;
        }

        auto* battle_state = read_value<std::byte*>(
            reinterpret_cast<const std::byte*>(
                module_base + rva::kSkyCastleBattleStateGlobal));
        auto* map_object = battle_state == nullptr
            ? nullptr : battle_state + kSkyCastleMapOffset;
        log_stage("objects", reinterpret_cast<std::uintptr_t>(observed_field),
            reinterpret_cast<std::uintptr_t>(map_object));
        auto* field = observed_field;

        log_stage(
            "objects-check",
            reinterpret_cast<std::uintptr_t>(field),
            reinterpret_cast<std::uintptr_t>(map_object));
        if (field == nullptr || map_object == nullptr) {
            log_stage(
                "objects-wait",
                field == nullptr ? 0U : 1U,
                map_object == nullptr ? 0U : 1U);
            if (field == nullptr) {
                reset_state(module_base, state);
                report.status = FacilityCfgStatus::waiting_for_field;
            } else {
                report.status = FacilityCfgStatus::waiting_for_data;
            }
            log_report(report);
            return report;
        }

        if (state.field != field || state.map_object != map_object) {
            log_stage(
                "state-reset",
                reinterpret_cast<std::uintptr_t>(state.field),
                reinterpret_cast<std::uintptr_t>(state.map_object));
            reset_state(module_base, state);
            state.field = field;
            state.map_object = map_object;
            state.module_base = module_base;
        }
        log_stage(
            "state-check",
            static_cast<std::uintptr_t>(state.attached),
            static_cast<std::uintptr_t>(state.fingerprint));
        report.attached = static_cast<std::uint16_t>(state.attached);

        // The normal PvE Input object may be created a few frames after the
        // Field. Retry the vtable bridge on every attached pass until the
        // global points at the live srpg::map::node::Input instance.
        if (!state.selection_bridge_installed
            && !install_selection_bridge(module_base, state)) {
            log_stage("input-bridge-wait");
        }

        if (state.attached != 0 && state.fingerprint != config.fingerprint) {
            log_stage("config-changed", state.fingerprint, config.fingerprint);
            report.status = FacilityCfgStatus::config_changed;
            log_report(report);
            return report;
        }
        if (state.attached >= config.count) {
            log_stage("attached-check", state.attached, config.count);
            if (state.facility_container != nullptr
                && state.fingerprint == config.fingerprint) {
                if (!set_normal_pve_skycastle_context_gate(
                    module_base, map_object)) {
                    report.status = FacilityCfgStatus::effect_gate_failed;
                    log_report(report);
                    return report;
                }
            }
            // The first attachment can precede PvE unit construction.  The
            // bridge is invoked repeatedly while the map is open, so retry
            // the native pass from the already-attached path until a live
            // roster has allowed one complete invocation.
            run_native_facility_effect_pass(module_base, state);
            flush_pending_facility_selection(state);
            probe_native_facility_queries(
                module_base, map_object, config, state);
            report.status = FacilityCfgStatus::already_attached;
            log_report(report);
            return report;
        }

        std::int32_t battle_width = 0;
        std::int32_t battle_height = 0;
        log_stage("grid-before");
        if (!current_battle_is_six_by_eight(
                module_base, battle_width, battle_height)) {
            log_stage("grid-reject", battle_width, battle_height);
            report.status = FacilityCfgStatus::unsupported_map;
            log_report(report);
            return report;
        }
        log_stage("grid-ok", battle_width, battle_height);

        auto* manager = read_value<std::byte*>(
            reinterpret_cast<const std::byte*>(
                module_base + rva::kFacilityManagerGlobal));
        if (manager == nullptr) {
            report.status = FacilityCfgStatus::waiting_for_data;
            log_report(report);
            return report;
        }
        log_stage("manager", reinterpret_cast<std::uintptr_t>(manager));
        log_facility_manager_candidates(module_base, manager);

        using ResetBattleMapFn = std::int32_t (*)(std::byte*);
        const auto reset_map = resolve_game_function<ResetBattleMapFn>(
            module_base, rva::kResetSkyCastleBattleBattleMap);
        if (state.attached == 0) {
            // The constructor calls 0x1B79634 with the embedded battle::Map
            // at SkyCastleState + 0x210, not with the outer state object.
            log_stage("reset-before", reinterpret_cast<std::uintptr_t>(map_object));
            (void)reset_map(map_object);
            log_stage("reset-after", reinterpret_cast<std::uintptr_t>(map_object));
        }
        log_stage("gate-before", reinterpret_cast<std::uintptr_t>(map_object));
        if (!set_normal_pve_skycastle_context_gate(module_base, map_object)) {
            report.status = FacilityCfgStatus::effect_gate_failed;
            log_report(report);
            return report;
        }
        log_stage("gate-after", reinterpret_cast<std::uintptr_t>(map_object));

        while (state.attached < config.count) {
            const auto& entry = config.entries[state.attached];
            log_stage("lookup-before", entry.grid_x, entry.grid_y);
            std::string resolved_key;
            auto* record = find_facility_record(
                module_base, manager, entry.selector, resolved_key);
            if (record == nullptr || resolved_key.empty()) {
                log_stage("lookup-failed", reinterpret_cast<std::uintptr_t>(record));
                report.status = FacilityCfgStatus::facility_not_found;
                break;
            }
            log_stage("lookup-ok", reinterpret_cast<std::uintptr_t>(record));
            if (!apply_facility_side_override(entry, record, state)) {
                log_stage("side-override-conflict",
                    reinterpret_cast<std::uintptr_t>(record),
                    static_cast<std::uintptr_t>(
                        static_cast<std::uint32_t>(entry.side)));
                report.status = FacilityCfgStatus::create_failed;
                break;
            }
            if (!set_map_cell_key(
                module_base,
                map_object,
                entry,
                resolved_key,
                true)
                || !set_map_cell_state(module_base, map_object, entry)) {
                report.status = FacilityCfgStatus::create_failed;
                break;
            }
            const auto* stored_key = get_map_cell_key(
                module_base, map_object, entry);
            using FacilityLookupFn = std::byte* (*)(
                std::byte*, const std::byte*);
            const auto native_lookup = resolve_game_function<FacilityLookupFn>(
                module_base, rva::kFacilityLookup);
            auto* native_record = stored_key == nullptr
                ? nullptr : native_lookup(manager, stored_key);
            log_stage(
                "cell-verify",
                reinterpret_cast<std::uintptr_t>(stored_key),
                reinterpret_cast<std::uintptr_t>(native_record));
            if (native_record != record) {
                // Keep a diagnostic fallback for records whose first field is
                // already plain text rather than an encoded Cki string.
                if (!set_map_cell_key(
                        module_base,
                        map_object,
                        entry,
                        resolved_key,
                        false)) {
                    report.status = FacilityCfgStatus::facility_not_found;
                    break;
                }
                stored_key = get_map_cell_key(
                    module_base, map_object, entry);
                native_record = stored_key == nullptr
                    ? nullptr : native_lookup(manager, stored_key);
                log_stage(
                    "cell-verify-plain",
                    reinterpret_cast<std::uintptr_t>(stored_key),
                    reinterpret_cast<std::uintptr_t>(native_record));
                if (native_record != record) {
                    report.status = FacilityCfgStatus::facility_not_found;
                    break;
                }
            }
            log_stage("cell-ok", entry.grid_x, entry.grid_y);

#if defined(__ANDROID__)
            __android_log_print(
                ANDROID_LOG_INFO,
                "feh-engage",
                "native facility key=%.*s record=%p cell=%d,%d enabled=%d level=%d",
                static_cast<int>(resolved_key.size()),
                resolved_key.data(),
                static_cast<void*>(record),
                entry.grid_x,
                entry.grid_y,
                entry.enabled ? 1 : 0,
                entry.level);
#endif
            ++state.attached;
            report.attached = static_cast<std::uint16_t>(state.attached);
        }

        if (state.attached != config.count) {
            log_report(report);
            return report;
        }

        // Do not set map_mode + 556 for ordinary PvE. That flag is consumed by
        // the mode dispatcher and rebuilds the global current Field as
        // FieldSkyCastle, replacing the ordinary terrain and unit layers.
        // The native container remains display-only until the effect dispatch
        // gate can be reached without changing ARM code or the map mode.
        if (state.facility_container == nullptr) {
            state.facility_container = create_native_facility_container(
                module_base, field, map_object);
            if (state.facility_container == nullptr) {
                reset_state(module_base, state);
                report.status = FacilityCfgStatus::create_failed;
                log_report(report);
                return report;
            }

            auto* entity_grid = read_value<std::byte*>(
                state.facility_container + 600);
            using GetFacilityEntityFn = std::byte* (*)(
                std::byte*, std::int32_t, std::int32_t);
            const auto get_entity = resolve_game_function<GetFacilityEntityFn>(
                module_base, rva::kGetFacilityContainerEntity);
            for (std::size_t index = 0; index < config.count; ++index) {
                const auto& entry = config.entries[index];
                auto* entity_slot = entity_grid == nullptr
                    ? nullptr : get_entity(
                        entity_grid, entry.grid_x, entry.grid_y);
                auto* entity = read_value<std::byte*>(entity_slot);
                log_stage(
                    "entity",
                    reinterpret_cast<std::uintptr_t>(entity_grid),
                    reinterpret_cast<std::uintptr_t>(entity));
                if (entity == nullptr) {
                    reset_state(module_base, state);
                    report.status = FacilityCfgStatus::create_failed;
                    log_report(report);
                    return report;
                }
            }
            if (!state.selection_layout_logged && config.count != 0) {
                const auto& first_entry = config.entries[0];
                log_native_facility_selection_layout(
                    module_base,
                    state.facility_container,
                    entity_grid,
                    first_entry.grid_x,
                    first_entry.grid_y);
                state.selection_layout_logged = true;
            }
            // The ordinary PvE Field does not enter the SkyCastle facility
            // state walkers. Run the native reconciliation/activation passes
            // once after the real container and entities exist. The passes
            // have their own mode guard, so scope the flag to this call and
            // restore the ordinary PvE value immediately afterwards.
            run_native_facility_effect_pass(module_base, state);

            // FieldSkyCastle::CreateField refreshes both field coordinate
            // layers for every cell immediately after attaching the native
            // FacilityContainer.  Ordinary PvE still owns the Field object;
            // invoking its existing virtual slots here wires the facility
            // state into that field without replacing its terrain or units.
            auto* field_vtable = read_value<std::byte*>(field);
            const auto update_coordinate = field_vtable == nullptr
                ? static_cast<std::uintptr_t>(0)
                : read_value<std::uintptr_t>(field_vtable + 664);
            const auto update_facility_state = field_vtable == nullptr
                ? static_cast<std::uintptr_t>(0)
                : read_value<std::uintptr_t>(field_vtable + 692);
            log_stage(
                "field-callbacks-before",
                update_coordinate,
                update_facility_state);
            if (update_coordinate == 0 || update_facility_state == 0) {
                report.status = FacilityCfgStatus::effect_gate_failed;
                log_report(report);
                return report;
            }
            using FieldCoordinateCallbackFn = void (*)(
                std::byte*, const std::byte*);
            const auto update_coordinate_fn =
                reinterpret_cast<FieldCoordinateCallbackFn>(
                    update_coordinate);
            const auto update_facility_state_fn =
                reinterpret_cast<FieldCoordinateCallbackFn>(
                    update_facility_state);
            using ConstructGridCoordinateFn = void* (*)(
                void*, std::int32_t, std::int32_t);
            const auto construct_coordinate =
                resolve_game_function<ConstructGridCoordinateFn>(
                    module_base, rva::kConstructGridCoordinate);
            if (construct_coordinate == nullptr) {
                report.status = FacilityCfgStatus::effect_gate_failed;
                log_report(report);
                return report;
            }
            for (std::int32_t y = 0; y < battle_height; ++y) {
                for (std::int32_t x = 0; x < battle_width; ++x) {
                    alignas(8) std::array<std::byte, 16> coordinate{};
                    construct_coordinate(coordinate.data(), x, y);
                    update_coordinate_fn(field, coordinate.data());
                    update_facility_state_fn(field, coordinate.data());
                }
            }
            log_stage("field-callbacks-after", battle_width, battle_height);
        }

        if (!state.selection_bridge_installed
            && !install_selection_bridge(module_base, state)) {
            // Input is a separate scene node and may not exist in the same
            // frame as the Field. Facility rendering remains valid; the
            // bridge will be retried from the attached path above.
            log_stage("input-bridge-wait");
        }

        auto* field_vtable = read_value<std::byte*>(field);
        const auto update_coordinate = field_vtable == nullptr
            ? static_cast<std::uintptr_t>(0)
            : read_value<std::uintptr_t>(field_vtable + 664);
        const auto update_facility_state = field_vtable == nullptr
            ? static_cast<std::uintptr_t>(0)
            : read_value<std::uintptr_t>(field_vtable + 692);
        log_stage("field-effect-callbacks", update_coordinate, update_facility_state);
        if (update_coordinate == 0 || update_facility_state == 0) {
            report.status = FacilityCfgStatus::effect_gate_failed;
            log_report(report);
            return report;
        }

        probe_native_facility_queries(
            module_base, map_object, config, state);
        flush_pending_facility_selection(state);
        state.fingerprint = config.fingerprint;
        report.status = FacilityCfgStatus::attached;
        log_report(report);
        return report;
    } catch (...) {
        report.status = FacilityCfgStatus::create_failed;
        log_report(report);
        return report;
    }
}

}  // namespace feh::mod
