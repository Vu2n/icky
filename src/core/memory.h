#pragma once

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>
#include <Windows.h>

namespace icky {

// Safe for injectors: reject guard / noaccess and never cross regions blindly.
inline bool is_readable(const void* p, size_t size) {
    if (!p || !size) return false;
    auto cur = reinterpret_cast<const uint8_t*>(p);
    const auto end = cur + size;
    while (cur < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(cur, &mbi, sizeof(mbi)))
            return false;
        if (mbi.State != MEM_COMMIT)
            return false;
        // PAGE_GUARD faults on first touch — common crash source when scanning modules
        if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))
            return false;
        const DWORD prot = mbi.Protect & 0xFF;
        const bool ok =
            prot == PAGE_READONLY || prot == PAGE_READWRITE || prot == PAGE_WRITECOPY ||
            prot == PAGE_EXECUTE_READ || prot == PAGE_EXECUTE_READWRITE ||
            prot == PAGE_EXECUTE_WRITECOPY;
        if (!ok)
            return false;
        const auto reg_end =
            reinterpret_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
        if (reg_end <= cur)
            return false;
        cur = reg_end < end ? reg_end : end;
    }
    return true;
}

// SEH-guarded copy — last line of defense under EAC/obfuscated modules
inline bool safe_copy(const void* src, void* dst, size_t size) {
    if (!src || !dst || !size) return false;
    __try {
        std::memcpy(dst, src, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

class Mem {
public:
    static bool read(uint64_t addr, void* buf, size_t size) {
        if (!addr || !buf || !size) return false;
        const void* p = reinterpret_cast<const void*>(static_cast<uintptr_t>(addr));
        if (!is_readable(p, size)) return false;
        return safe_copy(p, buf, size);
    }

    template <typename T>
    static bool read(uint64_t addr, T& out) {
        return read(addr, &out, sizeof(T));
    }

    template <typename T>
    static std::optional<T> get(uint64_t addr) {
        T v{};
        if (!read(addr, v)) return std::nullopt;
        return v;
    }

    static uint64_t ptr(uint64_t addr) {
        uint64_t p = 0;
        read(addr, p);
        return p;
    }

    static bool valid_user_ptr(uint64_t p) {
        return p >= 0x10000 && p <= 0x00007FFFFFFFFFFFULL;
    }

    static std::string cstr(uint64_t addr, size_t max_len = 1024);
    static std::string wstr(uint64_t addr, size_t max_chars = 512);
    static std::vector<uint8_t> bytes(uint64_t addr, size_t n);
};

} // namespace icky
