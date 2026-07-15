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
#include <iostream>
#include <cstdio>

namespace {

std::atomic<bool> g_running{false};
HMODULE g_module = nullptr;

void log_to_console(icky_log_level level, const char* msg, void*) {
    const char* tag = "INFO";
    switch (level) {
    case ICKY_LOG_TRACE: tag = "TRACE"; break;
    case ICKY_LOG_DEBUG: tag = "DEBUG"; break;
    case ICKY_LOG_INFO:  tag = "INFO";  break;
    case ICKY_LOG_WARN:  tag = "WARN";  break;
    case ICKY_LOG_ERROR: tag = "ERROR"; break;
    default: break;
    }
    std::printf("[%s] %s\n", tag, msg);
    std::fflush(stdout);
}

DWORD WINAPI icky_thread(LPVOID) {
    g_running = true;
    icky::alloc_console();
    icky_set_log_callback(log_to_console, nullptr);
    icky_set_log_level(ICKY_LOG_INFO);

    ILOG_I("Icky %s loaded into %s", icky_version(), icky::process_name().c_str());

    icky_status st = icky_run_interactive();

    if (st == ICKY_OK)
        ILOG_I("Done. You can unload the DLL or close this console.");
    else if (st == ICKY_ERR_CANCELLED)
        ILOG_W("Cancelled by user.");
    else
        ILOG_E("Finished with error: %s", icky_status_string(st));

    std::printf("\n  Press Enter to close console...\n");
    std::string line;
    std::getline(std::cin, line);

    icky::free_console();
    g_running = false;
    // Optional: FreeLibraryAndExitThread for self-unload
    // FreeLibraryAndExitThread(g_module, 0);
    return 0;
}

} // namespace

// ── Public C API ────────────────────────────────────────────────────────────

extern "C" {

ICKY_API const char* ICKY_CALL icky_version(void) {
    return "1.0.0";
}

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
    return icky::detect_best_engine(nullptr);
}

ICKY_API icky_status ICKY_CALL icky_run(icky_engine engine, icky_sdk_mode mode, const char* out_dir) {
    icky::SdkModel model;
    icky::DetectResult det{};
    if (!icky::dump_engine(engine, model, &det))
        return ICKY_ERR_DUMP;

    if (engine == ICKY_ENGINE_UNKNOWN)
        model.engine = det.matched ? icky::detect_best_engine(nullptr) : model.engine;

    std::string dir = out_dir ? out_dir : icky::default_output_dir();

    // mode == 3 means both (internal console encoding)
    if (static_cast<int>(mode) == 3) {
        auto a = icky::write_sdk(model, ICKY_SDK_INTERNAL, dir);
        auto b = icky::write_sdk(model, ICKY_SDK_EXTERNAL, dir);
        if (!a.ok && !b.ok) return ICKY_ERR_IO;
        ILOG_I("Internal files: %d  External files: %d  → %s", a.files, b.files, dir.c_str());
        return ICKY_OK;
    }

    auto wr = icky::write_sdk(model, mode, dir);
    if (!wr.ok) return ICKY_ERR_IO;
    ILOG_I("SDK written: %s (%d files)", wr.out_dir.c_str(), wr.files);
    return ICKY_OK;
}

ICKY_API icky_status ICKY_CALL icky_run_interactive(void) {
    auto choices = icky::run_console_menu();
    if (choices.cancelled)
        return ICKY_ERR_CANCELLED;

    ILOG_I("Dumping… engine=%s mode=%d",
           icky_engine_name(choices.engine == ICKY_ENGINE_UNKNOWN
                                ? icky_detect_engine()
                                : choices.engine),
           static_cast<int>(choices.mode));

    return icky_run(choices.engine, choices.mode,
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
