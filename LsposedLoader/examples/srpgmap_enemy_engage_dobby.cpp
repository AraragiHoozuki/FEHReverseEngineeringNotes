#include "feh/srpgmap_enemy_engage_install.hpp"
#include "feh/srpgmap_enemy_engage_cfg.hpp"
#include "feh/skycastle_facility_cfg.hpp"

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <atomic>
#include <array>
#include <jni.h>
#include <link.h>

#if defined(__ANDROID__)
#include <android/log.h>
#include <pthread.h>
#include <unistd.h>
#endif

namespace {

int FindCocosModule(
    dl_phdr_info* info,
    std::size_t,
    void* output) noexcept {
    if (info == nullptr || info->dlpi_name == nullptr || output == nullptr) {
        return 0;
    }

    const char* name = std::strrchr(info->dlpi_name, '/');
    name = name == nullptr ? info->dlpi_name : name + 1;
    if (std::strcmp(name, "libcocos2dcpp.so") != 0) {
        return 0;
    }

    *static_cast<std::uintptr_t*>(output) =
        static_cast<std::uintptr_t>(info->dlpi_addr);
    return 1;
}

std::uintptr_t FindCocosLoadBias() noexcept {
    std::uintptr_t result = 0;
    dl_iterate_phdr(&FindCocosModule, &result);
    return result;
}

constexpr int kDefaultMaximumLimitBreakLevel = 10;
constexpr int kInstallRetryDelayMs = 200;
constexpr int kInstallRetryCount = 300;
#if defined(FEH_ENABLE_UNSAFE_FACILITY_PROBE)
constexpr const char* kFacilityTestConfigPath =
    "/data/local/tmp/feh-engage/facility_test.cfg";

struct FacilityDisplayConfig {
    std::uint32_t facility_value{34};
    float x{};
    float y{};
    float scale{1.0F};
};

bool ReadFacilityDisplayConfig(FacilityDisplayConfig& config) noexcept {
    auto* file = std::fopen(kFacilityTestConfigPath, "r");
    if (file == nullptr) {
        return false;
    }

    char line[192]{};
    bool parsed = false;
    while (std::fgets(line, sizeof(line), file) != nullptr) {
        const char* cursor = line;
        while (*cursor == ' ' || *cursor == '\t') {
            ++cursor;
        }
        if (*cursor == '\0' || *cursor == '\r' || *cursor == '\n' || *cursor == '#') {
            continue;
        }
        unsigned int facility_value = 0;
        FacilityDisplayConfig candidate{};
        if (std::sscanf(
                cursor,
                "%u,%f,%f,%f",
                &facility_value,
                &candidate.x,
                &candidate.y,
                &candidate.scale) == 4
            && candidate.scale > 0.0F) {
            candidate.facility_value = facility_value;
            config = candidate;
            parsed = true;
            break;
        }
    }
    std::fclose(file);
    return parsed;
}
#endif

void LogInstallStatus(const char* stage, int status) noexcept {
#if defined(__ANDROID__)
    __android_log_print(
        ANDROID_LOG_INFO,
        "feh-engage",
        "%s: status=%d",
        stage,
        status);
#else
    (void)stage;
    (void)status;
#endif
}

#if defined(__ANDROID__) && defined(FEH_ENABLE_UNSAFE_FACILITY_PROBE)
template <typename Value>
Value ReadRuntimeValue(const std::byte* address) noexcept {
    Value value{};
    if (address != nullptr) {
        std::memcpy(&value, address, sizeof(value));
    }
    return value;
}

void TryDisplayFacilityNode(
    std::uintptr_t module_base,
    std::byte* current_field,
    std::byte* facility_record,
    const FacilityDisplayConfig& config) noexcept {
    static std::byte* displayed_field = nullptr;
    static void* displayed_node = nullptr;

    if (current_field != displayed_field) {
        displayed_field = current_field;
        displayed_node = nullptr;
    }
    if (current_field == nullptr || facility_record == nullptr || displayed_node != nullptr) {
        return;
    }

    using GetFacilityLevelFn = std::int32_t (*)(std::byte*);
    using CreateFacilityNodeFn = void* (*)(
        std::byte*, std::int32_t, std::int32_t, std::int32_t,
        std::int32_t, std::int32_t, std::int32_t, void*);
    using DestroyFunctionObjectFn = void* (*)(void*);
    using SetScaleFn = void (*)(void*, float);
    using SetPositionFn = void (*)(void*, const float*);
    using AddChildFn = void (*)(void*, void*, std::int32_t);

    const auto get_level = feh::mod::resolve_game_function<GetFacilityLevelFn>(
        module_base, feh::mod::rva::kGetFacilityLevel);
    const auto create_node = feh::mod::resolve_game_function<CreateFacilityNodeFn>(
        module_base, feh::mod::rva::kCreateFacilityNode);
    const auto destroy_callback =
        feh::mod::resolve_game_function<DestroyFunctionObjectFn>(
            module_base, feh::mod::rva::kDestroyFunctionObject);

    std::array<std::uintptr_t, 7> callback{};
    callback[0] = module_base + feh::mod::rva::kFacilityCallbackVtable;
    callback[1] = (module_base + feh::mod::rva::kFacilityCallback) | 1U;
    callback[4] = reinterpret_cast<std::uintptr_t>(callback.data());

    const auto level = get_level(facility_record);
    auto* node = create_node(
        facility_record, level, 0, 0, 0, 1, 3, callback.data());
    (void)destroy_callback(callback.data());
    if (node == nullptr) {
        __android_log_print(
            ANDROID_LOG_INFO,
            "feh-engage",
            "facility-display create failed field=%p record=%p value=%u level=%d",
            static_cast<void*>(current_field),
            static_cast<void*>(facility_record),
            config.facility_value,
            level);
        return;
    }

    auto* node_vtable = ReadRuntimeValue<std::byte*>(static_cast<std::byte*>(node));
    auto* field_vtable = ReadRuntimeValue<std::byte*>(current_field);
    if (node_vtable == nullptr || field_vtable == nullptr) {
        return;
    }
    const auto set_scale = ReadRuntimeValue<SetScaleFn>(node_vtable + 64);
    const auto set_position = ReadRuntimeValue<SetPositionFn>(node_vtable + 76);
    const auto add_child = ReadRuntimeValue<AddChildFn>(field_vtable + 252);
    if (set_scale == nullptr || set_position == nullptr || add_child == nullptr) {
        return;
    }

    const float position[2] = {config.x, config.y};
    set_scale(node, config.scale);
    set_position(node, position);
    add_child(current_field, node, 100);
    displayed_node = node;
    __android_log_print(
        ANDROID_LOG_INFO,
        "feh-engage",
        "facility-display attached field=%p node=%p record=%p value=%u level=%d position=%.1f,%.1f scale=%.2f",
        static_cast<void*>(current_field),
        node,
        static_cast<void*>(facility_record),
        config.facility_value,
        level,
        static_cast<double>(config.x),
        static_cast<double>(config.y),
        static_cast<double>(config.scale));
}

void ProbeFacilityRuntime(std::uintptr_t module_base) noexcept {
    if (module_base == 0) {
        return;
    }

    auto* manager = ReadRuntimeValue<std::byte*>(reinterpret_cast<const std::byte*>(
        module_base + feh::mod::rva::kFacilityManagerGlobal));
    auto* map_owner = ReadRuntimeValue<std::byte*>(reinterpret_cast<const std::byte*>(
        module_base + feh::mod::rva::kCurrentMapOwnerGlobal));
    auto* current_field = ReadRuntimeValue<std::byte*>(reinterpret_cast<const std::byte*>(
        module_base + feh::mod::rva::kCurrentFieldGlobal));
    void* map_root = nullptr;
    if (map_owner != nullptr) {
        const auto get_root = feh::mod::resolve_game_function<
            feh::mod::GetCurrentMapRootFn>(module_base, feh::mod::rva::kGetCurrentMapRoot);
        map_root = get_root(map_owner + feh::mod::offset::kMapOwnerCurrentRoot);
    }

    const auto manager_words = manager == nullptr
        ? std::array<std::uint32_t, 6>{}
        : std::array<std::uint32_t, 6>{
            ReadRuntimeValue<std::uint32_t>(manager + 0),
            ReadRuntimeValue<std::uint32_t>(manager + 4),
            ReadRuntimeValue<std::uint32_t>(manager + 8),
            ReadRuntimeValue<std::uint32_t>(manager + 12),
            ReadRuntimeValue<std::uint32_t>(manager + 16),
            ReadRuntimeValue<std::uint32_t>(manager + 20),
        };
    const auto field_words = current_field == nullptr
        ? std::array<std::uint32_t, 6>{}
        : std::array<std::uint32_t, 6>{
            ReadRuntimeValue<std::uint32_t>(current_field + 0),
            ReadRuntimeValue<std::uint32_t>(current_field + 4),
            ReadRuntimeValue<std::uint32_t>(current_field + 8),
            ReadRuntimeValue<std::uint32_t>(current_field + 12),
            ReadRuntimeValue<std::uint32_t>(current_field + 16),
            ReadRuntimeValue<std::uint32_t>(current_field + 20),
        };
    // cocos2d::Node layout recovered from the Node destructor: +0x190/+0x194
    // are the children begin/end pair and +0x19C is the parent pointer stored
    // in every child. This remains read-only here.
    const auto field_children_begin = current_field == nullptr
        ? static_cast<std::byte*>(nullptr)
        : ReadRuntimeValue<std::byte*>(current_field + 0x190);
    const auto field_children_end = current_field == nullptr
        ? static_cast<std::byte*>(nullptr)
        : ReadRuntimeValue<std::byte*>(current_field + 0x194);
    const auto field_parent = current_field == nullptr
        ? static_cast<std::byte*>(nullptr)
        : ReadRuntimeValue<std::byte*>(current_field + 0x19C);
    const auto* table = manager == nullptr ? nullptr : manager + 4;
    auto* buckets = table == nullptr
        ? nullptr
        : ReadRuntimeValue<std::byte*>(table + 0);
    const auto bucket_count = table == nullptr
        ? 0U
        : ReadRuntimeValue<std::uint32_t>(table + 4);
    const auto table_count = table == nullptr
        ? 0U
        : ReadRuntimeValue<std::uint32_t>(table + 8);

    std::uint32_t ids[16]{};
    std::uint32_t id_count = 0;
    std::uint32_t node_count = 0;
    FacilityDisplayConfig display_config{};
    const bool display_enabled = ReadFacilityDisplayConfig(display_config);
    std::byte* display_record = nullptr;
    if (buckets != nullptr && bucket_count > 0 && bucket_count <= 4096) {
        for (std::uint32_t bucket = 0; bucket < bucket_count && id_count < 16; ++bucket) {
            auto* sentinel = ReadRuntimeValue<std::byte*>(
                buckets + static_cast<std::size_t>(bucket) * sizeof(void*));
            if (sentinel == nullptr) {
                continue;
            }
            auto* node = ReadRuntimeValue<std::byte*>(sentinel);
            for (std::uint32_t guard = 0; node != nullptr && guard < 512; ++guard) {
                ++node_count;
                auto* record = ReadRuntimeValue<std::byte*>(node + 20);
                if (record != nullptr) {
                    const auto encoded_id = ReadRuntimeValue<std::uint32_t>(record + 0x6C);
                    const auto facility_id = encoded_id ^ 0x94DD6C4AU;
                    if (display_record == nullptr
                        && display_enabled
                        && facility_id == display_config.facility_value
                        && ReadRuntimeValue<std::byte*>(record + 0x08) != nullptr) {
                        display_record = record;
                    }
                    bool seen = false;
                    for (std::uint32_t index = 0; index < id_count; ++index) {
                        seen = seen || ids[index] == facility_id;
                    }
                    if (!seen && id_count < 16) {
                        ids[id_count++] = facility_id;
                    }
                }
                node = ReadRuntimeValue<std::byte*>(node);
            }
        }
    }

    if (display_enabled) {
        TryDisplayFacilityNode(
            module_base, current_field, display_record, display_config);
    }

    std::uint64_t key = reinterpret_cast<std::uintptr_t>(manager)
        ^ (reinterpret_cast<std::uintptr_t>(current_field) << 3U)
        ^ (reinterpret_cast<std::uintptr_t>(map_root) << 7U)
        ^ (static_cast<std::uint64_t>(field_words[0]) << 11U)
        ^ (static_cast<std::uint64_t>(field_words[1]) << 13U)
        ^ (static_cast<std::uint64_t>(field_words[2]) << 17U)
        ^ (reinterpret_cast<std::uintptr_t>(field_children_begin) << 19U)
        ^ (reinterpret_cast<std::uintptr_t>(field_children_end) << 23U)
        ^ (reinterpret_cast<std::uintptr_t>(field_parent) << 27U)
        ^ (static_cast<std::uint64_t>(bucket_count) << 32U)
        ^ static_cast<std::uint64_t>(node_count);
    for (std::uint32_t index = 0; index < id_count; ++index) {
        key ^= static_cast<std::uint64_t>(ids[index]) << ((index % 4U) * 8U);
    }
    static std::uint64_t last_key = 0;
    if (key == last_key) {
        return;
    }
    last_key = key;

    __android_log_print(
        ANDROID_LOG_INFO,
        "feh-engage",
        "facility-probe manager=%p field=%p parent=%p children=%p..%p map_owner=%p map_root=%p table=%p buckets=%p bucket_count=%u table_aux=%p manager_words=%08x/%08x/%08x/%08x/%08x/%08x field_words=%08x/%08x/%08x/%08x/%08x/%08x nodes=%u",
        static_cast<void*>(manager),
        static_cast<void*>(current_field),
        static_cast<void*>(field_parent),
        static_cast<void*>(field_children_begin),
        static_cast<void*>(field_children_end),
        static_cast<void*>(map_owner),
        map_root,
        static_cast<const void*>(table),
        static_cast<void*>(buckets),
        bucket_count,
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(table_count)),
        manager_words[0], manager_words[1], manager_words[2],
        manager_words[3], manager_words[4], manager_words[5],
        field_words[0], field_words[1], field_words[2],
        field_words[3], field_words[4], field_words[5],
        node_count);
    __android_log_print(
        ANDROID_LOG_INFO,
        "feh-engage",
        "facility-probe ids=%u first=%u,%u,%u,%u,%u,%u,%u,%u",
        id_count,
        ids[0], ids[1], ids[2], ids[3], ids[4], ids[5], ids[6], ids[7]);
    if (map_root != nullptr) {
        auto* root_bytes = static_cast<const std::byte*>(map_root);
        __android_log_print(
            ANDROID_LOG_INFO,
            "feh-engage",
            "facility-probe root-words=%08x/%08x/%08x/%08x/%08x/%08x/%08x/%08x",
            ReadRuntimeValue<std::uint32_t>(root_bytes + 0x00),
            ReadRuntimeValue<std::uint32_t>(root_bytes + 0x04),
            ReadRuntimeValue<std::uint32_t>(root_bytes + 0x08),
            ReadRuntimeValue<std::uint32_t>(root_bytes + 0x0C),
            ReadRuntimeValue<std::uint32_t>(root_bytes + 0x10),
            ReadRuntimeValue<std::uint32_t>(root_bytes + 0x14),
            ReadRuntimeValue<std::uint32_t>(root_bytes + 0x18),
            ReadRuntimeValue<std::uint32_t>(root_bytes + 0x1C));
    }
}

