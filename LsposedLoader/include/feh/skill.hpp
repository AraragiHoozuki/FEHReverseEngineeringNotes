#pragma once

#include "feh/hsdarc.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace feh {

static_assert(
    std::endian::native == std::endian::little,
    "SkillDefinition overlays require the little-endian FEH archive byte order");

template <typename UInt, UInt Key>
struct XorValue {
    static_assert(std::is_unsigned_v<UInt>);

    UInt encoded;

    static constexpr UInt key = Key;

    [[nodiscard]] constexpr UInt value() const noexcept {
        return encoded ^ key;
    }

    constexpr void set(UInt decoded) noexcept {
        encoded = decoded ^ key;
    }
};

template <std::uint16_t Key>
struct XorInt16 {
    std::uint16_t encoded;

    static constexpr std::uint16_t key = Key;

    [[nodiscard]] constexpr std::int16_t value() const noexcept {
        return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(encoded ^ key));
    }

    constexpr void set(std::int16_t decoded) noexcept {
        encoded = std::bit_cast<std::uint16_t>(decoded) ^ key;
    }
};

static_assert(sizeof(XorValue<std::uint32_t, 0>) == 4);
static_assert(sizeof(XorInt16<0>) == 2);

enum class StringCipher {
    id,
    none,
};

// The archive stores only the pointer here. String decoding, when required by
// the selected cipher, is separate from HSDArc pointer relocation.
template <StringCipher Cipher>
struct CryptString {
    hsdarc::Pointer<const char> pointer;

    static constexpr StringCipher cipher = Cipher;
};

using IdString = CryptString<StringCipher::id>;
using PlainString = CryptString<StringCipher::none>;

static_assert(sizeof(IdString) == 8);
static_assert(sizeof(PlainString) == 8);

// Seven consecutive tuples occupy SkillDefinition+0x068..+0x0D7. The first
// five columns and their XOR keys are directly decoded by the SO. The final
// three columns have no located consumer and remain raw.
struct SkillStatsTuple {
    XorInt16<0xD632> hp;
    XorInt16<0x14A0> atk;
    XorInt16<0xA55E> spd;
    XorInt16<0x8566> def;
    XorInt16<0xAEE5> res;
    std::uint16_t unknown_0A;
    std::uint16_t unknown_0C;
    std::uint16_t unknown_0E;
};

static_assert(sizeof(SkillStatsTuple) == 0x10);
static_assert(offsetof(SkillStatsTuple, hp) == 0x00);
static_assert(offsetof(SkillStatsTuple, res) == 0x08);

struct SkillDefinition {
    IdString id_tag;                         // +0x000, manager string key
    IdString refine_base;                    // +0x008, provisional semantic name
    IdString name_id;                        // +0x010, localization key
    IdString desc_id;                        // +0x018, localization key; _NOSKW fallback
    IdString refine_id;                      // +0x020, resolves to Skill slot 8
    IdString beast_effect_id;                // +0x028, resolves to conditional slot 9
    std::array<IdString, 2> prerequisites;   // +0x030, resolved by sub_2E3B150
    IdString next_skill;                     // +0x040, provisional semantic name
    std::array<PlainString, 4> sprites;      // +0x048, first two used by rendering code

    SkillStatsTuple stats;                   // +0x068
    SkillStatsTuple class_params;            // +0x078, provisional row name
    SkillStatsTuple combat_buffs;            // +0x088, provisional row name
    SkillStatsTuple skill_params;            // +0x098, provisional row name
    SkillStatsTuple skill_params2;           // +0x0A8, provisional row name
    SkillStatsTuple skill_params3;           // +0x0B8, third skill parameter row
    SkillStatsTuple refine_stats;            // +0x0C8, only refinement-stat row

    XorValue<std::uint32_t, 0xC6A53A23> id_num;      // +0x0D8
    XorValue<std::uint32_t, 0x8DDBF8AC> sort_id;     // +0x0DC
    XorValue<std::uint32_t, 0xC6DF2173> icon_id;     // +0x0E0
    XorValue<std::uint32_t, 0x35B99828> wep_equip;   // +0x0E4
    XorValue<std::uint32_t, 0xAB2818EB> mov_equip;   // +0x0E8
    XorValue<std::uint32_t, 0xC031F669> sp_cost;     // +0x0EC

