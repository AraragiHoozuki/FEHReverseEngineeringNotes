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
    "SRPGMap overlays require the little-endian FEH archive byte order");

template <std::uint32_t Key>
struct SrpgMapXorU32 {
    std::uint32_t encoded;

    static constexpr std::uint32_t key = Key;

    [[nodiscard]] constexpr std::uint32_t value() const noexcept {
        return encoded ^ key;
    }

    constexpr void set(std::uint32_t decoded) noexcept {
        encoded = decoded ^ key;
    }
};

template <std::uint8_t Key>
struct SrpgMapXorS8 {
    std::uint8_t encoded;

    static constexpr std::uint8_t key = Key;

    [[nodiscard]] constexpr std::int8_t value() const noexcept {
        return std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(encoded ^ key));
    }

    constexpr void set(std::int8_t decoded) noexcept {
        encoded = std::bit_cast<std::uint8_t>(decoded) ^ key;
    }
};

static_assert(sizeof(SrpgMapXorU32<0>) == 4);
static_assert(sizeof(SrpgMapXorS8<0>) == 1);

// Only the tail is semantically named. The first 0x70 bytes remain opaque
// until their consumers have been proved independently from the SO.
struct SrpgMapUnitRecord {
    std::array<std::byte, 0x70> opaque_000;
    hsdarc::Pointer<const char> reference_id;              // +0x70, P* or E*
    SrpgMapXorS8<0x0A> spawn_quota;                        // +0x78
    SrpgMapXorS8<0x0A> not_before_turn;                    // +0x79, provisional name
    SrpgMapXorS8<0x2D> side1_match_count_ceiling;          // +0x7A
    SrpgMapXorS8<0x5B> side3_match_count_floor;            // +0x7B
    std::array<std::byte, 4> opaque_07C;
};

static_assert(std::is_standard_layout_v<SrpgMapUnitRecord>);
static_assert(std::is_trivially_copyable_v<SrpgMapUnitRecord>);
static_assert(sizeof(SrpgMapUnitRecord) == 0x80);
static_assert(alignof(SrpgMapUnitRecord) == 8);
static_assert(offsetof(SrpgMapUnitRecord, reference_id) == 0x70);
static_assert(offsetof(SrpgMapUnitRecord, spawn_quota) == 0x78);
static_assert(offsetof(SrpgMapUnitRecord, not_before_turn) == 0x79);
static_assert(offsetof(SrpgMapUnitRecord, side1_match_count_ceiling) == 0x7A);
static_assert(offsetof(SrpgMapUnitRecord, side3_match_count_floor) == 0x7B);

// The SRPGMap root has other tables before the unit table. Only the prefix
// through the confirmed unit count is represented here.
struct SrpgMapArchiveRootPrefix {
    std::array<std::byte, 0x18> opaque_000;
    hsdarc::Pointer<SrpgMapUnitRecord> unit_records;        // +0x18
    std::uint32_t field_20;
    SrpgMapXorU32<0xAC6710EE> unit_count;                  // +0x24
};

static_assert(sizeof(SrpgMapArchiveRootPrefix) == 0x28);
static_assert(offsetof(SrpgMapArchiveRootPrefix, unit_records) == 0x18);
static_assert(offsetof(SrpgMapArchiveRootPrefix, unit_count) == 0x24);

// Runtime scalar created by sub_2E30B68. Its layout is 12 bytes and is not the
// 48-byte Unit protected integer used by sub_28ED7D4/sub_28EEE58.
struct RuntimeProtectedScalar32 {
    std::uint32_t encoded;
    std::uint32_t key;
    std::uint32_t checksum;

    [[nodiscard]] constexpr std::int32_t value_unchecked() const noexcept {
        return std::bit_cast<std::int32_t>(encoded ^ key);
    }
};

static_assert(sizeof(RuntimeProtectedScalar32) == 0x0C);
static_assert(offsetof(RuntimeProtectedScalar32, key) == 0x04);
static_assert(offsetof(RuntimeProtectedScalar32, checksum) == 0x08);

// The SRPGMap intermediate object uses this 24-byte protected pointer form.
// Keep the internals opaque and use the game's getter so its checksum is
// verified.
struct alignas(8) RuntimeProtectedPointer24 {
    std::array<std::byte, 0x18> storage;
};

static_assert(sizeof(RuntimeProtectedPointer24) == 0x18);
static_assert(alignof(RuntimeProtectedPointer24) == 8);