#endif

}  // namespace

std::atomic<int> g_install_state{0};
std::atomic<bool> g_delayed_install_started{false};
std::atomic<std::uintptr_t> g_build_unit_resume{0};
std::atomic<std::uintptr_t> g_cocos_load_bias{0};
std::atomic<int> g_apply_debug_calls{0};

void feh::mod::ConfigureBuildUnitResume(std::uintptr_t address) noexcept {
    g_build_unit_resume.store(address, std::memory_order_release);
}

extern "C" __attribute__((visibility("hidden")))
std::uintptr_t GetBuildUnitResume() noexcept {
    return g_build_unit_resume.load(std::memory_order_acquire);
}

extern "C" __attribute__((visibility("hidden")))
std::int32_t HookBuildUnitPostImpl(
    std::int32_t built,
    feh::MapUnitIntermediate* source,
    feh::mod::RuntimeUnit* unit) noexcept {
    if (built != 0) {
        (void)feh::mod::TryApplyMapEngage(
            source,
            unit,
            feh::mod::g_enemy_engage_bindings,
            feh::mod::g_enemy_engage_options);
    }
    return built;
}

// Call this only after libcocos2dcpp.so has been loaded.
//
// Return values:
//   0   installed
//  -1   libcocos2dcpp.so is not loaded
//  -2   hook installer failed or returned no trampoline
//  >0   internal Thumb hook error status
extern "C" __attribute__((visibility("default")))
int InstallFehSrpgMapEnemyEngage(int maximum_limit_break_level) noexcept {
    int expected = 0;
    if (!g_install_state.compare_exchange_strong(expected, 1)) {
        return expected == 2 ? 0 : -3;
    }

    const auto module_base = FindCocosLoadBias();
    if (module_base == 0) {
        g_install_state.store(0);
        return -1;
    }

    const auto result = feh::mod::InstallEnemyEngageHook(
        module_base,
        feh::mod::EnemyEngageOptions{maximum_limit_break_level});
    if (result.installed) {
        g_install_state.store(2);
        return 0;
    }
    g_install_state.store(0);
    return result.hook_status == 0 ? -2 : result.hook_status;
}

