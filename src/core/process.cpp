#include "process.h"
#include "logger.h"

#include <cstring>
#include <new>
#include <cctype>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <Windows.h>
#  include <TlHelp32.h>
#  include <Psapi.h>
#endif

namespace ue {
namespace {

int str_ieq(const char* a, const char* b) {
    if (!a || !b)
        return 0;
    while (*a && *b) {
        const int ca = std::tolower(static_cast<unsigned char>(*a++));
        const int cb = std::tolower(static_cast<unsigned char>(*b++));
        if (ca != cb)
            return 0;
    }
    return *a == *b;
}

#if defined(_WIN32)

int process_read(void* ctx, uint64_t address, void* buffer, size_t size) {
    auto* st = static_cast<ProcessBackendState*>(ctx);
    if (!st || !st->handle || !buffer || size == 0)
        return -1;
    SIZE_T read = 0;
    if (!ReadProcessMemory(st->handle, reinterpret_cast<LPCVOID>(address), buffer, size, &read))
        return -1;
    return read == size ? 0 : -1;
}

int process_write(void* ctx, uint64_t address, const void* buffer, size_t size) {
    auto* st = static_cast<ProcessBackendState*>(ctx);
    if (!st || !st->handle || !buffer || size == 0)
        return -1;
    SIZE_T written = 0;
    if (!WriteProcessMemory(st->handle, reinterpret_cast<LPVOID>(address), buffer, size, &written))
        return -1;
    return written == size ? 0 : -1;
}

uint64_t process_module_base(void* ctx, const char* module_name) {
    auto* st = static_cast<ProcessBackendState*>(ctx);
    if (!st || !st->handle || !module_name)
        return 0;

    HMODULE mods[1024];
    DWORD needed = 0;
    if (!EnumProcessModulesEx(st->handle, mods, sizeof(mods), &needed, LIST_MODULES_ALL))
        return 0;

    const DWORD count = needed / sizeof(HMODULE);
    char name[MAX_PATH];
    for (DWORD i = 0; i < count; ++i) {
        if (!GetModuleBaseNameA(st->handle, mods[i], name, MAX_PATH))
            continue;
        if (str_ieq(name, module_name))
            return reinterpret_cast<uint64_t>(mods[i]);
    }
    // Also try full path basename match already done; try partial
    for (DWORD i = 0; i < count; ++i) {
        if (!GetModuleBaseNameA(st->handle, mods[i], name, MAX_PATH))
            continue;
        if (std::strstr(name, module_name) != nullptr)
            return reinterpret_cast<uint64_t>(mods[i]);
    }
    return 0;
}

size_t process_module_size(void* ctx, const char* module_name) {
    auto* st = static_cast<ProcessBackendState*>(ctx);
    if (!st || !st->handle || !module_name)
        return 0;

    HMODULE mods[1024];
    DWORD needed = 0;
    if (!EnumProcessModulesEx(st->handle, mods, sizeof(mods), &needed, LIST_MODULES_ALL))
        return 0;

    const DWORD count = needed / sizeof(HMODULE);
    char name[MAX_PATH];
    MODULEINFO mi{};
    for (DWORD i = 0; i < count; ++i) {
        if (!GetModuleBaseNameA(st->handle, mods[i], name, MAX_PATH))
            continue;
        if (str_ieq(name, module_name) || std::strstr(name, module_name)) {
            if (GetModuleInformation(st->handle, mods[i], &mi, sizeof(mi)))
                return mi.SizeOfImage;
        }
    }
    return 0;
}

#endif // _WIN32

int image_read(void* ctx, uint64_t address, void* buffer, size_t size) {
    auto* st = static_cast<ImageBackendState*>(ctx);
    if (!st || !st->image || !buffer || size == 0)
        return -1;
    if (address < st->image_base)
        return -1;
    const uint64_t off = address - st->image_base;
    if (off + size > st->image_size)
        return -1;
    std::memcpy(buffer, st->image + off, size);
    return 0;
}

uint64_t image_module_base(void* ctx, const char* module_name) {
    auto* st = static_cast<ImageBackendState*>(ctx);
    if (!st)
        return 0;
    if (!module_name || st->module_name.empty())
        return st->image_base;
    if (str_ieq(st->module_name.c_str(), module_name))
        return st->image_base;
    // Accept any name when single-image mode
    return st->image_base;
}

size_t image_module_size(void* ctx, const char* /*module_name*/) {
    auto* st = static_cast<ImageBackendState*>(ctx);
    return st ? st->image_size : 0;
}

} // namespace

int process_backend_open(uint32_t pid, ue_memory_backend* out, ProcessBackendState** state_out) {
#if !defined(_WIN32)
    (void)pid; (void)out; (void)state_out;
    UE_LOG_E("Process backend is only available on Windows");
    return -1;
#else
    if (!out || !state_out || pid == 0)
        return -1;

    HANDLE h = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION |
                               PROCESS_QUERY_INFORMATION,
                           FALSE, pid);
    if (!h) {
        // Retry read-only
        h = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    }
    if (!h) {
        UE_LOG_E("OpenProcess(%u) failed (err=%lu)", pid, GetLastError());
        return -1;
    }

    auto* st = new (std::nothrow) ProcessBackendState{};
    if (!st) {
        CloseHandle(h);
        return -1;
    }
    st->handle = h;
    st->pid    = pid;

    std::memset(out, 0, sizeof(*out));
    out->user        = st;
    out->read        = process_read;
    out->write       = process_write;
    out->module_base = process_module_base;
    out->module_size = process_module_size;

    *state_out = st;
    UE_LOG_I("Attached to process %u", pid);
    return 0;
#endif
}

void process_backend_close(ue_memory_backend* backend, ProcessBackendState* state) {
    if (state) {
#if defined(_WIN32)
        if (state->handle)
            CloseHandle(static_cast<HANDLE>(state->handle));
#endif
        delete state;
    }
    if (backend)
        std::memset(backend, 0, sizeof(*backend));
}

int image_backend_open(const void* image, size_t size, uint64_t base, const char* name,
                       ue_memory_backend* out, ImageBackendState** state_out) {
    if (!image || size == 0 || !out || !state_out)
        return -1;

    auto* st = new (std::nothrow) ImageBackendState{};
    if (!st)
        return -1;
    st->image       = static_cast<const uint8_t*>(image);
    st->image_size  = size;
    st->image_base  = base;
    st->module_name = name ? name : "image";

    std::memset(out, 0, sizeof(*out));
    out->user        = st;
    out->read        = image_read;
    out->write       = nullptr;
    out->module_base = image_module_base;
    out->module_size = image_module_size;

    *state_out = st;
    return 0;
}

void image_backend_close(ue_memory_backend* backend, ImageBackendState* state) {
    delete state;
    if (backend)
        std::memset(backend, 0, sizeof(*backend));
}

} // namespace ue
