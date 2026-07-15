#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace icky {

struct Pattern {
    std::vector<uint8_t> bytes;
    std::vector<bool>    mask;
    static std::optional<Pattern> parse(const std::string& ida);
};

// Scan local module memory
uint64_t scan(uint64_t base, size_t size, const Pattern& pat);
uint64_t scan_ida(uint64_t base, size_t size, const std::string& ida);
uint64_t resolve_rip(uint64_t match, int disp_off, int instr_len);
uint64_t find_rip(uint64_t base, size_t size, const std::string& ida, int disp, int len);

// Scan exported name
uint64_t get_export(uint64_t module_base, const char* name);

} // namespace icky