// This can be called by a root loader before the game has opened its native
// library. It only observes module state until the existing installer can run;
// it never patches a target that fails the version signature check.
extern "C" __attribute__((visibility("default")))
int ProbeFehSrpgMapEnemyEngageTarget() noexcept {
    const auto module_base = FindCocosLoadBias();
    if (module_base == 0) {
        return -1;
    }
    return feh::mod::verify_build_unit_target(module_base)
               ? 0
               : feh::mod::kEnemyEngageTargetSignatureMismatch;
}

extern "C" __attribute__((visibility("default")))
int GetFehSrpgMapEnemyEngageInstallState() noexcept {
    return g_install_state.load();
}

extern "C" __attribute__((visibility("default")))
JNIEXPORT jint JNICALL
Java_io_github_feh_engageloader_NativeBridge_nativeProbe(
    JNIEnv*,
    jclass) noexcept {
    auto module_base = FindCocosLoadBias();
    g_cocos_load_bias.store(module_base, std::memory_order_release);
    if (module_base == 0) {
        return -1;
    }
    return feh::mod::verify_build_unit_target(module_base)
               ? 0
               : feh::mod::kEnemyEngageTargetSignatureMismatch;
}

extern "C" __attribute__((visibility("default")))
JNIEXPORT jint JNICALL
Java_io_github_feh_engageloader_NativeBridge_nativeProbeFacilities(
    JNIEnv*,
    jclass) noexcept {
    auto module_base = g_cocos_load_bias.load(std::memory_order_acquire);
    if (module_base == 0) {
        module_base = FindCocosLoadBias();
        g_cocos_load_bias.store(module_base, std::memory_order_release);
    }
    if (module_base == 0) {
        return -1;
    }
#if defined(__ANDROID__)
#if defined(FEH_ENABLE_UNSAFE_FACILITY_PROBE)
    ProbeFacilityRuntime(module_base);
#endif
#endif
    return 0;
}

