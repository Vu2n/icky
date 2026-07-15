#include "pattern_scan.h"
#include "logger.h"

#include <cctype>
#include <sstream>

namespace ue {

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
        if (tok.size() != 2 || !std::isxdigit(static_cast<unsigned char>(tok[0])) ||
            !std::isxdigit(static_cast<unsigned char>(tok[1]))) {
            return std::nullopt;
        }
        auto hex = [](char c) -> uint8_t {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return static_cast<uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10);
        };
        p.bytes.push_back(static_cast<uint8_t>((hex(tok[0]) << 4) | hex(tok[1])));
        p.mask.push_back(true);
    }
    if (p.bytes.empty())
        return std::nullopt;
    return p;
}

uint64_t scan_pattern(const Memory& mem, uint64_t base, size_t size, const Pattern& pat) {
    if (!base || size == 0 || pat.bytes.empty() || pat.bytes.size() > size)
        return 0;

    // Chunked scan to avoid huge single allocations
    constexpr size_t kChunk = 1 << 20; // 1 MiB
    const size_t pat_len = pat.bytes.size();

    size_t offset = 0;
    while (offset + pat_len <= size) {
        const size_t to_read = std::min(kChunk + pat_len, size - offset);
        auto data = mem.read_bytes(base + offset, to_read);
        if (data.size() < pat_len)
            break;

        const size_t limit = data.size() - pat_len;
        for (size_t i = 0; i <= limit; ++i) {
            bool ok = true;
            for (size_t j = 0; j < pat_len; ++j) {
                if (pat.mask[j] && data[i + j] != pat.bytes[j]) {
                    ok = false;
                    break;
                }
            }
            if (ok)
                return base + offset + i;
        }

        if (offset + to_read >= size)
            break;
        // Overlap by pat_len-1 so matches across chunk boundaries are found
        offset += to_read > pat_len ? (to_read - pat_len + 1) : 1;
    }
    return 0;
}

uint64_t resolve_rip(const Memory& mem, uint64_t match, int disp_offset, int instr_len) {
    if (!match)
        return 0;
    int32_t rel = 0;
    if (!mem.read(match + static_cast<uint64_t>(disp_offset), rel))
        return 0;
    return match + static_cast<uint64_t>(instr_len) + static_cast<int64_t>(rel);
}

uint64_t find_rip_ptr(const Memory& mem, uint64_t base, size_t size,
                      const std::string& ida_pattern, int disp_offset, int instr_len) {
    auto pat = Pattern::parse(ida_pattern);
    if (!pat) {
        UE_LOG_E("Invalid pattern: %s", ida_pattern.c_str());
        return 0;
    }
    const uint64_t m = scan_pattern(mem, base, size, *pat);
    if (!m) {
        UE_LOG_D("Pattern not found: %s", ida_pattern.c_str());
        return 0;
    }
    return resolve_rip(mem, m, disp_offset, instr_len);
}

} // namespace ue
