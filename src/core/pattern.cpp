#include "pattern.h"
#include "memory.h"
#include "pe.h"
#include "logger.h"

#include <cctype>
#include <sstream>
#include <Windows.h>

namespace icky {

std::optional<Pattern> Pattern::parse(const std::string& ida) {
    Pattern p;
    std::istringstream iss(ida);
    std::string tok;
    while (iss >> tok) {
        if (tok == "?" || tok == "??") {
            p.bytes.push_back(0);
            p.mask.push_back(false);
            continue;
        }
        if (tok.size() != 2) return std::nullopt;
        auto hx = [](char c) -> int {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        int a = hx(tok[0]), b = hx(tok[1]);
        if (a < 0 || b < 0) return std::nullopt;
        p.bytes.push_back(static_cast<uint8_t>((a << 4) | b));
        p.mask.push_back(true);
    }
    if (p.bytes.empty()) return std::nullopt;
    return p;
}

uint64_t scan(uint64_t base, size_t size, const Pattern& pat) {
    if (!base || !size || pat.bytes.empty() || pat.bytes.size() > size) return 0;
    const auto* data = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(base));
    const size_t n = pat.bytes.size();
    // Page-wise: skip non-readable / PAGE_GUARD regions (prevents inject AV)
    for (size_t i = 0; i + n <= size;) {
        if (!is_readable(data + i, n)) {
            // jump to next page
            i = (i + 0x1000) & ~size_t(0xFFF);
            continue;
        }
        // scan within this page as far as readable
        size_t page_end = (i + 0x1000) & ~size_t(0xFFF);
        if (page_end <= i) page_end = i + 0x1000;
        if (page_end > size) page_end = size;
        const size_t last = page_end >= n ? page_end - n : 0;
        for (; i <= last; ++i) {
            bool ok = true;
            for (size_t j = 0; j < n; ++j) {
                if (pat.mask[j] && data[i + j] != pat.bytes[j]) {
                    ok = false;
                    break;
                }
            }
            if (ok) return base + i;
        }
        i = page_end;
    }
    return 0;
}

uint64_t scan_ida(uint64_t base, size_t size, const std::string& ida) {
    auto p = Pattern::parse(ida);
    if (!p) return 0;
    return scan(base, size, *p);
}

uint64_t resolve_rip(uint64_t match, int disp_off, int instr_len) {
    if (!match) return 0;
    int32_t rel = 0;
    if (!Mem::read(match + static_cast<uint64_t>(disp_off), rel)) return 0;
    return match + static_cast<uint64_t>(instr_len) + static_cast<int64_t>(rel);
}

uint64_t find_rip(uint64_t base, size_t size, const std::string& ida, int disp, int len) {
    const uint64_t m = scan_ida(base, size, ida);
    if (!m) return 0;
    return resolve_rip(m, disp, len);
}

uint64_t get_export(uint64_t module_base, const char* name) {
    if (!module_base || !name) return 0;
    HMODULE hm = reinterpret_cast<HMODULE>(static_cast<uintptr_t>(module_base));
    FARPROC p = GetProcAddress(hm, name);
    return reinterpret_cast<uint64_t>(p);
}

} // namespace icky
