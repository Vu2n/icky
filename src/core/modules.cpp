#include "modules.h"
#include "logger.h"

#include <Windows.h>
#include <Psapi.h>
#include <algorithm>
#include <cctype>

namespace icky {
namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

HMODULE self_module() {
    HMODULE hm = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&self_module), &hm);
    return hm;
}

} // namespace

std::vector<ModuleInfo> list_modules() {
    std::vector<ModuleInfo> out;
    HMODULE mods[1024];
    DWORD needed = 0;
    HANDLE proc = GetCurrentProcess();
    if (!EnumProcessModulesEx(proc, mods, sizeof(mods), &needed, LIST_MODULES_ALL))
        return out;
    const DWORD count = needed / sizeof(HMODULE);
    char name[MAX_PATH];
    char path[MAX_PATH];
    MODULEINFO mi{};
    for (DWORD i = 0; i < count; ++i) {
        ModuleInfo m;
        if (GetModuleBaseNameA(proc, mods[i], name, MAX_PATH))
            m.name = name;
        if (GetModuleFileNameExA(proc, mods[i], path, MAX_PATH))
            m.path = path;
        if (GetModuleInformation(proc, mods[i], &mi, sizeof(mi))) {
            m.base = reinterpret_cast<uint64_t>(mi.lpBaseOfDll);
            m.size = mi.SizeOfImage;
        }
        out.push_back(std::move(m));
    }
    return out;
}

std::optional<ModuleInfo> find_module(const char* name_substr) {
    if (!name_substr) return std::nullopt;
    const std::string needle = lower(name_substr);
    for (auto& m : list_modules()) {
        if (lower(m.name).find(needle) != std::string::npos)
            return m;
    }
    return std::nullopt;
}

std::optional<ModuleInfo> find_module_exact(const char* name) {
    if (!name) return std::nullopt;
    const std::string needle = lower(name);
    for (auto& m : list_modules()) {
        if (lower(m.name) == needle)
            return m;
    }
    return std::nullopt;
}

std::string dll_directory() {
    char path[MAX_PATH]{};
    HMODULE hm = self_module();
    if (!hm || !GetModuleFileNameA(hm, path, MAX_PATH))
        return ".";
    std::string p(path);
    const auto pos = p.find_last_of("\\/");
    if (pos == std::string::npos) return ".";
    return p.substr(0, pos);
}

std::string process_name() {
    char path[MAX_PATH]{};
    if (!GetModuleFileNameA(nullptr, path, MAX_PATH))
        return "unknown";
    std::string p(path);
    const auto pos = p.find_last_of("\\/");
    return pos == std::string::npos ? p : p.substr(pos + 1);
}

} // namespace icky
