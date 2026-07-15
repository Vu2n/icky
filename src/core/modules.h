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

// Full list — can be heavy / flaky under AC; avoid during inject startup
std::vector<ModuleInfo> list_modules();

// Fast: GetModuleHandleA only (no EnumProcessModules) — safe for post-inject detect
std::optional<ModuleInfo> find_module_handle(const char* module_file_name);

// Substring search: tries common exact names first, then full enum as last resort
std::optional<ModuleInfo> find_module(const char* name_substr);
std::optional<ModuleInfo> find_module_exact(const char* name);

std::string dll_directory();
std::string process_name();

// Step logger that always hits the console handle (survives broken CRT)
void step_log(const char* msg);

} // namespace icky