extern "C" __attribute__((visibility("default")))
JNIEXPORT jint JNICALL
Java_io_github_feh_engageloader_NativeBridge_nativeApplyCurrentMap(
    JNIEnv*,
    jclass) noexcept {
    auto module_base = g_cocos_load_bias.load(std::memory_order_acquire);
    if (module_base == 0) {
        module_base = FindCocosLoadBias();
        g_cocos_load_bias.store(module_base, std::memory_order_release);
    }
    if (module_base == 0) {
        return -1;
    }
    if (!feh::mod::verify_build_unit_target(module_base)) {
        return feh::mod::kEnemyEngageTargetSignatureMismatch;
    }

#if 0
    const auto debug_call = g_apply_debug_calls.fetch_add(1) + 1;
    if (debug_call % 10 == 0) {
        auto* map_owner = *reinterpret_cast<std::byte**>(
            module_base + feh::mod::rva::kCurrentMapOwnerGlobal);
        auto* unit_manager = *reinterpret_cast<std::byte**>(
            module_base + feh::mod::rva::kRuntimeUnitManagerGlobal);
        auto* map_mode = *reinterpret_cast<std::byte**>(
            module_base + feh::mod::rva::kMapModeGlobal);
        auto* mode1_owner = *reinterpret_cast<std::byte**>(
            module_base + feh::mod::rva::kMode1ContainerGlobal);
        auto* map_data = *reinterpret_cast<std::byte**>(
            module_base + feh::mod::rva::kMapDataGlobal);
        void* map_root = nullptr;
        void* partition = nullptr;
        void* mode1_container = nullptr;
        void* map_data_begin = nullptr;
        void* map_data_end = nullptr;
        void* map_data_owner = nullptr;
        void* map_data_payload = nullptr;
        void* map_data_source_begin = nullptr;
        std::int32_t source_count = -999;
        std::int32_t map_data_count = -999;
        std::int32_t payload_count_a = -999;
        std::int32_t payload_count_b = -999;
        std::int32_t mode = -999;
        std::int32_t flag_540 = -1;
        std::int32_t flag_556 = -1;
        std::int32_t flag_564 = -1;
        if (map_mode != nullptr && bindings.get_map_side != nullptr) {
            mode = bindings.get_map_side(map_mode + 4);
        }
        if (map_mode != nullptr && bindings.get_map_protected_bool != nullptr) {
            flag_540 = bindings.get_map_protected_bool(
                reinterpret_cast<std::uint8_t*>(map_mode + 540));
            flag_556 = bindings.get_map_protected_bool(
                reinterpret_cast<std::uint8_t*>(map_mode + 556));
            flag_564 = bindings.get_map_protected_bool(
                reinterpret_cast<std::uint8_t*>(map_mode + 564));
        }
        if (map_owner != nullptr && bindings.get_current_map_root != nullptr) {
            map_root = bindings.get_current_map_root(
                map_owner + feh::mod::offset::kMapOwnerCurrentRoot);
        }
        if (map_root != nullptr && bindings.select_current_map_partition != nullptr) {
            partition = bindings.select_current_map_partition(map_root);
        }
        if (partition != nullptr && bindings.engage.get_map_protected_int != nullptr) {
            source_count = bindings.engage.get_map_protected_int(
                reinterpret_cast<feh::RuntimeProtectedScalar32*>(
                    static_cast<std::byte*>(partition)
                    + feh::mod::offset::kMapPartitionSourceCount));
        }
        if (mode1_owner != nullptr) {
            using GetMode1ContainerFn = void* (*)(void*);
            const auto get_mode1_container =
                feh::mod::resolve_game_function<GetMode1ContainerFn>(
                    module_base, feh::mod::rva::kGetMode1Container);
            mode1_container = get_mode1_container(mode1_owner + 168);
        }
        if (map_data != nullptr) {
            std::memcpy(&map_data_owner, map_data + 4, sizeof(map_data_owner));
            if (map_data_owner != nullptr) {
                std::memcpy(
                    &map_data_payload,
                    static_cast<std::byte*>(map_data_owner) + 8,
                    sizeof(map_data_payload));
            }
            std::memcpy(
                &map_data_begin,
                map_data + feh::mod::offset::kMapDataUnitVector,
                sizeof(map_data_begin));
            std::memcpy(
                &map_data_end,
                map_data + feh::mod::offset::kMapDataUnitVectorEnd,
                sizeof(map_data_end));
            if (map_data_begin != nullptr
                && map_data_end >= map_data_begin
                && (static_cast<std::uintptr_t>(
                        reinterpret_cast<std::byte*>(map_data_end)
                        - reinterpret_cast<std::byte*>(map_data_begin))
                    % sizeof(feh::MapUnitIntermediate)) == 0) {
                const auto byte_count = static_cast<std::uintptr_t>(
                    reinterpret_cast<std::byte*>(map_data_end)
                    - reinterpret_cast<std::byte*>(map_data_begin));
                const auto count = byte_count / sizeof(feh::MapUnitIntermediate);
                if (count <= 64) {
                    map_data_count = static_cast<std::int32_t>(count);
                }
            }
            if (map_data_payload != nullptr) {
                std::memcpy(
                    &map_data_source_begin,
                    static_cast<std::byte*>(map_data_payload)
                        + feh::mod::offset::kMapDataPayloadSourceVector,
                    sizeof(map_data_source_begin));
                std::uint32_t raw_a = 0;
                std::uint32_t raw_b = 0;
                std::memcpy(
                    &raw_a,
                    static_cast<std::byte*>(map_data_payload) + 32,
                    sizeof(raw_a));
                std::memcpy(
                    &raw_b,
                    static_cast<std::byte*>(map_data_payload) + 36,
                    sizeof(raw_b));
                payload_count_a = static_cast<std::int32_t>(
                    raw_a ^ 0x9D63C79AU);
                payload_count_b = static_cast<std::int32_t>(
                    raw_b ^ 0xAC6710EEU);
            }
        }
        std::uintptr_t mode1_vtable = 0;
        std::int32_t mode1_word0 = -1;
        std::int32_t mode1_word1 = -1;
        std::int32_t mode1_word2 = -1;
        std::int32_t mode1_count = -1;
        if (mode1_container != nullptr) {
            auto* bytes = static_cast<std::byte*>(mode1_container);
            std::memcpy(&mode1_vtable, bytes, sizeof(mode1_vtable));
            std::memcpy(&mode1_word0, bytes + 0, sizeof(mode1_word0));
            std::memcpy(&mode1_word1, bytes + 4, sizeof(mode1_word1));
            std::memcpy(&mode1_word2, bytes + 8, sizeof(mode1_word2));
            std::memcpy(&mode1_count, bytes + 12, sizeof(mode1_count));
        }
        std::uintptr_t mode1_getter = 0;
        if (mode1_container != nullptr && mode1_vtable != 0) {
            std::memcpy(
                &mode1_getter,
                reinterpret_cast<const std::byte*>(mode1_vtable),
                sizeof(mode1_getter));
        }
        __android_log_print(
            ANDROID_LOG_INFO,
            "feh-engage",
            "apply debug=%d mode=%d flags=%d/%d/%d map_owner=%p unit_manager=%p map_root=%p partition=%p source_count=%d mode1_owner=%p mode1_container=%p mode1_vtable=%p mode1_getter=%p mode1_words=%d/%d/%d count=%d",
            debug_call,
            mode,
            flag_540,
            flag_556,
            flag_564,
            static_cast<void*>(map_owner),
            static_cast<void*>(unit_manager),
            map_root,
            partition,
            source_count,
            static_cast<void*>(mode1_owner),
            mode1_container,
            reinterpret_cast<void*>(mode1_vtable),
            reinterpret_cast<void*>(mode1_getter),
            mode1_word0,
            mode1_word1,
            mode1_word2,
            mode1_count);

        __android_log_print(
            ANDROID_LOG_INFO,
            "feh-engage",
            "mapdata data=%p owner=%p payload=%p source=%p begin=%p end=%p count=%d payload_counts=%d/%d",
            static_cast<void*>(map_data),
            map_data_owner,
            map_data_payload,
            map_data_source_begin,
            map_data_begin,
            map_data_end,
            map_data_count,
            payload_count_a,
            payload_count_b);
        if (map_data_source_begin != nullptr
            && payload_count_b > 0
            && payload_count_b <= 64
            && bindings.init_map_intermediate != nullptr
            && bindings.fill_map_intermediate != nullptr
            && bindings.destroy_map_intermediate != nullptr) {
            auto* descriptor_bytes = reinterpret_cast<std::byte*>(
                map_data_source_begin);
            const auto log_count = payload_count_b < 8 ? payload_count_b : 8;
            for (std::int32_t index = 0; index < log_count; ++index) {
                feh::MapUnitIntermediate converted{};
                bindings.init_map_intermediate(&converted);
                bindings.fill_map_intermediate(
                    &converted,
                    descriptor_bytes + static_cast<std::size_t>(index)
                        * feh::mod::offset::kMapDataDescriptorStride,
                    0,
                    0);
                auto* converted_main = bindings.engage.get_map_reference_person(
                    reinterpret_cast<feh::RuntimeProtectedPointer24*>(&converted));
                auto* converted_engage = bindings.engage.get_map_reference_person(
                    &converted.reference_person);
                const auto converted_quota = bindings.engage.get_map_protected_int(
                    &converted.spawn_quota);
                const auto converted_level = bindings.engage.get_map_protected_int(
                    &converted.not_before_turn);
                __android_log_print(
                    ANDROID_LOG_INFO,
                    "feh-engage",
                    "converted[%d] main=%p engage=%p quota=%d level=%d engage_hero=%d",
                    index,
                    static_cast<void*>(converted_main),
                    static_cast<void*>(converted_engage),
                    converted_quota,
                    converted_level,
                    feh::mod::is_engage_hero(converted_engage) ? 1 : 0);
                bindings.destroy_map_intermediate(&converted);
            }
        }
        if (map_data_count > 0 && bindings.engage.get_map_protected_int != nullptr) {
            auto* records = reinterpret_cast<std::byte*>(map_data_begin);
            const auto log_count = map_data_count < 8 ? map_data_count : 8;
            for (std::int32_t index = 0; index < log_count; ++index) {
                auto* source = reinterpret_cast<feh::MapUnitIntermediate*>(
                    records + static_cast<std::size_t>(index)
                        * sizeof(feh::MapUnitIntermediate));
                auto* main_person = bindings.engage.get_map_reference_person(
                    reinterpret_cast<feh::RuntimeProtectedPointer24*>(source));
                auto* engage_person = bindings.engage.get_map_reference_person(
                    &source->reference_person);
                const auto quota = bindings.engage.get_map_protected_int(
                    &source->spawn_quota);
                const auto level = bindings.engage.get_map_protected_int(
                    &source->not_before_turn);
                __android_log_print(
                    ANDROID_LOG_INFO,
                    "feh-engage",
                    "mapdata[%d] source=%p main=%p engage=%p quota=%d level=%d engage_hero=%d",
                    index,
                    static_cast<void*>(source),
                    static_cast<void*>(main_person),
                    static_cast<void*>(engage_person),
                    quota,
                    level,
                    feh::mod::is_engage_hero(engage_person) ? 1 : 0);
            }
        }

        const auto expected_getter =
            (module_base + feh::mod::rva::kMode1ContainerGetElement) | 1U;
        if (mode1_container != nullptr
            && mode1_count > 0
            && mode1_count <= 16
            && mode1_getter == expected_getter) {
            using GetElementFn = void* (*)(void*, std::int32_t);
            using GetUnitIntFn = std::int32_t (*)(void*);
            const auto get_element = reinterpret_cast<GetElementFn>(mode1_getter);
            const auto get_unit_int =
                feh::mod::resolve_game_function<GetUnitIntFn>(
                    module_base, feh::mod::rva::kGetUnitProtectedInt);
            for (std::int32_t index = 0; index < mode1_count; ++index) {
                auto* element = get_element(mode1_container, index);
                void* main_person = nullptr;
                void* engage_person = nullptr;
                std::int32_t engage_level = -999;
                if (element != nullptr) {
                    auto* element_bytes = static_cast<std::byte*>(element);
                    main_person = bindings.engage.get_unit_protected_pointer(
                        element_bytes + feh::mod::offset::kUnitMainPerson);
                    engage_person = bindings.engage.get_unit_protected_pointer(
                        element_bytes + feh::mod::offset::kUnitEngagedPerson);
                    engage_level = get_unit_int(
                        element_bytes + feh::mod::offset::kUnitEngageLimitBreak);
                }
                __android_log_print(
                    ANDROID_LOG_INFO,
                    "feh-engage",
                    "mode1[%d] element=%p main=%p engage=%p level=%d engage_hero=%d",
                    index,
                    element,
                    main_person,
                    engage_person,
                    engage_level,
                    feh::mod::is_engage_hero(
                        static_cast<feh::mod::PersonDefinition*>(engage_person))
                    ? 1
                        : 0);
            }
            auto* unit_manager_debug = *reinterpret_cast<std::byte**>(
                module_base + feh::mod::rva::kRuntimeUnitManagerGlobal);
            if (unit_manager_debug != nullptr) {
                using GetUnitIntDebugFn = std::int32_t (*)(void*);
                const auto get_unit_int_debug =
                    feh::mod::resolve_game_function<GetUnitIntDebugFn>(
                        module_base, feh::mod::rva::kGetUnitProtectedInt);
                for (std::size_t side = 0;
                     side < feh::mod::offset::kRuntimeUnitRosterCount;
                     ++side) {
                    auto* roster = unit_manager_debug
                        + feh::mod::offset::kRuntimeUnitRosterBase
                        + side * feh::mod::offset::kRuntimeUnitRosterStride;
                    feh::mod::RuntimeUnit** begin = nullptr;
                    feh::mod::RuntimeUnit** end = nullptr;
                    std::memcpy(&begin, roster, sizeof(begin));
                    std::memcpy(&end, roster + sizeof(begin), sizeof(end));
                    const auto count = (begin != nullptr && end >= begin)
                        ? static_cast<std::size_t>(end - begin) : 0;
                    __android_log_print(
                        ANDROID_LOG_INFO, "feh-engage",
                        "roster[%zu] begin=%p end=%p count=%zu",
                        side, begin, end, count);
                    const auto limit = count < 16 ? count : 16;
                    for (std::size_t i = 0; i < limit; ++i) {
                        auto* runtime_unit = begin[i];
                        void* runtime_main = nullptr;
                        void* runtime_engage = nullptr;
                        std::int32_t runtime_level = -999;
                        if (runtime_unit != nullptr) {
                            auto* runtime_bytes = reinterpret_cast<std::byte*>(
                                runtime_unit);
                            runtime_main = bindings.engage.get_unit_protected_pointer(
                                runtime_bytes + feh::mod::offset::kUnitMainPerson);
                            runtime_engage = bindings.engage.get_unit_protected_pointer(
                                runtime_bytes + feh::mod::offset::kUnitEngagedPerson);
                            runtime_level = get_unit_int_debug(
                                runtime_bytes + feh::mod::offset::kUnitEngageLimitBreak);
                        }
                        __android_log_print(
                            ANDROID_LOG_INFO, "feh-engage",
                            "roster[%zu][%zu] unit=%p main=%p engage=%p level=%d engage_hero=%d",
                            side, i, runtime_unit, runtime_main, runtime_engage,
                            runtime_level,
                            feh::mod::is_engage_hero(
                                static_cast<feh::mod::PersonDefinition*>(runtime_engage))
                                ? 1 : 0);
                    }
                }
            }

            // PersonDefinition instances are duplicated by the map/template
            // loaders. Dump a small read-only fingerprint so the matching key
            // can be identified without dereferencing guessed fields in the
            // apply path.
            auto log_person_fingerprint = [](const char* label, void* person) {
                if (person == nullptr) {
                    __android_log_print(
                        ANDROID_LOG_INFO, "feh-engage",
                        "person[%s]=null", label);
                    return;
                }
                std::uint32_t words[16]{};
                std::memcpy(words, person, sizeof(words));
                __android_log_print(
                    ANDROID_LOG_INFO, "feh-engage",
                    "person[%s] ptr=%p w0=%08x w1=%08x w2=%08x w3=%08x w4=%08x w5=%08x w6=%08x w7=%08x",
                    label, person, words[0], words[1], words[2], words[3],
                    words[4], words[5], words[6], words[7]);
                __android_log_print(
                    ANDROID_LOG_INFO, "feh-engage",
                    "person[%s] w8=%08x w9=%08x w10=%08x w11=%08x w12=%08x w13=%08x w14=%08x w15=%08x",
                    label, words[8], words[9], words[10], words[11],
                    words[12], words[13], words[14], words[15]);
            };
            if (map_data_source_begin != nullptr
                && payload_count_b > 0
                && payload_count_b <= 64) {
                feh::MapUnitIntermediate converted{};
                bindings.init_map_intermediate(&converted);
                bindings.fill_map_intermediate(
                    &converted,
                    reinterpret_cast<std::byte*>(map_data_source_begin),
                    0,
                    0);
                auto* source_main = bindings.engage.get_map_reference_person(
                    reinterpret_cast<feh::RuntimeProtectedPointer24*>(&converted));
                log_person_fingerprint("source0", source_main);
                bindings.destroy_map_intermediate(&converted);
            }
            for (std::int32_t index = 0; index < mode1_count && index < 2; ++index) {
                auto* element = get_element(mode1_container, index);
                if (element != nullptr) {
                    auto* element_bytes = static_cast<std::byte*>(element);
                    auto* template_main = bindings.engage.get_unit_protected_pointer(
                        element_bytes + feh::mod::offset::kUnitMainPerson);
                    char label[16]{};
                    std::snprintf(label, sizeof(label), "template%d", index);
                    log_person_fingerprint(label, template_main);
                }
            }
        }
    }
#endif
    const auto cfg_bindings = feh::mod::make_external_cfg_bindings(module_base);
    const auto report = feh::mod::ApplyExternalCfgEngage(
        module_base,
        cfg_bindings,
        feh::mod::EnemyEngageOptions{kDefaultMaximumLimitBreakLevel});
    const auto facility_report =
        feh::mod::ApplyExternalCfgFacilities(module_base);
#if defined(__ANDROID__)
    static std::uint32_t last_facility_bridge_report = 0xFFFFFFFFU;
    const auto packed_facility_report =
        static_cast<std::uint32_t>(facility_report.status)
        | (static_cast<std::uint32_t>(facility_report.configured) << 8U)
        | (static_cast<std::uint32_t>(facility_report.attached) << 20U);
    if (packed_facility_report != last_facility_bridge_report) {
        last_facility_bridge_report = packed_facility_report;
        __android_log_print(
            ANDROID_LOG_INFO,
            "feh-engage",
            "facility bridge status=%u configured=%u attached=%u",
            static_cast<unsigned>(facility_report.status),
            static_cast<unsigned>(facility_report.configured),
            static_cast<unsigned>(facility_report.attached));
    }
#endif
    const auto status = static_cast<std::uint32_t>(report.status);
    return static_cast<jint>(
        status
        | (static_cast<std::uint32_t>(report.configured & 0xFFU) << 8U)
        | (static_cast<std::uint32_t>(report.applied & 0xFFU) << 16U)
        | (static_cast<std::uint32_t>(report.unmatched & 0x7FU) << 24U));
}