// ARM32 10.8.0: sub_153CF10 constructs 0x260 bytes and the map containers
// advance by that stride.  sub_159D19C maps record+0x70/+0x78..+0x7B into the
// fields named below.  Only fields needed by reinforcement selection and the
// enemy-Engage protocol are exposed.
struct MapUnitIntermediate {
    std::array<std::byte, 0x1D8> opaque_000;
    RuntimeProtectedScalar32 spawn_quota;                  // +0x1D8
    RuntimeProtectedScalar32 not_before_turn;              // +0x1E4
    RuntimeProtectedScalar32 side1_match_count_ceiling;    // +0x1F0
    RuntimeProtectedScalar32 side3_match_count_floor;      // +0x1FC
    RuntimeProtectedPointer24 reference_person;            // +0x208
    RuntimeProtectedPointer24 reference_enemy;             // +0x220
    RuntimeProtectedScalar32 generated_count;              // +0x238
    RuntimeProtectedScalar32 field_244;
    std::array<std::byte, 0x10> opaque_250;
};

static_assert(std::is_standard_layout_v<MapUnitIntermediate>);
static_assert(sizeof(MapUnitIntermediate) == 0x260);
static_assert(alignof(MapUnitIntermediate) == 8);
static_assert(offsetof(MapUnitIntermediate, spawn_quota) == 0x1D8);
static_assert(offsetof(MapUnitIntermediate, not_before_turn) == 0x1E4);
static_assert(offsetof(MapUnitIntermediate, side1_match_count_ceiling) == 0x1F0);
static_assert(offsetof(MapUnitIntermediate, side3_match_count_floor) == 0x1FC);
static_assert(offsetof(MapUnitIntermediate, reference_person) == 0x208);
static_assert(offsetof(MapUnitIntermediate, reference_enemy) == 0x220);
static_assert(offsetof(MapUnitIntermediate, generated_count) == 0x238);
static_assert(offsetof(MapUnitIntermediate, field_244) == 0x244);

// Read-only access to an unrelocated SRPGMap HSDArc image. Pointer values in
// copied records remain relative to data_base().
class SrpgMapArchiveView {
public:
    explicit SrpgMapArchiveView(std::span<const std::byte> archive)
        : archive_(archive),
          root_(archive_.copy_data_object<SrpgMapArchiveRootPrefix>(0)) {
        const auto count = static_cast<std::size_t>(root_.unit_count.value());
        const auto records_offset = checked_records_offset();
        if (count > (archive_.data_extent() - records_offset)
                        / sizeof(SrpgMapUnitRecord)) {
            throw hsdarc::Error("SRPGMap unit array exceeds the HSDArc data section");
        }
    }

    [[nodiscard]] const hsdarc::View& hsdarc_view() const noexcept { return archive_; }
    [[nodiscard]] const SrpgMapArchiveRootPrefix& root() const noexcept { return root_; }
    [[nodiscard]] std::size_t size() const noexcept { return root_.unit_count.value(); }
    [[nodiscard]] const std::byte* data_base() const noexcept { return archive_.data().data(); }

    [[nodiscard]] SrpgMapUnitRecord record(std::size_t index) const {
        if (index >= size()) {
            throw hsdarc::Error("SRPGMap unit index is out of range");
        }
        return archive_.copy_data_object<SrpgMapUnitRecord>(
            checked_records_offset() + index * sizeof(SrpgMapUnitRecord));
    }

    [[nodiscard]] const SrpgMapUnitRecord* records_overlay() const {
        return archive_.data_object_at<SrpgMapUnitRecord>(checked_records_offset(), size());
    }

    [[nodiscard]] const char* resolve_reference_id(const SrpgMapUnitRecord& record) const {
        const auto offset = record.reference_id.relative_offset();
        if (offset == 0) {
            return nullptr;
        }
        const auto body_extent = archive_.archive().size() - hsdarc::kHeaderSize;
        if (offset >= body_extent) {
            throw hsdarc::Error("SRPGMap reference ID exceeds the HSDArc body");
        }
        return reinterpret_cast<const char*>(data_base() + offset);
    }

private:
    [[nodiscard]] std::size_t checked_records_offset() const {
        const auto offset = root_.unit_records.relative_offset();
        if (offset > archive_.data_extent()) {
            throw hsdarc::Error("SRPGMap unit pointer exceeds the HSDArc data section");
        }
        return static_cast<std::size_t>(offset);
    }

    hsdarc::View archive_;
    SrpgMapArchiveRootPrefix root_;
};

}  // namespace feh
