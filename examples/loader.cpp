/**
 * Minimal test loader — injects icky.dll into a process by PID.
 * Requires appropriate privileges; for development only.
 *
 *   icky_loader.exe <pid> [path\to\icky.dll]
 */

#include <Windows.h>
#include <TlHelp32.h>
#include <cstdio>
#include <string>

static bool inject(DWORD pid, const char* dll_path) {
    HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                  PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                              FALSE, pid);
    if (!proc) {
        std::printf("OpenProcess failed (%lu)\n", GetLastError());
        return false;
    }

    size_t len = std::strlen(dll_path) + 1;
    void* remote = VirtualAllocEx(proc, nullptr, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        std::printf("VirtualAllocEx failed\n");
        CloseHandle(proc);
        return false;
    }
    if (!WriteProcessMemory(proc, remote, dll_path, len, nullptr)) {
        std::printf("WriteProcessMemory failed\n");
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        CloseHandle(proc);
        return false;
    }

    auto load = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA"));
    HANDLE th = CreateRemoteThread(proc, nullptr, 0, load, remote, 0, nullptr);
    if (!th) {
        std::printf("CreateRemoteThread failed (%lu)\n", GetLastError());
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        CloseHandle(proc);
        return false;
    }
    WaitForSingleObject(th, 15000);
    CloseHandle(th);
    VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    CloseHandle(proc);
    std::printf("Injected OK into pid %lu\n", pid);
    return true;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("Usage: %s <pid> [icky.dll path]\n", argv[0]);
        return 2;
    }
    DWORD pid = static_cast<DWORD>(std::strtoul(argv[1], nullptr, 10));
    char path[MAX_PATH]{};
    if (argc >= 3) {
        strncpy(path, argv[2], MAX_PATH - 1);
    } else {
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        std::string p(path);
        auto slash = p.find_last_of("\\/");
        if (slash != std::string::npos) p = p.substr(0, slash + 1);
        p += "icky.dll";
        strncpy(path, p.c_str(), MAX_PATH - 1);
    }
    std::printf("DLL: %s\n", path);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        std::printf("DLL not found\n");
        return 1;
    }
    return inject(pid, path) ? 0 : 1;
}