#if defined(__ANDROID__)
void* DelayedInstallThread(void*) noexcept {
    for (int attempt = 0; attempt < kInstallRetryCount; ++attempt) {
        const auto status = InstallFehSrpgMapEnemyEngage(
            kDefaultMaximumLimitBreakLevel);
        if (status == 0) {
            LogInstallStatus("installed", status);
            return nullptr;
        }
        if (status != -1 && status != -3) {
            LogInstallStatus("stopped", status);
            return nullptr;
        }
        usleep(kInstallRetryDelayMs * 1000);
    }
    LogInstallStatus("timed-out", -1);
    return nullptr;
}

void StartDelayedInstall() noexcept {
    bool expected = false;
    if (!g_delayed_install_started.compare_exchange_strong(expected, true)) {
        return;
    }

    pthread_t thread{};
    if (pthread_create(&thread, nullptr, &DelayedInstallThread, nullptr) != 0) {
        g_delayed_install_started.store(false);
        LogInstallStatus("thread-create-failed", -4);
        return;
    }
    (void)pthread_detach(thread);
}
#endif

// APK repackaging and dlopen-based loaders generally load this module after
// libcocos2dcpp.so. In that normal order, install automatically. The exported
// function remains available for loaders that need to control the timing.
extern "C" __attribute__((constructor))
void FehSrpgMapEnemyEngageOnLoad() noexcept {
#if defined(__ANDROID__)
    // Native Bridge can execute this payload, but redirecting FEH's ARM guest
    // control flow crashes Houdini. LSPosed invokes the JNI post-build scanner
    // instead; the legacy installer remains exported for real ARM devices.
    LogInstallStatus("loaded-passive", 0);
#else
    LogInstallStatus("loaded-passive", 0);
#endif
}
