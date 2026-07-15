#include "menu.h"
#include "core/logger.h"
#include "core/modules.h"
#include "core/fsutil.h"

#include <icky/icky.h>

#include <Windows.h>
#include <string>
#include <cstdio>
#include <cstdlib>

namespace icky {
namespace {

bool g_console_owned = false;

void set_color(WORD c) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE)
        SetConsoleTextAttribute(h, c);
}

int read_choice(int min_v, int max_v, int default_v) {
    step_log("awaiting input");
    printf("  > ");
    fflush(stdout);

    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    if (!hin || hin == INVALID_HANDLE_VALUE)
        return default_v;

    // Line read via ReadConsole (more reliable than fgets after freopen under inject)
    char buf[64]{};
    DWORD read = 0;
    if (!ReadConsoleA(hin, buf, sizeof(buf) - 1, &read, nullptr) || read == 0)
        return default_v;
    buf[read] = 0;
    // strip CR/LF
    for (DWORD i = 0; i < read; ++i)
        if (buf[i] == '\r' || buf[i] == '\n') { buf[i] = 0; break; }
    if (!buf[0]) return default_v;
    int v = atoi(buf);
    if (v < min_v || v > max_v) return default_v;
    return v;
}

icky_engine soft_detect(char* detail, size_t detail_n) {
    if (detail && detail_n) detail[0] = 0;
    if (GetModuleHandleA("GameAssembly.dll")) {
        if (detail) strcpy_s(detail, detail_n, "GameAssembly.dll");
        return ICKY_ENGINE_IL2CPP;
    }
    if (GetModuleHandleA("mono-2.0-bdwgc.dll") || GetModuleHandleA("mono.dll")) {
        if (detail) strcpy_s(detail, detail_n, "mono");
        return ICKY_ENGINE_MONO;
    }
    if (GetModuleHandleA("engine2.dll")) {
        if (detail) strcpy_s(detail, detail_n, "engine2");
        return ICKY_ENGINE_SOURCE2;
    }
    if (GetModuleHandleA("client.dll") && GetModuleHandleA("engine.dll")) {
        if (detail) strcpy_s(detail, detail_n, "client+engine");
        return ICKY_ENGINE_SOURCE1;
    }
    return ICKY_ENGINE_UNKNOWN;
}

} // namespace

bool alloc_console() {
    if (!GetConsoleWindow()) {
        if (!AllocConsole())
            return false;
        g_console_owned = true;
    }
    SetConsoleTitleA("Icky");

    // Attach std handles without freopen first — freopen can crash some hosts
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
    freopen_s(&f, "CONIN$", "r", stdin);

    // Also force Win32 standard handles
    SetStdHandle(STD_OUTPUT_HANDLE, CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                               OPEN_EXISTING, 0, nullptr));
    SetStdHandle(STD_INPUT_HANDLE, CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE,
                                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                              OPEN_EXISTING, 0, nullptr));
    return true;
}

void free_console() {
    if (g_console_owned) {
        FreeConsole();
        g_console_owned = false;
    }
}

UserChoices run_console_menu() {
    UserChoices c;
    step_log("menu: enter");

    char detail[128]{};
    step_log("menu: soft_detect");
    icky_engine auto_eng = soft_detect(detail, sizeof(detail));
    step_log("menu: soft_detect done");

    set_color(FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY);
    printf("\n  ICKY dumper\n\n");
    set_color(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);

    if (auto_eng != ICKY_ENGINE_UNKNOWN)
        printf("  Auto: %s (%s)\n", icky_engine_name(auto_eng), detail);
    else
        printf("  Auto: none — pick manually\n");

    printf("\n  Engine:\n");
    printf("    [0] Auto\n");
    printf("    [1] Unreal\n");
    printf("    [2] IL2CPP (Rust/Unity)\n");
    printf("    [3] Mono\n");
    printf("    [4] Source1\n");
    printf("    [5] Source2\n");
    printf("    [9] Cancel\n");

    int eng = read_choice(0, 9, auto_eng == ICKY_ENGINE_IL2CPP ? 2 : 0);
    step_log("menu: engine chosen");
    if (eng == 9) {
        c.cancelled = true;
        return c;
    }
    switch (eng) {
    case 1: c.engine = ICKY_ENGINE_UNREAL; break;
    case 2: c.engine = ICKY_ENGINE_IL2CPP; break;
    case 3: c.engine = ICKY_ENGINE_MONO; break;
    case 4: c.engine = ICKY_ENGINE_SOURCE1; break;
    case 5: c.engine = ICKY_ENGINE_SOURCE2; break;
    default: c.engine = auto_eng != ICKY_ENGINE_UNKNOWN ? auto_eng : ICKY_ENGINE_IL2CPP; break;
    }

    printf("\n  Mode:\n");
    printf("    [1] Internal\n");
    printf("    [2] External (recommended)\n");
    printf("    [3] Both\n");
    int mode = read_choice(1, 3, 2);
    step_log("menu: mode chosen");
    if (mode == 2) c.mode = ICKY_SDK_EXTERNAL;
    else if (mode == 3) c.mode = static_cast<icky_sdk_mode>(3);
    else c.mode = ICKY_SDK_INTERNAL;

    step_log("menu: resolving out dir");
    c.out_dir = default_output_dir();
    printf("\n  Out: %s\n", c.out_dir.c_str());
    printf("  Enter to dump...\n");
    {
        char dummy[8]{};
        DWORD n = 0;
        HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
        if (hin && hin != INVALID_HANDLE_VALUE)
            ReadConsoleA(hin, dummy, sizeof(dummy) - 1, &n, nullptr);
    }
    step_log("menu: leave -> dump");
    return c;
}

} // namespace icky
