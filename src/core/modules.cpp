#include "modules.h"
#include "logger.h"

#include <Windows.h>
#include <Psapi.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>

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

ModuleInfo from_hmodule(HMODULE hm, const char* fallback_name) {
    ModuleInfo m;
    m.base = reinterpret_cast<uint64_t>(hm);
    char name[MAX_PATH]{};
    char path[MAX_PATH]{};
    HANDLE proc = GetCurrentProcess();
    if (hm && GetModuleBaseNameA(proc, hm, name, MAX_PATH))
        m.name = name;
    else if (fallback_name)
        m.name = fallback_name;
    if (hm && GetModuleFileNameExA(proc, hm, path, MAX_PATH))
        m.path = path;
    MODULEINFO mi{};
    if (hm && GetModuleInformation(proc, hm, &mi, sizeof(mi))) {
        m.base = reinterpret_cast<uint64_t>(mi.lpBaseOfDll);
        m.size = mi.SizeOfImage;
    }
    return m;
}

} // namespace

void step_log(const char* msg) {
    char line[1024];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[icky] %s\r\n", msg ? msg : "");
    OutputDebugStringA(line);

    // Prefer WriteConsole only (one stream — freopen CRT can be half-dead under inject)
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(h, &mode)) {
            DWORD w = 0;
            WriteConsoleA(h, line, (DWORD)lstrlenA(line), &w, nullptr);
            return;
        }
    }
    // Fallback CRT
    fputs(line, stdout);
    fflush(stdout);
}

std::vector<ModuleInfo> list_modules() {
    std::vector<ModuleInfo> out;
    HMODULE mods[512];
    DWORD needed = 0;
    HANDLE proc = GetCurrentProcess();
    if (!EnumProcessModulesEx(proc, mods, sizeof(mods), &needed, LIST_MODULES_DEFAULT))
        return out;
    const DWORD count = needed / sizeof(HMODULE);
    const DWORD n = count > 512 ? 512 : count;
    for (DWORD i = 0; i < n; ++i) {
        if (!mods[i]) continue;
        out.push_back(from_hmodule(mods[i], nullptr));
    }
    return out;
}

std::optional<ModuleInfo> find_module_handle(const char* module_file_name) {
    if (!module_file_name || !*module_file_name) return std::nullopt;
    HMODULE hm = GetModuleHandleA(module_file_name);
    if (!hm) return std::nullopt;
    // Minimal info — avoid Psapi if possible under AC
    ModuleInfo m;
    m.name = module_file_name;
    m.base = reinterpret_cast<uint64_t>(hm);
    // SizeOfImage from PE header (no Psapi)
    auto base = reinterpret_cast<const uint8_t*>(hm);
    if (base[0] == 'M' && base[1] == 'Z') {
        auto lfanew = *reinterpret_cast<const int32_t*>(base + 0x3C);
        if (lfanew > 0 && lfanew < 0x1000) {
            auto pe = base + lfanew;
            if (pe[0] == 'P' && pe[1] == 'E') {
                // OptionalHeader.SizeOfImage at +0x50 for PE32+, +0x38 for PE32
                auto magic = *reinterpret_cast<const uint16_t*>(pe + 24);
                if (magic == 0x20b) // PE32+
                    m.size = *reinterpret_cast<const uint32_t*>(pe + 24 + 56);
                else if (magic == 0x10b)
                    m.size = *reinterpret_cast<const uint32_t*>(pe + 24 + 56);
            }
        }
    }
    return m;
}

std::optional<ModuleInfo> find_module(const char* name_substr) {
    if (!name_substr) return std::nullopt;

    static const char* kExact[] = {
        "GameAssembly.dll", "UnityPlayer.dll",
        "mono-2.0-bdwgc.dll", "mono-2.0-sgen.dll", "mono.dll",
        "client.dll", "engine.dll", "engine2.dll", "schemasystem.dll",
        nullptr
    };
    const std::string needle = lower(name_substr);
    for (int i = 0; kExact[i]; ++i) {
        if (lower(kExact[i]).find(needle) != std::string::npos) {
            if (auto m = find_module_handle(kExact[i]))
                return m;
        }
    }
    if (auto m = find_module_handle(name_substr))
        return m;

    // Avoid full enum during early inject — only if nothing found
    return std::nullopt;
}

std::optional<ModuleInfo> find_module_exact(const char* name) {
    return find_module_handle(name);
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
