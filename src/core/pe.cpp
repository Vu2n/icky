#include "pe.h"
#include "memory.h"

#include <Windows.h>

namespace icky {

bool pe_valid(uint64_t base) {
    if (!base) return false;
    uint16_t mz = 0;
    if (!Mem::read(base, mz) || mz != 0x5A4D) return false;
    int32_t lfanew = 0;
    if (!Mem::read(base + 0x3C, lfanew)) return false;
    uint32_t pe = 0;
    if (!Mem::read(base + static_cast<uint64_t>(lfanew), pe) || pe != 0x00004550)
        return false;
    return true;
}

std::vector<PeSection> pe_sections(uint64_t base) {
    std::vector<PeSection> out;
    if (!pe_valid(base)) return out;
    int32_t lfanew = 0;
    Mem::read(base + 0x3C, lfanew);
    const uint64_t nt = base + static_cast<uint64_t>(lfanew);
    uint16_t num_sections = 0;
    uint16_t opt_size = 0;
    Mem::read(nt + 6, num_sections);
    Mem::read(nt + 20, opt_size);
    uint64_t sec = nt + 24 + opt_size;
    for (uint16_t i = 0; i < num_sections; ++i) {
        char name[9]{};
        Mem::read(sec, name, 8);
        PeSection s;
        s.name = name;
        uint32_t vsize = 0, va = 0, ch = 0;
        Mem::read(sec + 8, vsize);
        Mem::read(sec + 12, va);
        Mem::read(sec + 36, ch);
        s.va = base + va;
        s.size = vsize;
        s.characteristics = ch;
        out.push_back(std::move(s));
        sec += 40;
    }
    return out;
}

std::string pe_product_version(const std::string& module_path) {
    DWORD h = 0;
    DWORD sz = GetFileVersionInfoSizeA(module_path.c_str(), &h);
    if (!sz) return {};
    std::vector<char> buf(sz);
    if (!GetFileVersionInfoA(module_path.c_str(), 0, sz, buf.data())) return {};
    VS_FIXEDFILEINFO* fi = nullptr;
    UINT len = 0;
    if (!VerQueryValueA(buf.data(), "\\", reinterpret_cast<void**>(&fi), &len) || !fi)
        return {};
    char ver[64];
    snprintf(ver, sizeof(ver), "%u.%u.%u.%u",
             HIWORD(fi->dwProductVersionMS), LOWORD(fi->dwProductVersionMS),
             HIWORD(fi->dwProductVersionLS), LOWORD(fi->dwProductVersionLS));
    return ver;
}

} // namespace icky
