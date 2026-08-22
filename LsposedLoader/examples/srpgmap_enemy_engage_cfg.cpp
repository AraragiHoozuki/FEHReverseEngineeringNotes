#include "feh/srpgmap_enemy_engage_cfg.hpp"

#include <array>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace feh::mod {
namespace {

constexpr std::size_t kMapContextStride = 0xE0;
constexpr std::size_t kMapContextMapId = 0x2C;
constexpr std::size_t kMapContextVector = 0xA8;
// The map owner stores the active context index at +0xD8.  The original
// runtime helper sub_166C80C reads this index and then selects one 0xE0-byte
// context from the vector at +0xA8.
constexpr std::size_t kMapContextCurrentIndex = 0xD8;
constexpr std::size_t kMapCoordinate = 0x168;
constexpr std::size_t kMaximumMapContexts = 64;
constexpr std::size_t kMaximumFileBytes = 64 * 1024;
constexpr std::size_t kCkiStringSize = 16;
constexpr std::size_t kMaximumCkiTextBytes = 255;
constexpr std::size_t kMaximumRegistryEntries = 8192;

struct RuntimeUnitState {
    RuntimeUnit* unit{};
    std::int32_t x{};
    std::int32_t y{};
    bool used{};
};

struct ConfigCandidate {
    ExternalEngageConfig config{};
    std::string map_id;
    std::string path;
    std::size_t score{};
};

enum class ConfigSection : std::uint8_t {
    none,
    engage,
    facility,
};

[[nodiscard]] std::string& selected_cfg_path() {
    static std::string path;
    return path;
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

[[nodiscard]] std::uint64_t hash_bytes(
    std::uint64_t hash,
    const void* data,
    std::size_t size) noexcept {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
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

[[nodiscard]] bool parse_integer(
    std::string_view text,
    std::int32_t& output) noexcept {
    const auto trimmed = trim_copy(text);
    if (trimmed.empty()) {
        return false;
    }
    const char* begin = trimmed.data();
    const char* end = begin + trimmed.size();
    const auto result = std::from_chars(begin, end, output, 10);
    return result.ec == std::errc{} && result.ptr == end;
}

[[nodiscard]] bool read_file(
    const char* path,
    std::string& output) {
    output.clear();
    if (path == nullptr) {
        return false;
    }
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    std::array<char, 4096> buffer{};
    std::size_t total = 0;
    bool ok = true;
    for (;;) {
        const auto read_count = read(fd, buffer.data(), buffer.size());
        if (read_count < 0) {
            ok = false;
            break;
        }
        if (read_count == 0) {
            break;
        }
        total += static_cast<std::size_t>(read_count);
        if (total > kMaximumFileBytes) {
            ok = false;
            break;
        }
        output.append(buffer.data(), static_cast<std::size_t>(read_count));
    }
    close(fd);
    return ok;
}

[[nodiscard]] bool path_exists(const std::string& path) noexcept {
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

[[nodiscard]] bool split_config_line(
    std::string_view line,
    std::array<std::string, 5>& fields) {
    std::size_t begin = 0;
    for (std::size_t column = 0; column < fields.size(); ++column) {
        const auto comma = line.find(',', begin);
        if (column + 1 == fields.size()) {
            if (comma != std::string_view::npos) {
                return false;
            }
            fields[column] = trim_copy(line.substr(begin));
            return !fields[column].empty();
        }
        if (comma == std::string_view::npos) {
            return false;
        }
        fields[column] = trim_copy(line.substr(begin, comma - begin));
        if (fields[column].empty()) {
            return false;
        }
        begin = comma + 1;
    }
    return false;
}

[[nodiscard]] bool looks_like_engage_line(std::string_view line) {
    std::array<std::string, 5> fields{};
    if (!split_config_line(line, fields)) {
        return false;
    }
    return fields[2].rfind("PID_", 0) == 0
        && fields[3].rfind("PID_", 0) == 0;
}

[[nodiscard]] bool decode_string_object(
    const std::array<std::byte, kCkiStringSize>& object,
    std::string& output) {
    const auto tag = std::to_integer<std::uint8_t>(object[0]);
    // The packed lookup string uses bit 0 as the long-string marker.  The
    // game tests this with `tag << 31`; short strings encode length as tag/2.
    const bool is_long = (tag & 1U) != 0;
    std::uint32_t length = 0;
    const char* data = nullptr;
    if (is_long) {
        std::memcpy(&length, object.data() + 4, sizeof(length));
        std::memcpy(&data, object.data() + 8, sizeof(data));
    } else {
        length = tag >> 1U;
        data = reinterpret_cast<const char*>(object.data() + 1);
    }
    if (data == nullptr || length > 128) {
        return false;
    }
    output.assign(data, static_cast<std::size_t>(length));
    for (const unsigned char value : output) {
        if (value == 0 || (value < 0x20U && value != '\t')) {
            return false;
        }
    }
    return !output.empty();
}

[[nodiscard]] bool decode_string_object(
    const std::byte* object,
    std::string& output) {
    if (object == nullptr) {
        return false;
    }
    std::array<std::byte, kCkiStringSize> copy{};
    std::memcpy(copy.data(), object, copy.size());
    return decode_string_object(copy, output);
}

[[nodiscard]] bool read_protected_map_id(
    const ExternalCfgBindings& bindings,
    const std::byte* source,
    std::string& output) {
    alignas(8) std::array<std::byte, kCkiStringSize> object{};
    bindings.decode_protected_string(
        object.data(), const_cast<std::byte*>(source));
    const bool decoded = decode_string_object(object, output);
    bindings.destroy_lookup_string(object.data());
    return decoded;
}

struct RegistryPersonEntry {
    std::string pid;
    PersonDefinition* person{};
};

struct RegistryPersonCache {
    void* registry{};
    std::array<RegistryPersonEntry, kMaximumRegistryEntries> entries{};
    std::size_t count{};
    bool built{};
};

[[nodiscard]] RegistryPersonCache& registry_cache() {
    static RegistryPersonCache cache{};
    return cache;
}

void build_registry_cache(void* registry) {
    auto& cache = registry_cache();
    if (cache.built && cache.registry == registry) {
        return;
    }
    cache = {};
    cache.registry = registry;
    cache.built = true;
    if (registry == nullptr) {
        return;
    }

    std::byte* const* bucket_table = nullptr;
    std::uint32_t bucket_count = 0;
    // get_person_by_pid passes registry + 4 to the hash-table helper. The
    // registry's first field is not the bucket table.
    const auto* registry_bytes = reinterpret_cast<const std::byte*>(registry);
    std::memcpy(&bucket_table, registry_bytes + 4, sizeof(bucket_table));
    std::memcpy(&bucket_count, registry_bytes + 8, sizeof(bucket_count));
    if (bucket_table == nullptr || bucket_count == 0 || bucket_count > 65536) {
        return;
    }

    std::size_t node_count = 0;
    std::size_t decoded_count = 0;
    for (std::uint32_t bucket = 0; bucket < bucket_count; ++bucket) {
        std::byte* node = nullptr;
        std::memcpy(&node, reinterpret_cast<const std::byte*>(bucket_table)
                        + static_cast<std::size_t>(bucket) * sizeof(node),
                    sizeof(node));
        for (std::size_t depth = 0; node != nullptr && depth < 4096; ++depth) {
            ++node_count;
            std::byte* next = nullptr;
            std::memcpy(&next, node, sizeof(next));

            std::string key;
            if (decode_string_object(node + 8, key)) {
                ++decoded_count;
                if (cache.count < cache.entries.size()) {
                    PersonDefinition* person = nullptr;
                    std::memcpy(&person, node + 20, sizeof(person));
                    cache.entries[cache.count++] = {
                        std::move(key), person};
                }
            }
            node = next;
        }
    }
#if defined(__ANDROID__)
    __android_log_print(
        ANDROID_LOG_INFO,
        "feh-engage",
        "cfg-registry scan registry=%p buckets=%u nodes=%zu decoded=%zu cached=%zu",
        registry,
        static_cast<unsigned>(bucket_count),
        node_count,
        decoded_count,
        cache.count);
#endif
}

[[nodiscard]] PersonDefinition* find_person_in_registry(
    void* registry,
    const std::string& pid) {
    if (registry == nullptr || pid.empty()) {
        return nullptr;
    }
    build_registry_cache(registry);
    const auto& cache = registry_cache();
    for (std::size_t index = 0; index < cache.count; ++index) {
        if (cache.entries[index].pid == pid) {
#if defined(__ANDROID__)
            __android_log_print(
                ANDROID_LOG_INFO,
                "feh-engage",
                "cfg-registry match pid=%s person=%p",
                pid.c_str(),
                static_cast<void*>(cache.entries[index].person));
#endif
            return cache.entries[index].person;
        }
    }
    return nullptr;
}

[[nodiscard]] PersonDefinition* lookup_person_by_pid(
    const ExternalCfgBindings& bindings,
    void* registry,
    const std::string& pid) {
    if (registry == nullptr || pid.empty()
        || bindings.get_person_by_pid == nullptr
        || bindings.transform_cki_string == nullptr
        || bindings.pack_lookup_string == nullptr) {
        return nullptr;
    }

    if (pid.size() > kMaximumCkiTextBytes) {
        return nullptr;
    }

    // Person registry keys are not plain UTF-8.  They are stored as Cki's
    // XOR-transformed bytes; sub_15A0624 is the same symmetric transform used
    // by the game's sub_15A0638 decoder.  Passing CFG text directly to the
    // packed-string lookup therefore hashes/compares the wrong byte sequence.
    std::array<char, kMaximumCkiTextBytes + 1> encoded{};
    bindings.transform_cki_string(
        encoded.data(),
        static_cast<std::uint32_t>(encoded.size()),
        pid.c_str());

    // get_person_by_pid consumes the packed libc++ string object produced by
    // sub_1D39316.  Pack the transformed Cki bytes, not the human-readable
    // CFG text, so the registry hash and memcmp see the same representation as
    // the original map loader.
    alignas(8) std::array<std::byte, kCkiStringSize> lookup{};
    bindings.pack_lookup_string(
        lookup.data(),
        encoded.data(),
        static_cast<std::uint32_t>(pid.size()));
    auto* person = bindings.get_person_by_pid(registry, lookup.data());
#if defined(__ANDROID__)
    if (person == nullptr) {
        std::uint32_t length = 0;
        std::uintptr_t data_pointer = 0;
        const auto tag = std::to_integer<std::uint8_t>(lookup[0]);
        if ((tag & 1U) != 0) {
            std::memcpy(&length, lookup.data() + 4, sizeof(length));
            std::memcpy(&data_pointer, lookup.data() + 8, sizeof(data_pointer));
        } else {
            length = tag >> 1U;
            data_pointer = reinterpret_cast<std::uintptr_t>(lookup.data() + 1);
        }
        static std::array<std::uint64_t, 64> logged_misses{};
        const auto miss_key = hash_bytes(
            1469598103934665603ULL, pid.data(), pid.size())
            ^ reinterpret_cast<std::uintptr_t>(registry);
        bool already_logged = false;
        for (const auto value : logged_misses) {
            if (value == miss_key) {
                already_logged = true;
                break;
            }
        }
        if (!already_logged) {
            for (auto& value : logged_misses) {
                if (value == 0) {
                    value = miss_key;
                    break;
                }
            }
            __android_log_print(
                ANDROID_LOG_INFO,
                "feh-engage",
                "cfg-cki miss pid=%s tag=%02x len=%u data=%p registry=%p",
                pid.c_str(),
                static_cast<unsigned>(tag),
                static_cast<unsigned>(length),
                reinterpret_cast<void*>(data_pointer),
                registry);
        }
    }
#endif
    bindings.destroy_lookup_string(lookup.data());
    return person;
}

[[nodiscard]] PersonDefinition* resolve_pid(
    std::uintptr_t module_base,
    const ExternalCfgBindings& bindings,
    void* registry,
    const std::string& pid) {
    (void)module_base;
    std::array<std::string, 6> variants{};
    std::size_t variant_count = 0;
    variants[variant_count++] = pid;
    if (pid.rfind("PID_", 0) == 0 && pid.size() > 4) {
        const auto suffix = pid.substr(4);
        variants[variant_count++] = "P_" + suffix;
        variants[variant_count++] = "P" + suffix;
        variants[variant_count++] = "E_" + suffix;
        variants[variant_count++] = "E" + suffix;
        variants[variant_count++] = suffix;
    }
    for (std::size_t index = 0; index < variant_count; ++index) {
        if (auto* result = lookup_person_by_pid(
                bindings, registry, variants[index])) {
            return result;
        }
    }
    // The direct hash lookup is the normal path.  A single cached registry
    // walk is retained as a diagnostic/compatibility fallback for builds where
    // the exported lookup uses a different registry partition.
    for (std::size_t index = 0; index < variant_count; ++index) {
        if (auto* result = find_person_in_registry(registry, variants[index])) {
            return result;
        }
    }
    return nullptr;
}

[[nodiscard]] std::string config_path_for_map(const std::string& map_id) {
    return std::string{kCfgDirectory} + "/" + map_id + ".cfg";
}

[[nodiscard]] bool resolve_context_cfg_path(
    std::uintptr_t module_base,
    const ExternalCfgBindings& bindings,
    std::string& map_id,
    std::string& path) {
    (void)module_base;
    map_id.clear();
    path.clear();

    // The native SRPGMap loader receives the active map-mode object and calls
    // sub_16132C8(map_id_string, map_mode). Reuse that exact helper instead
    // of selecting a CFG by inspecting its contents.
    auto* map_mode = *reinterpret_cast<std::byte**>(
        module_base + rva::kMapModeGlobal);
    if (map_mode != nullptr && bindings.get_current_map_id != nullptr) {
        alignas(8) std::array<std::byte, kCkiStringSize> object{};
        bindings.get_current_map_id(object.data(), map_mode);
        const bool decoded = decode_string_object(object, map_id);
        bindings.destroy_lookup_string(object.data());
        if (decoded && !map_id.empty()) {
            path = config_path_for_map(map_id);
            return true;
        }
        map_id.clear();
    }

    auto* map_owner = *reinterpret_cast<std::byte**>(
        module_base + rva::kCurrentMapOwnerGlobal);
    if (map_owner == nullptr || bindings.get_current_map_root == nullptr
        || bindings.get_context_vector == nullptr) {
        return false;
    }
    auto* map_root = static_cast<std::byte*>(
        bindings.get_current_map_root(
            map_owner + offset::kMapOwnerCurrentRoot));
    if (map_root == nullptr) {
        return false;
    }
    const auto current_index = bindings.engage.get_map_protected_int(
        reinterpret_cast<RuntimeProtectedScalar32*>(
            map_root + kMapContextCurrentIndex));
    auto* context_begin = static_cast<std::byte*>(
        bindings.get_context_vector(map_root + kMapContextVector));
    if (current_index < 0
        || current_index >= static_cast<std::int32_t>(kMaximumMapContexts)
        || context_begin == nullptr) {
        return false;
    }
    auto* context = context_begin
        + static_cast<std::size_t>(current_index) * kMapContextStride;
    if (!read_protected_map_id(
            bindings, context + kMapContextMapId, map_id)) {
        map_id.clear();
        return false;
    }
    path = config_path_for_map(map_id);
    return true;
}

[[nodiscard]] std::size_t count_matching_entries(
    std::uintptr_t module_base,
    const ExternalCfgBindings& bindings,
    void* registry,
    const ExternalEngageConfig& config,
    const std::array<RuntimeUnitState, detail::kMaximumRuntimeUnits>& units,
    std::size_t unit_count) {
    std::size_t score = 0;
    for (std::size_t index = 0; index < config.count; ++index) {
        const auto& entry = config.entries[index];
        auto* target = resolve_pid(module_base, bindings, registry, entry.target_pid);
        std::size_t coordinate_matches = 0;
        bool exact_match = false;
        for (std::size_t unit_index = 0; unit_index < unit_count; ++unit_index) {
            if (units[unit_index].x != entry.x
                || units[unit_index].y != entry.y) {
                continue;
            }
            ++coordinate_matches;
            auto* unit_bytes = reinterpret_cast<std::byte*>(units[unit_index].unit);
            auto* main_person = static_cast<PersonDefinition*>(
                bindings.engage.get_unit_protected_pointer(
                    unit_bytes + offset::kUnitMainPerson));
            if (target != nullptr && main_person == target) {
                exact_match = true;
                break;
            }
        }
        // Ordinary PVE duplicates PersonDefinition objects while constructing
        // RuntimeUnit instances. If the coordinate identifies exactly one
        // unit, it is the stable selector and the registry pointer mismatch
        // must not prevent the external CFG from applying.
        if (exact_match || coordinate_matches == 1) {
            ++score;
        }
    }
    return score;
}

[[nodiscard]] bool is_cfg_filename(const char* name) noexcept {
    if (name == nullptr) {
        return false;
    }
    const auto length = std::strlen(name);
    constexpr std::size_t suffix_length = 4;
    return length > suffix_length
        && std::strcmp(name + length - suffix_length, ".cfg") == 0
        && std::strcmp(name, "current.cfg") != 0;
}

void scan_cfg_directory(
    std::uintptr_t module_base,
    const ExternalCfgBindings& bindings,
    void* registry,
    const std::array<RuntimeUnitState, detail::kMaximumRuntimeUnits>& units,
    std::size_t unit_count,
    std::array<ConfigCandidate, kMaximumMapContexts>& candidates,
    std::size_t& candidate_count) {
    DIR* directory = opendir(kCfgDirectory);
    if (directory == nullptr) {
        return;
    }
    while (auto* entry = readdir(directory)) {
        if (!is_cfg_filename(entry->d_name)
            || candidate_count >= candidates.size()) {
            continue;
        }
        const std::string filename(entry->d_name);
        const std::string map_id = filename.substr(0, filename.size() - 4);
        const std::string path = std::string{kCfgDirectory} + "/" + filename;
        ConfigCandidate candidate{};
        candidate.map_id = map_id;
        candidate.path = path;
        if (!parse_external_engage_config(path.c_str(), candidate.config)) {
            continue;
        }
        candidate.score = count_matching_entries(
            module_base,
            bindings,
            registry,
            candidate.config,
            units,
            unit_count);
        candidates[candidate_count++] = std::move(candidate);
    }
    closedir(directory);
}

[[nodiscard]] ExternalCfgReport apply_external_cfg_engage_impl(
    std::uintptr_t module_base,
    const ExternalCfgBindings& bindings,
    EnemyEngageOptions options) {
    selected_cfg_path().clear();
    ExternalCfgReport report{};
    if (module_base == 0 || !bindings.ready()) {
        report.status = ExternalCfgStatus::missing_binding;
        return report;
    }

    std::string context_map_id;
    std::string context_cfg_path;
    if (resolve_context_cfg_path(
            module_base, bindings, context_map_id, context_cfg_path)) {
        selected_cfg_path() = context_cfg_path;
        report.map_id = context_map_id;
        report.path = context_cfg_path;
    }

    auto* unit_manager = *reinterpret_cast<std::byte**>(
        module_base + rva::kRuntimeUnitManagerGlobal);
    if (unit_manager == nullptr) {
        report.status = ExternalCfgStatus::waiting_for_units;
        return report;
    }

    std::array<RuntimeUnitState, detail::kMaximumRuntimeUnits> units{};
    std::size_t unit_count = 0;
    for (std::size_t side = 0; side < offset::kRuntimeUnitRosterCount; ++side) {
        auto* roster = unit_manager + offset::kRuntimeUnitRosterBase
                       + side * offset::kRuntimeUnitRosterStride;
        RuntimeUnit** begin = nullptr;
        RuntimeUnit** end = nullptr;
        RuntimeUnit** capacity = nullptr;
        std::memcpy(&begin, roster, sizeof(begin));
        std::memcpy(&end, roster + sizeof(begin), sizeof(end));
        std::memcpy(&capacity, roster + sizeof(begin) + sizeof(end), sizeof(capacity));
        if (begin == nullptr && end == nullptr && capacity == nullptr) {
            continue;
        }
        if (begin == nullptr || end < begin || capacity < end) {
            report.status = ExternalCfgStatus::invalid_runtime_state;
            return report;
        }
        const auto count = static_cast<std::size_t>(end - begin);
        if (count > detail::kMaximumUnitsPerRoster
            || unit_count + count > units.size()) {
            report.status = ExternalCfgStatus::invalid_runtime_state;
            return report;
        }
        for (std::size_t index = 0; index < count; ++index) {
            auto* unit = begin[index];
            if (unit == nullptr) {
                continue;
            }
            auto* coordinate = reinterpret_cast<std::byte*>(unit) + kMapCoordinate;
            units[unit_count++] = {
                unit,
                bindings.get_coordinate_x(coordinate),
                bindings.get_coordinate_y(coordinate),
                false,
            };
        }
    }
    if (unit_count == 0) {
        report.status = ExternalCfgStatus::waiting_for_units;
        return report;
    }

    auto* registry = *reinterpret_cast<void**>(
        module_base + rva::kPersonRegistryGlobal);
    if (registry == nullptr) {
        report.status = ExternalCfgStatus::waiting_for_map;
        return report;
    }

    std::array<ConfigCandidate, kMaximumMapContexts> candidates{};
    std::size_t candidate_count = 0;
    // The map ID above is authoritative. Parse only its same-named CFG.
    if (!context_cfg_path.empty() && path_exists(context_cfg_path)) {
        ConfigCandidate candidate{};
        candidate.map_id = context_map_id;
        candidate.path = context_cfg_path;
        if (!parse_external_engage_config(
                candidate.path.c_str(), candidate.config)) {
            report.status = ExternalCfgStatus::invalid_config;
            report.map_id = candidate.map_id;
            report.path = candidate.path;
            return report;
        }
        candidate.score = count_matching_entries(
            module_base,
            bindings,
            registry,
            candidate.config,
            units,
            unit_count);
        candidates[candidate_count++] = std::move(candidate);
    }

    auto* map_owner = *reinterpret_cast<std::byte**>(
        module_base + rva::kCurrentMapOwnerGlobal);
    if (candidate_count == 0 && map_owner != nullptr) {
        auto* map_root = static_cast<std::byte*>(
            bindings.get_current_map_root(
                map_owner + offset::kMapOwnerCurrentRoot));
#if defined(__ANDROID__)
        if (map_root == nullptr) {
            static std::uintptr_t last_null_owner = 0;
            if (last_null_owner != reinterpret_cast<std::uintptr_t>(map_owner)) {
                last_null_owner = reinterpret_cast<std::uintptr_t>(map_owner);
                __android_log_print(
                    ANDROID_LOG_INFO,
                    "feh-engage",
                    "cfg-probe owner=%p root=null",
                    static_cast<void*>(map_owner));
            }
        }
#endif
        if (map_root != nullptr) {
            const auto current_index = bindings.engage.get_map_protected_int(
                reinterpret_cast<RuntimeProtectedScalar32*>(
                    map_root + kMapContextCurrentIndex));
            auto* context_begin = static_cast<std::byte*>(
                bindings.get_context_vector(map_root + kMapContextVector));
#if defined(__ANDROID__)
            static std::uint64_t last_probe_key = 0;
            std::uint64_t probe_key =
                reinterpret_cast<std::uintptr_t>(map_owner)
                ^ (reinterpret_cast<std::uintptr_t>(map_root) << 7U)
                ^ (static_cast<std::uint64_t>(
                       static_cast<std::uint32_t>(current_index)) << 32U)
                ^ reinterpret_cast<std::uintptr_t>(context_begin);
            const bool log_probe = probe_key != last_probe_key;
            if (log_probe) {
                last_probe_key = probe_key;
                __android_log_print(
                    ANDROID_LOG_INFO,
                    "feh-engage",
                    "cfg-probe owner=%p root=%p index=%d vector=%p",
                    static_cast<void*>(map_owner),
                    static_cast<void*>(map_root),
                    current_index,
                    static_cast<void*>(context_begin));
            }
#endif
            if (current_index >= 0
                && current_index < static_cast<std::int32_t>(kMaximumMapContexts)
                && context_begin != nullptr) {
                auto* context = context_begin
                    + static_cast<std::size_t>(current_index) * kMapContextStride;
#if defined(__ANDROID__)
                if (log_probe) {
                    std::uint8_t bytes[12]{};
                    std::memcpy(bytes, context + kMapContextMapId, sizeof(bytes));
                    __android_log_print(
                        ANDROID_LOG_INFO,
                        "feh-engage",
                        "cfg-probe context=%p mapid-bytes=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
                        static_cast<void*>(context),
                        bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5],
                        bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11]);
                }
#endif
                std::string map_id;
                if (read_protected_map_id(
                        bindings,
                        context + kMapContextMapId,
                        map_id)) {
                    const auto path = config_path_for_map(map_id);
                    report.map_id = map_id;
                    report.path = path;
                    if (path_exists(path)) {
                        ConfigCandidate candidate{};
                        candidate.map_id = std::move(map_id);
                        candidate.path = path;
                        if (!parse_external_engage_config(
                                candidate.path.c_str(), candidate.config)) {
                            report.status = ExternalCfgStatus::invalid_config;
                            return report;
                        }
                        candidate.score = count_matching_entries(
                            module_base,
                            bindings,
                            registry,
                            candidate.config,
                            units,
                            unit_count);
                        candidates[candidate_count++] = std::move(candidate);
                    }
                }
            }
        }
    }

    if (candidate_count == 0) {
        report.status = ExternalCfgStatus::no_config;
        return report;
    }

    // There is exactly one candidate: the CFG whose filename matches the
    // active runtime map ID. Unit matching only decides whether Engage rows
    // apply; it never selects a different map's file.
    auto& candidate = candidates[0];
    selected_cfg_path() = candidate.path;
    report.map_id = candidate.map_id;
    report.path = candidate.path;
    report.configured = static_cast<std::uint16_t>(candidate.config.count);

#if defined(__ANDROID__)
    static std::uint64_t last_debug_key = 0;
    std::uint64_t debug_key = candidate.config.fingerprint
        ^ static_cast<std::uint64_t>(unit_count);
    for (std::size_t index = 0; index < unit_count; ++index) {
        const auto address = reinterpret_cast<std::uintptr_t>(units[index].unit);
        debug_key ^= address + (debug_key << 7U) + (debug_key >> 3U);
        debug_key ^= static_cast<std::uint64_t>(
            static_cast<std::uint32_t>(units[index].x) * 131U
            + static_cast<std::uint32_t>(units[index].y));
    }
    if (debug_key != last_debug_key) {
        last_debug_key = debug_key;
        __android_log_print(
            ANDROID_LOG_INFO,
            "feh-engage",
            "cfg-debug map=%s path=%s units=%zu entries=%zu",
            report.map_id.c_str(),
            report.path.c_str(),
            unit_count,
            candidate.config.count);
        for (std::size_t index = 0; index < unit_count; ++index) {
            auto* unit_bytes = reinterpret_cast<std::byte*>(units[index].unit);
            auto* main_person = static_cast<PersonDefinition*>(
                bindings.engage.get_unit_protected_pointer(
                    unit_bytes + offset::kUnitMainPerson));
            __android_log_print(
                ANDROID_LOG_INFO,
                "feh-engage",
                "cfg-debug unit[%zu]=%p xy=%d,%d main=%p",
                index,
                static_cast<void*>(units[index].unit),
                units[index].x,
                units[index].y,
                static_cast<void*>(main_person));
        }
        for (std::size_t index = 0; index < candidate.config.count; ++index) {
            const auto& entry = candidate.config.entries[index];
            auto* target = resolve_pid(
                module_base, bindings, registry, entry.target_pid);
            auto* engaged = resolve_pid(
                module_base, bindings, registry, entry.engage_pid);
            __android_log_print(
                ANDROID_LOG_INFO,
                "feh-engage",
                "cfg-debug entry[%zu] xy=%d,%d target=%s ptr=%p engage=%s ptr=%p level=%d",
                index,
                entry.x,
                entry.y,
                entry.target_pid.c_str(),
                static_cast<void*>(target),
                entry.engage_pid.c_str(),
                static_cast<void*>(engaged),
                entry.level);
        }
    }
#endif

    std::uint64_t signature = 1469598103934665603ULL;
    signature = hash_bytes(signature, candidate.map_id.data(), candidate.map_id.size());
    signature = hash_bytes(signature, &candidate.config.fingerprint,
                           sizeof(candidate.config.fingerprint));
    for (std::size_t index = 0; index < unit_count; ++index) {
        const auto address = reinterpret_cast<std::uintptr_t>(units[index].unit);
        signature = hash_bytes(signature, &address, sizeof(address));
        signature = hash_bytes(signature, &units[index].x, sizeof(units[index].x));
        signature = hash_bytes(signature, &units[index].y, sizeof(units[index].y));
    }
    static std::uintptr_t last_signature = 0;
    if (signature == last_signature) {
        report.status = ExternalCfgStatus::already_scanned;
        return report;
    }

    for (std::size_t index = 0; index < candidate.config.count; ++index) {
        const auto& entry = candidate.config.entries[index];
        auto* target = resolve_pid(module_base, bindings, registry, entry.target_pid);
        auto* engaged = resolve_pid(module_base, bindings, registry, entry.engage_pid);
        if (engaged == nullptr) {
            ++report.unmatched;
            continue;
        }
        bool matched = false;
        std::size_t coordinate_index = unit_count;
        std::size_t coordinate_matches = 0;
        for (std::size_t unit_index = 0; unit_index < unit_count; ++unit_index) {
            auto& state = units[unit_index];
            if (state.used || state.x != entry.x || state.y != entry.y) {
                continue;
            }
            coordinate_index = unit_index;
            ++coordinate_matches;
            auto* unit_bytes = reinterpret_cast<std::byte*>(state.unit);
            auto* main_person = static_cast<PersonDefinition*>(
                bindings.engage.get_unit_protected_pointer(
                    unit_bytes + offset::kUnitMainPerson));
            if (target != nullptr && main_person != target) {
                continue;
            }
            state.used = true;
            matched = true;
            if (TryApplyDirectEngage(
                    state.unit,
                    engaged,
                    entry.level,
                    bindings.engage,
                    options) == EnemyEngageApplyResult::applied) {
                ++report.applied;
            }
            break;
        }
        if (!matched && coordinate_matches == 1
            && coordinate_index < unit_count) {
            auto& state = units[coordinate_index];
            state.used = true;
            matched = true;
#if defined(__ANDROID__)
            __android_log_print(
                ANDROID_LOG_INFO,
                "feh-engage",
                "cfg-match coordinate-fallback xy=%d,%d target=%s unit=%p",
                entry.x,
                entry.y,
                entry.target_pid.c_str(),
                static_cast<void*>(state.unit));
#endif
            if (TryApplyDirectEngage(
                    state.unit,
                    engaged,
                    entry.level,
                    bindings.engage,
                    options) == EnemyEngageApplyResult::applied) {
                ++report.applied;
            }
        }
        if (!matched) {
            ++report.unmatched;
        }
    }

    if (report.applied != 0 && report.unmatched == 0) {
        last_signature = signature;
        report.status = ExternalCfgStatus::applied;
    } else if (report.unmatched != 0) {
        report.status = ExternalCfgStatus::waiting_for_units;
    } else {
        last_signature = signature;
        report.status = ExternalCfgStatus::no_config;
    }
    return report;
}

}  // namespace

ExternalCfgBindings make_external_cfg_bindings(
    std::uintptr_t module_base) noexcept {
    ExternalCfgBindings result{};
    result.engage = make_enemy_engage_bindings(module_base, nullptr);
    result.get_coordinate_x = resolve_game_function<GetCoordinateFn>(
        module_base, rva::kGetCoordinateX);
    result.get_coordinate_y = resolve_game_function<GetCoordinateFn>(
        module_base, rva::kGetCoordinateY);
    result.get_person_by_pid = resolve_game_function<GetPersonByPidFn>(
        module_base, rva::kGetPersonByPid);
    result.transform_cki_string = resolve_game_function<TransformCkiStringFn>(
        module_base, rva::kTransformCkiString);
    result.construct_string = resolve_game_function<ConstructStringFn>(
        module_base, rva::kConstructString);
    result.destroy_string = resolve_game_function<DestroyStringFn>(
        module_base, rva::kDestroyString);
    result.make_lookup_string = resolve_game_function<MakeLookupStringFn>(
        module_base, rva::kMakeLookupString);
    result.pack_lookup_string = resolve_game_function<PackLookupStringFn>(
        module_base, rva::kPackLookupString);
    result.destroy_lookup_string = resolve_game_function<DestroyLookupStringFn>(
        module_base, rva::kDestroyLookupString);
    result.decode_protected_string = resolve_game_function<DecodeProtectedStringFn>(
        module_base, rva::kDecodeProtectedString);
    result.get_context_vector = resolve_game_function<GetProtectedPointerFn>(
        module_base, rva::kGetMapContextVector);
    result.get_current_map_root = resolve_game_function<GetCurrentMapRootFn>(
        module_base, rva::kGetCurrentMapRoot);
    result.get_current_map_id = resolve_game_function<GetCurrentMapIdFn>(
        module_base, rva::kGetCurrentMapId);
    return result;
}

bool parse_external_engage_config(
    const char* path,
    ExternalEngageConfig& output) noexcept {
    output = {};
    try {
        std::string contents;
        if (!read_file(path, contents)) {
            return false;
        }
        std::uint64_t fingerprint = 1469598103934665603ULL;
        fingerprint = hash_bytes(fingerprint, contents.data(), contents.size());
        output.fingerprint = fingerprint;

        ConfigSection section = ConfigSection::none;
        bool saw_known_section = false;
        std::size_t line_begin = 0;
        while (line_begin <= contents.size()) {
            const auto line_end = contents.find('\n', line_begin);
            auto line = std::string_view(contents).substr(
                line_begin,
                line_end == std::string::npos
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
                    saw_known_section = true;
                    if (section == ConfigSection::facility) {
                        output.has_facility_section = true;
                    }
                    if (line_end == std::string::npos) {
                        break;
                    }
                    line_begin = line_end + 1;
                    continue;
                }
                // Facility rows are intentionally ignored here. In a merged
                // CFG, only the engage section belongs to this parser.
                if (section == ConfigSection::facility
                    || (section == ConfigSection::none
                        && !looks_like_engage_line(trimmed))) {
                    if (line_end == std::string::npos) {
                        break;
                    }
                    line_begin = line_end + 1;
                    continue;
                }
                if (output.count >= output.entries.size()) {
                    return false;
                }
                std::array<std::string, 5> fields{};
                if (!split_config_line(trimmed, fields)) {
                    return false;
                }
                std::int32_t x = 0;
                std::int32_t y = 0;
                std::int32_t level = 0;
                if (!parse_integer(fields[0], x)
                    || !parse_integer(fields[1], y)
                    || !parse_integer(fields[4], level)
                    || x < 0 || x > 63 || y < 0 || y > 63
                    || level < 0 || level > 10
                    || fields[2].rfind("PID_", 0) != 0
                    || fields[3].rfind("PID_", 0) != 0
                    || fields[2].size() <= 4 || fields[3].size() <= 4) {
                    return false;
                }
                auto& entry = output.entries[output.count++];
                entry.x = static_cast<std::int16_t>(x);
                entry.y = static_cast<std::int16_t>(y);
                entry.level = level;
                entry.target_pid = std::move(fields[2]);
                entry.engage_pid = std::move(fields[3]);
            }
            if (line_end == std::string::npos) {
                break;
            }
            line_begin = line_end + 1;
        }
        output.valid = output.count != 0 || saw_known_section;
        return output.valid;
    } catch (...) {
        output = {};
        return false;
    }
}

