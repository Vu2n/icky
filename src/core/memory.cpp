#include "memory.h"

namespace icky {

std::string Mem::cstr(uint64_t addr, size_t max_len) {
    if (!addr) return {};
    std::vector<char> buf(max_len + 1, 0);
    if (!read(addr, buf.data(), max_len)) return {};
    buf[max_len] = 0;
    return std::string(buf.data());
}

std::string Mem::wstr(uint64_t addr, size_t max_chars) {
    if (!addr) return {};
    std::vector<wchar_t> buf(max_chars + 1, 0);
    if (!read(addr, buf.data(), max_chars * sizeof(wchar_t))) return {};
    buf[max_chars] = 0;
    std::string out;
    out.reserve(max_chars);
    for (size_t i = 0; i < max_chars && buf[i]; ++i) {
        out.push_back(buf[i] < 128 ? static_cast<char>(buf[i]) : '?');
    }
    return out;
}

std::vector<uint8_t> Mem::bytes(uint64_t addr, size_t n) {
    std::vector<uint8_t> b(n);
    if (!read(addr, b.data(), n)) return {};
    return b;
}

} // namespace icky
