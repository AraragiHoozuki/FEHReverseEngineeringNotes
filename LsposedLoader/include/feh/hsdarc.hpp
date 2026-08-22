#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace feh::hsdarc {

inline constexpr std::size_t kHeaderSize = 0x20;
inline constexpr std::size_t kRelocationEntrySize = 8;

// Header layout observed by libcocos2dcpp.so:sub_3BF7E0C.
struct Header {
    std::uint32_t file_size;
    std::uint32_t data_size;
    std::uint32_t relocation_count;
    std::uint32_t public_count;
    std::uint32_t external_count;
    std::uint32_t version;
    std::array<std::byte, 8> reserved;
};

static_assert(sizeof(Header) == kHeaderSize);
static_assert(offsetof(Header, data_size) == 0x04);
static_assert(offsetof(Header, relocation_count) == 0x08);
static_assert(offsetof(Header, public_count) == 0x0C);
static_assert(offsetof(Header, external_count) == 0x10);
static_assert(offsetof(Header, version) == 0x14);

class Error final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Relocation {
    std::uint64_t raw;

    [[nodiscard]] constexpr std::uint64_t slot_offset() const noexcept {
        return raw & ~std::uint64_t{7};
    }

    [[nodiscard]] constexpr std::uint8_t flags() const noexcept {
        return static_cast<std::uint8_t>(raw & 7);
    }
};

// An HSDArc pointer is a data-section-relative offset before relocation and an
// absolute native address after sub_3BF7E0C-style relocation.
template <typename T>
struct Pointer {
    static_assert(std::is_object_v<std::remove_const_t<T>>);

    std::uint64_t value;

    [[nodiscard]] constexpr bool empty() const noexcept { return value == 0; }
    [[nodiscard]] constexpr std::uint64_t relative_offset() const noexcept { return value; }

    [[nodiscard]] T* resolve_relative(std::byte* data_base) const noexcept {
        if (empty()) {
            return nullptr;
        }
        return reinterpret_cast<T*>(data_base + value);
    }

    [[nodiscard]] const T* resolve_relative(const std::byte* data_base) const noexcept {
        if (empty()) {
            return nullptr;
        }
        return reinterpret_cast<const T*>(data_base + value);
    }

    [[nodiscard]] T* resolve_relocated() const noexcept {
        return reinterpret_cast<T*>(static_cast<std::uintptr_t>(value));
    }
};

static_assert(sizeof(Pointer<std::byte>) == 8);
static_assert(std::is_trivially_copyable_v<Pointer<std::byte>>);

namespace detail {

template <typename UInt>
[[nodiscard]] UInt read_le(std::span<const std::byte> bytes, std::size_t offset) {
    static_assert(std::is_unsigned_v<UInt>);
    if (offset > bytes.size() || sizeof(UInt) > bytes.size() - offset) {
        throw Error("HSDArc read exceeds the archive");
    }

    UInt result = 0;
    for (std::size_t index = 0; index < sizeof(UInt); ++index) {
        result |= static_cast<UInt>(std::to_integer<unsigned int>(bytes[offset + index]))
                  << (index * 8);
    }
    return result;
}

template <typename UInt>
void write_le(std::span<std::byte> bytes, std::size_t offset, UInt value) {
    static_assert(std::is_unsigned_v<UInt>);
    if (offset > bytes.size() || sizeof(UInt) > bytes.size() - offset) {
        throw Error("HSDArc write exceeds the archive");
    }

    for (std::size_t index = 0; index < sizeof(UInt); ++index) {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8)) & 0xFF);
    }
}

[[nodiscard]] inline Header read_header(std::span<const std::byte> bytes) {
    if (bytes.size() < kHeaderSize) {
        throw Error("file is shorter than the 0x20-byte HSDArc header");
    }

    Header header{};
    header.file_size = read_le<std::uint32_t>(bytes, 0x00);
    header.data_size = read_le<std::uint32_t>(bytes, 0x04);
    header.relocation_count = read_le<std::uint32_t>(bytes, 0x08);
    header.public_count = read_le<std::uint32_t>(bytes, 0x0C);
    header.external_count = read_le<std::uint32_t>(bytes, 0x10);
    header.version = read_le<std::uint32_t>(bytes, 0x14);
    std::memcpy(header.reserved.data(), bytes.data() + 0x18, header.reserved.size());
    return header;
}

}  // namespace detail

// Read-only view of an unrelocated HSDArc image.
class View {
public:
    explicit View(std::span<const std::byte> archive)
        : archive_(archive), header_(detail::read_header(archive)) {
        validate();
    }

    [[nodiscard]] const Header& header() const noexcept { return header_; }
    [[nodiscard]] std::span<const std::byte> archive() const noexcept { return archive_; }

    // sub_3BF7E0C masks the low three bits before locating the relocation table.
    [[nodiscard]] constexpr std::size_t data_extent() const noexcept {
        return static_cast<std::size_t>(header_.data_size & ~std::uint32_t{7});
    }

