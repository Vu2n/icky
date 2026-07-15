#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace icky {

struct PeSection {
    std::string name;
    uint64_t    va = 0;
    uint64_t    size = 0;
    uint32_t    characteristics = 0;
};

bool pe_valid(uint64_t base);
std::vector<PeSection> pe_sections(uint64_t base);
// Product version string from VERSIONINFO if present (best-effort empty)
std::string pe_product_version(const std::string& module_path);

} // namespace icky
