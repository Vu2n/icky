#pragma once

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>
#include <Windows.h>

namespace icky {

inline bool is_readable(const void* p, size_t size) {
    if (!p || !size) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    const DWORD prot = mbi.Protect & 0xFF;
    if (prot == PAGE_NOACCESS || prot == PAGE_EXECUTE) return false;
    const auto start = reinterpret_cast<const uint8_t*>(p);
    const auto end = start + size;
    const auto reg_end = reinterpret_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
    return end <= reg_end;
}

// In-process (injected) memory view
class Mem {
public:
    static bool read(uint64_t addr, void* buf, size_t size) {
        if (!addr || !buf || !size) return false;
        const void* p = reinterpret_cast<const void*>(static_cast<uintptr_t>(addr));
        if (!is_readable(p, size)) return false;
        std::memcpy(buf, p, size);
        return true;
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