    [[nodiscard]] constexpr std::size_t relocation_table_offset() const noexcept {
        return kHeaderSize + data_extent();
    }

    [[nodiscard]] constexpr std::size_t relocation_table_end() const noexcept {
        return relocation_table_offset()
               + static_cast<std::size_t>(header_.relocation_count) * kRelocationEntrySize;
    }

    [[nodiscard]] std::span<const std::byte> data() const noexcept {
        return archive_.subspan(kHeaderSize, data_extent());
    }

    [[nodiscard]] std::span<const std::byte> metadata_tail() const noexcept {
        return archive_.subspan(relocation_table_end());
    }

    [[nodiscard]] Relocation relocation(std::size_t index) const {
        if (index >= header_.relocation_count) {
            throw Error("HSDArc relocation index is out of range");
        }
        const auto offset = relocation_table_offset() + index * kRelocationEntrySize;
        return Relocation{detail::read_le<std::uint64_t>(archive_, offset)};
    }

    [[nodiscard]] std::uint32_t read_data_u32(std::size_t offset) const {
        return detail::read_le<std::uint32_t>(data(), offset);
    }

    [[nodiscard]] std::uint64_t read_data_u64(std::size_t offset) const {
        return detail::read_le<std::uint64_t>(data(), offset);
    }

    template <typename T>
    [[nodiscard]] T copy_data_object(std::size_t offset) const {
        static_assert(std::is_trivially_copyable_v<T>);
        if (offset > data_extent() || sizeof(T) > data_extent() - offset) {
            throw Error("HSDArc object exceeds the data section");
        }

        T result{};
        std::memcpy(&result, data().data() + offset, sizeof(T));
        return result;
    }

    template <typename T>
    [[nodiscard]] const T* data_object_at(std::size_t offset, std::size_t count = 1) const {
        static_assert(std::is_trivially_copyable_v<T>);
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            throw Error("HSDArc object count overflows size_t");
        }
        const auto size = count * sizeof(T);
        if (offset > data_extent() || size > data_extent() - offset) {
            throw Error("HSDArc object array exceeds the data section");
        }

        const auto* address = data().data() + offset;
        if (reinterpret_cast<std::uintptr_t>(address) % alignof(T) != 0) {
            throw Error("HSDArc object is not suitably aligned");
        }
        return reinterpret_cast<const T*>(address);
    }

private:
    void validate() const {
        if (header_.file_size != archive_.size()) {
            throw Error("HSDArc header file_size does not match the input size");
        }
        if (data_extent() > archive_.size() - kHeaderSize) {
            throw Error("HSDArc data section exceeds the archive");
        }

        const auto table_offset = relocation_table_offset();
        if (table_offset > archive_.size()) {
            throw Error("HSDArc relocation table starts outside the archive");
        }
        if (header_.relocation_count
            > (archive_.size() - table_offset) / kRelocationEntrySize) {
            throw Error("HSDArc relocation table exceeds the archive");
        }

        for (std::size_t index = 0; index < header_.relocation_count; ++index) {
            const auto entry = relocation(index);
            const auto slot = entry.slot_offset();
            if (slot > data_extent() || sizeof(std::uint64_t) > data_extent() - slot) {
                throw Error("HSDArc relocation slot exceeds the data section at index "
                            + std::to_string(index));
            }

            // Targets may reference the trailing string/symbol area after the
            // relocation table. sub_3BF7E0C addresses them from data_base too.
            const auto target = read_data_u64(static_cast<std::size_t>(slot));
            const auto addressable_extent = archive_.size() - kHeaderSize;
            if (target > addressable_extent) {
                throw Error("HSDArc relocation target exceeds the archive body at index "
                            + std::to_string(index));
            }
        }
    }

    std::span<const std::byte> archive_;
    Header header_;
};

// Reproduces sub_3BF7E0C: slot = data_base + (entry & ~7), *slot += data_base.
// The input must be an unrelocated archive. Validation is completed before the
// first write, and attempting to relocate the same buffer twice is rejected.
inline void relocate_in_place(std::span<std::byte> archive) {
    const View view(std::span<const std::byte>{archive.data(), archive.size()});
    const auto data_base = reinterpret_cast<std::uintptr_t>(archive.data() + kHeaderSize);

    for (std::size_t index = 0; index < view.header().relocation_count; ++index) {
        const auto slot = static_cast<std::size_t>(view.relocation(index).slot_offset());
        const auto relative = view.read_data_u64(slot);
        if (relative > std::numeric_limits<std::uint64_t>::max() - data_base) {
            throw Error("HSDArc relocated pointer overflows uint64_t");
        }
        detail::write_le<std::uint64_t>(archive, kHeaderSize + slot, data_base + relative);
    }
}

}  // namespace feh::hsdarc
