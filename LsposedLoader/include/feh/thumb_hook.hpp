#pragma once

// Minimal ARM32 Thumb-2 entry redirect. It replaces the first eight bytes of
// the target with an absolute literal jump.

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(__ANDROID__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace feh::mod {

using ThumbHookFunction = void (*)();

inline bool patch_thumb_literal_jump(
    std::uintptr_t address,
    std::uintptr_t destination) noexcept {
    // Thumb-2: ldr.w pc, [pc, #0], then a 32-bit Thumb function pointer.
    std::uint8_t patch[8] = {0xDF, 0xF8, 0x00, 0xF0, 0, 0, 0, 0};
    destination |= 1U;
    std::memcpy(patch + 4, &destination, sizeof(destination));

#if defined(__ANDROID__)
    const long page_size = ::sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        return false;
    }
    const auto page_mask = static_cast<std::uintptr_t>(page_size - 1);
    const auto page = address & ~page_mask;
    const auto end = (address + sizeof(patch) + page_mask) & ~page_mask;
    const auto length = end - page;
    if (::mprotect(reinterpret_cast<void*>(page), length,
                   PROT_READ | PROT_WRITE) != 0) {
        return false;
    }
#endif

    std::memcpy(reinterpret_cast<void*>(address), patch, sizeof(patch));
    __builtin___clear_cache(reinterpret_cast<char*>(address),
                            reinterpret_cast<char*>(address + sizeof(patch)));

#if defined(__ANDROID__)
    // If restoring RX fails, the already-patched target is still valid; do
    // not report failure after the trampoline has become reachable.
    (void)::mprotect(reinterpret_cast<void*>(page), length,
                     PROT_READ | PROT_EXEC);
#endif
    return true;
}

inline int InstallThumb32EntryRedirect(
    void* target,
    ThumbHookFunction replacement) noexcept {
    if (target == nullptr || replacement == nullptr) {
        return -1;
    }

#if !defined(__ANDROID__)
    return -2;
#else
    const auto target_ptr = reinterpret_cast<std::uintptr_t>(target) & ~std::uintptr_t{1};
    const auto replacement_ptr = reinterpret_cast<std::uintptr_t>(replacement) | 1U;

    if (!patch_thumb_literal_jump(target_ptr, replacement_ptr)) {
        return -3;
    }
    return 0;
#endif
}

}  // namespace feh::mod
