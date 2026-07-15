#pragma once

#include "memory.h"
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace ue {

struct Pattern {
    std::vector<uint8_t> bytes;
    std::vector<bool>    mask; // true = must match

    // IDA-style: "48 8B 05 ?? ?? ?? ?? 48 85 C0"
    static std::optional<Pattern> parse(const std::string& ida);
};

// Scan [base, base+size) in target memory via backend.
// Returns first match address, or 0.
uint64_t scan_pattern(const Memory& mem, uint64_t base, size_t size, const Pattern& pat);

// Resolve RIP-relative: instr_addr + instr_len + *(int32*)(instr_addr + disp_offset)
uint64_t resolve_rip(const Memory& mem, uint64_t match, int disp_offset, int instr_len);

// Convenience: scan + RIP resolve in one call
uint64_t find_rip_ptr(const Memory& mem, uint64_t base, size_t size,
                      const std::string& ida_pattern, int disp_offset, int instr_len);

} // namespace ue
