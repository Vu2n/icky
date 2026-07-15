#include <icky/icky.h>

#include "console/menu.h"
#include "engine/engine_iface.h"
#include "generate/sdk_writer.h"
#include "core/logger.h"
#include "core/fsutil.h"
#include "core/modules.h"

#include <Windows.h>
#include <atomic>
#include <string>
#include <cstdio>

namespace {

std::atomic<bool> g_running{false};
HMODULE g_module = nullptr;

void log_to_console(icky_log_level level, const char* msg, void*) {
    (void)level;
    // Single path only — do not call step_log (avoids double print / re-entry)
    char line[4200];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[icky] %s\r\n", msg ? msg : "");
    OutputDebugStringA(line);
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE) {
        DWORD mode = 0, w = 0;
        if (GetConsoleMode(h, &mode))
            WriteConsoleA(h, line, (DWORD)lstrlenA(line), &w, nullptr);
        else {
            fputs(line, stdout);
            fflush(stdout);
        }
    }
}

DWORD WINAPI icky_thread(LPVOID) {
    Sleep(3000);
    g_running = true;

    icky::step_log("1 worker alive");

    bool con = icky::alloc_console();
    icky::step_log(con ? "2 console ok" : "2 console fail");

    // Skip icky_set_log_callback / mutex logger — that path crashes under some injectors
    // (static std::mutex + freopen CRT). All UI uses step_log / WriteConsole only.
    Sleep(100);
    icky::step_log("3 skip callback (safe)");
    icky::step_log("4 before menu");

    icky_status st = ICKY_ERR_INTERNAL;
    try {
        st = icky_run_interactive();
    } catch (...) {
        icky::step_log("exception in menu/dump");
        st = ICKY_ERR_DUMP;
    }

    icky::step_log(st == ICKY_OK ? "5 done ok" : "5 done with error");

    // Use Win32 only for wait — CRT stdin can be dead
    icky::step_log("press ENTER in console or wait 30s...");
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    if (hin && hin != INVALID_HANDLE_VALUE) {
        // Wait for any key or 30s
        WaitForSingleObject(hin, 30000);
        // Drain
        INPUT_RECORD rec{};
        DWORD n = 0;
        while (PeekConsoleInputA(hin, &rec, 1, &n) && n)
            ReadConsoleInputA(hin, &rec, 1, &n);
    } else {
        Sleep(10000);
    }

    icky::free_console();
    g_running = false;
    return 0;
}

} // namespace

extern "C" {

ICKY_API const char* ICKY_CALL icky_version(void) { return "1.0.3"; }

ICKY_API const char* ICKY_CALL icky_engine_name(icky_engine e) {
    switch (e) {
    case ICKY_ENGINE_UNREAL:  return "Unreal Engine";
    case ICKY_ENGINE_IL2CPP:  return "IL2CPP";
    case ICKY_ENGINE_MONO:    return "Mono";
    case ICKY_ENGINE_SOURCE1: return "Source 1";
    case ICKY_ENGINE_SOURCE2: return "Source 2";
    default: return "Unknown";
    }
}

ICKY_API const char* ICKY_CALL icky_status_string(icky_status s) {
    switch (s) {
    case ICKY_OK: return "OK";
    case ICKY_ERR_INVALID: return "Invalid argument";
    case ICKY_ERR_DETECT: return "Detection failed";
    case ICKY_ERR_DUMP: return "Dump failed";
    case ICKY_ERR_IO: return "I/O error";
    case ICKY_ERR_CANCELLED: return "Cancelled";
    default: return "Internal error";
    }
}

ICKY_API void ICKY_CALL icky_set_log_level(icky_log_level level) {
    icky::set_log_level(level);
}

ICKY_API void ICKY_CALL icky_set_log_callback(icky_log_fn fn, void* user) {
    icky::set_log_callback(fn, user);
}

ICKY_API icky_engine ICKY_CALL icky_detect_engine(void) {
    // Soft only
    if (GetModuleHandleA("GameAssembly.dll")) return ICKY_ENGINE_IL2CPP;
    if (GetModuleHandleA("mono-2.0-bdwgc.dll") || GetModuleHandleA("mono.dll"))
        return ICKY_ENGINE_MONO;
    return ICKY_ENGINE_UNKNOWN;
}

ICKY_API icky_status ICKY_CALL icky_run(icky_engine engine, icky_sdk_mode mode, const char* out_dir) {
    icky::step_log("icky_run");
    icky::SdkModel model;
    icky::DetectResult det{};
    if (!icky::dump_engine(engine, model, &det)) {
        icky::step_log("dump_engine failed");
        return ICKY_ERR_DUMP;
    }
    icky::step_log("writing files");
    std::string dir = out_dir ? out_dir : icky::default_output_dir();
    if (static_cast<int>(mode) == 3) {
        auto a = icky::write_sdk(model, ICKY_SDK_INTERNAL, dir);
        auto b = icky::write_sdk(model, ICKY_SDK_EXTERNAL, dir);
        if (!a.ok && !b.ok) return ICKY_ERR_IO;
        return ICKY_OK;
    }
    auto wr = icky::write_sdk(model, mode, dir);
    return wr.ok ? ICKY_OK : ICKY_ERR_IO;
}

ICKY_API icky_status ICKY_CALL icky_run_interactive(void) {
    icky::step_log("4a run_console_menu enter");
    auto choices = icky::run_console_menu();
    icky::step_log("4b menu returned");
    if (choices.cancelled)
        return ICKY_ERR_CANCELLED;

    icky_engine eng = choices.engine;
    if (eng == ICKY_ENGINE_UNKNOWN)
        eng = icky_detect_engine();
    if (eng == ICKY_ENGINE_UNKNOWN)
        eng = ICKY_ENGINE_IL2CPP;

    char buf[64];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "4c dump eng=%d", (int)eng);
    icky::step_log(buf);

    return icky_run(eng, choices.mode,
                    choices.out_dir.empty() ? nullptr : choices.out_dir.c_str());
}

} // extern "C"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = hModule;
        DisableThreadLibraryCalls(hModule);
        HANDLE th = CreateThread(nullptr, 0, icky_thread, nullptr, 0, nullptr);
        if (th) CloseHandle(th);
    }
    return TRUE;
}
