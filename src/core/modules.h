#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace icky {

struct ModuleInfo {
    std::string name;
    std::string path;
    uint64_t    base = 0;
    size_t      size = 0;
};

std::vector<ModuleInfo> list_modules();
std::optional<ModuleInfo> find_module(const char* name_substr); // case-insensitive contains
std::optional<ModuleInfo> find_module_exact(const char* name);

// Directory containing this DLL (Icky)
std::string dll_directory();
std::string process_name();

} // namespace icky