const char* GetSelectedExternalCfgPath() noexcept {
    const auto& path = selected_cfg_path();
    return path.empty() ? nullptr : path.c_str();
}

ExternalCfgReport ApplyExternalCfgEngage(
    std::uintptr_t module_base,
    const ExternalCfgBindings& bindings,
    EnemyEngageOptions options) noexcept {
    try {
        auto report = apply_external_cfg_engage_impl(
            module_base, bindings, options);
#if defined(__ANDROID__)
        static std::uint64_t last_report_key = 0;
        auto report_key = hash_bytes(
            1469598103934665603ULL,
            report.map_id.data(),
            report.map_id.size());
        report_key = hash_bytes(
            report_key,
            report.path.data(),
            report.path.size());
        report_key ^= static_cast<std::uint64_t>(report.status)
            << 8U;
        report_key ^= static_cast<std::uint64_t>(report.configured)
            << 16U;
        report_key ^= static_cast<std::uint64_t>(report.applied)
            << 32U;
        report_key ^= static_cast<std::uint64_t>(report.unmatched)
            << 48U;
        if (report_key == last_report_key) {
            return report;
        }
        last_report_key = report_key;
        __android_log_print(
            ANDROID_LOG_INFO,
            "feh-engage",
            "cfg map=%s path=%s status=%u configured=%u applied=%u unmatched=%u",
            report.map_id.c_str(),
            report.path.c_str(),
            static_cast<unsigned>(report.status),
            static_cast<unsigned>(report.configured),
            static_cast<unsigned>(report.applied),
            static_cast<unsigned>(report.unmatched));
#endif
        return report;
    } catch (...) {
        ExternalCfgReport report{};
        report.status = ExternalCfgStatus::invalid_runtime_state;
        return report;
    }
}

}  // namespace feh::mod