    XorValue<std::uint8_t, 0xBC> category;           // +0x0F0, decoded range 0..6
    std::uint8_t field_0F1;
    std::uint8_t field_0F2;
    std::uint8_t field_0F3;
    std::array<std::byte, 2> field_0F4;
    XorValue<std::uint8_t, 0x56> field_0F6;
    std::array<std::byte, 2> field_0F7;
    XorValue<std::uint8_t, 0x09> field_0F9;
    std::array<std::byte, 4> field_0FA;
    std::uint8_t field_0FE;
    XorValue<std::uint8_t, 0xFC> field_0FF;
    std::array<std::byte, 8> field_100;

    XorValue<std::uint32_t, 0x554548BC> field_108;
    XorValue<std::uint32_t, 0xF1410DA4> field_10C;
    XorValue<std::uint32_t, 0x005A02AF> field_110;
    XorValue<std::uint32_t, 0xB269B819> field_114;
    XorValue<std::uint32_t, 0x647F9ECD> field_118;
    XorValue<std::uint32_t, 0xB7064176> field_11C;
    XorValue<std::uint32_t, 0x494E2629> field_120;
    XorValue<std::uint32_t, 0xEE6CEF2E> field_124;
    XorValue<std::uint16_t, 0x029C> field_128;
    XorValue<std::uint16_t, 0x68F6> field_12A;
    XorValue<std::uint16_t, 0x0D1E> field_12C;
    std::array<std::byte, 2> field_12E;
    XorValue<std::uint8_t, 0x1D> field_130;
    XorValue<std::uint8_t, 0x99> field_131;
    XorValue<std::uint8_t, 0x28> field_132;
    XorValue<std::uint8_t, 0x31> field_133;
    XorValue<std::uint16_t, 0x544C> field_134;
    std::array<std::byte, 2> field_136;

    std::uint32_t field_138;
    // Numeric reference to SkillAbilityDefinition::id_num.  The runtime also
    // compares the two encoded values directly with XOR 0xD6B79C15.
    XorValue<std::uint32_t, 0x72B07325> ability_id;
    std::uint32_t field_140;
    XorValue<std::uint16_t, 0xA590> field_144;
    XorValue<std::uint16_t, 0xA590> field_146;
    std::uint32_t field_148;
    XorValue<std::uint16_t, 0xA590> field_14C;
    XorValue<std::uint16_t, 0xA590> field_14E;
    XorValue<std::uint32_t, 0x409FC9D7> field_150;
    XorValue<std::uint32_t, 0x6C64D122> field_154;
    hsdarc::Pointer<const std::byte> pointer_158;
    XorValue<std::uint64_t, 0xED3F39F93BFE9F51> field_160;
    XorValue<std::uint8_t, 0x10> selection_weight;
    XorValue<std::uint8_t, 0x90> field_169;
    XorValue<std::uint8_t, 0x24> field_16A;
    std::uint8_t field_16B;
    std::uint8_t field_16C;
    std::array<std::byte, 3> field_16D;
    std::uint32_t field_170;
    XorValue<std::uint16_t, 0xA590> field_174;
    XorValue<std::uint16_t, 0xA590> field_176;
    XorValue<std::uint8_t, 0x5C> field_178;
    std::uint8_t field_179;
    XorValue<std::uint8_t, 0x41> field_17A;
    std::uint8_t field_17B;
    std::uint8_t field_17C;
    std::array<std::byte, 2> field_17D;
    std::uint8_t field_17F;
    std::uint32_t flags_180;
    std::array<std::byte, 4> field_184;
};

static_assert(std::is_standard_layout_v<SkillDefinition>);
static_assert(std::is_trivially_copyable_v<SkillDefinition>);
static_assert(sizeof(SkillDefinition) == 0x188);
static_assert(alignof(SkillDefinition) == 8);
static_assert(offsetof(SkillDefinition, prerequisites) == 0x030);
static_assert(offsetof(SkillDefinition, sprites) == 0x048);
static_assert(offsetof(SkillDefinition, stats) == 0x068);
static_assert(offsetof(SkillDefinition, class_params) == 0x078);
static_assert(offsetof(SkillDefinition, combat_buffs) == 0x088);
static_assert(offsetof(SkillDefinition, skill_params) == 0x098);
static_assert(offsetof(SkillDefinition, skill_params2) == 0x0A8);
static_assert(offsetof(SkillDefinition, skill_params3) == 0x0B8);
static_assert(offsetof(SkillDefinition, refine_stats) == 0x0C8);
static_assert(offsetof(SkillDefinition, id_num) == 0x0D8);
static_assert(offsetof(SkillDefinition, category) == 0x0F0);
static_assert(offsetof(SkillDefinition, field_108) == 0x108);
static_assert(offsetof(SkillDefinition, pointer_158) == 0x158);
static_assert(offsetof(SkillDefinition, field_160) == 0x160);
static_assert(offsetof(SkillDefinition, selection_weight) == 0x168);
static_assert(offsetof(SkillDefinition, flags_180) == 0x180);
static_assert(offsetof(SkillDefinition, ability_id) == 0x13C);

struct SkillAbilityDefinition {
    IdString id_tag;                                  // +0x00, SAID_* lookup key
    XorValue<std::uint32_t, 0xA407EF30> id_num;       // +0x08
    std::uint32_t padding;                            // +0x0C
};

static_assert(sizeof(SkillAbilityDefinition) == 0x10);
static_assert(offsetof(SkillAbilityDefinition, id_num) == 0x08);

struct SkillAbilityArchiveRoot {
    hsdarc::Pointer<SkillAbilityDefinition> records;  // +0x00
    XorValue<std::uint32_t, 0x22CF90AB> record_count; // +0x08
    std::uint32_t padding;                            // +0x0C
};

static_assert(sizeof(SkillAbilityArchiveRoot) == 0x10);

struct SkillArchiveRoot {
    hsdarc::Pointer<SkillDefinition> records;             // +0x00
    XorValue<std::uint32_t, 0x4ADEE9AD> record_count;     // +0x08
    std::uint32_t field_0C;
};

static_assert(sizeof(SkillArchiveRoot) == 0x10);
static_assert(offsetof(SkillArchiveRoot, record_count) == 0x08);

// Portable read-only access to an unrelocated Skill HSDArc image. Pointer
// fields in copied records remain relative to data_base().
class SkillArchiveView {
public:
    explicit SkillArchiveView(std::span<const std::byte> archive)
        : archive_(archive), root_(archive_.copy_data_object<SkillArchiveRoot>(0)) {
        const auto count = static_cast<std::size_t>(root_.record_count.value());
        if (count > (archive_.data_extent() - checked_records_offset())
                        / sizeof(SkillDefinition)) {
            throw hsdarc::Error("Skill record array exceeds the HSDArc data section");
        }
    }

    [[nodiscard]] const hsdarc::View& hsdarc_view() const noexcept { return archive_; }
    [[nodiscard]] const SkillArchiveRoot& root() const noexcept { return root_; }
    [[nodiscard]] std::size_t size() const noexcept { return root_.record_count.value(); }
    [[nodiscard]] const std::byte* data_base() const noexcept { return archive_.data().data(); }

    [[nodiscard]] SkillDefinition record(std::size_t index) const {
        if (index >= size()) {
            throw hsdarc::Error("Skill record index is out of range");
        }
        return archive_.copy_data_object<SkillDefinition>(
            checked_records_offset() + index * sizeof(SkillDefinition));
    }

    [[nodiscard]] const SkillDefinition* records_overlay() const {
        return archive_.data_object_at<SkillDefinition>(checked_records_offset(), size());
    }

    template <StringCipher Cipher>
    [[nodiscard]] const char* resolve(const CryptString<Cipher>& string) const {
        const auto offset = string.pointer.relative_offset();
        if (offset == 0) {
            return nullptr;
        }
        if (offset >= archive_.data_extent()) {
            throw hsdarc::Error("Skill string pointer exceeds the HSDArc data section");
        }
        return reinterpret_cast<const char*>(data_base() + offset);
    }

private:
    [[nodiscard]] std::size_t checked_records_offset() const {
        const auto offset = root_.records.relative_offset();
        if (offset > archive_.data_extent()) {
            throw hsdarc::Error("Skill record pointer exceeds the HSDArc data section");
        }
        return static_cast<std::size_t>(offset);
    }

    hsdarc::View archive_;
    SkillArchiveRoot root_;
};

}  // namespace feh
