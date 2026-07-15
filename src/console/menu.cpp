#include "menu.h"
#include "engine/engine_iface.h"
#include "core/logger.h"
#include "core/modules.h"
#include "core/fsutil.h"

#include <icky/icky.h>

#include <Windows.h>
#include <iostream>
#include <string>
#include <cstdio>

namespace icky {
namespace {

void set_color(WORD c) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), c);
}

void banner() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    set_color(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    std::printf("\n");
    std::printf("  ========================================\n");
    std::printf("   I C K Y  - multi-engine SDK dumper\n");
    std::printf("  ========================================\n");
    set_color(FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    std::printf("                    v%s\n", "1.0.0");
    set_color(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    std::printf("  Process : %s\n", process_name().c_str());
    std::printf("  DLL dir : %s\n\n", dll_directory().c_str());
}

int read_choice(int min_v, int max_v, int default_v) {
    std::printf("  > ");
    std::fflush(stdout);
    std::string line;
    if (!std::getline(std::cin, line))
        return default_v;
    if (line.empty())
        return default_v;
    try {
        int v = std::stoi(line);
        if (v < min_v || v > max_v) return default_v;
        return v;
    } catch (...) {
        return default_v;
    }
}

} // namespace

bool alloc_console() {
    if (!AllocConsole()) {
        // Already has console
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        freopen("CONIN$", "r", stdin);
        return true;
    }
    SetConsoleTitleA("Icky — multi-engine SDK dumper");
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    freopen("CONIN$", "r", stdin);
    // Larger buffer
    COORD size{120, 3000};
    SetConsoleScreenBufferSize(GetStdHandle(STD_OUTPUT_HANDLE), size);
    return true;
}

void free_console() {
    fclose(stdout);
    FreeConsole();
}

UserChoices run_console_menu() {
    UserChoices c;
    banner();

    ILOG_I("Scanning modules for engine signatures...");
    DetectResult det{};
    icky_engine auto_eng = detect_best_engine(&det);

    set_color(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    if (auto_eng != ICKY_ENGINE_UNKNOWN) {
        std::printf("  [AUTO] Detected: %s\n", icky_engine_name(auto_eng));
        std::printf("         %s (confidence %.0f%%)\n",
                    det.detail.c_str(), det.confidence * 100.f);
        if (det.primary.base)
            std::printf("         module %s @ 0x%llX\n",
                        det.primary.name.c_str(),
                        (unsigned long long)det.primary.base);
    } else {
        set_color(FOREGROUND_RED | FOREGROUND_INTENSITY);
        std::printf("  [AUTO] No engine confidently detected.\n");
    }
    set_color(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);

    std::printf("\n  Select engine:\n");
    {
        std::string auto_label = "    [0] Auto-detect";
        if (auto_eng != ICKY_ENGINE_UNKNOWN) {
            auto_label += " (";
            auto_label += icky_engine_name(auto_eng);
            auto_label += ")";
        }
        std::printf("%s\n", auto_label.c_str());
    }
    std::printf("    [1] Unreal Engine\n");
    std::printf("    [2] IL2CPP (Unity)\n");
    std::printf("    [3] Mono (Unity)\n");
    std::printf("    [4] Source 1 (CS:GO / classic)\n");
    std::printf("    [5] Source 2 (CS2 / Deadlock)\n");
    std::printf("    [9] Cancel\n");

    int eng_choice = read_choice(0, 9, 0);
    if (eng_choice == 9) {
        c.cancelled = true;
        return c;
    }
    switch (eng_choice) {
    case 1: c.engine = ICKY_ENGINE_UNREAL; break;
    case 2: c.engine = ICKY_ENGINE_IL2CPP; break;
    case 3: c.engine = ICKY_ENGINE_MONO; break;
    case 4: c.engine = ICKY_ENGINE_SOURCE1; break;
    case 5: c.engine = ICKY_ENGINE_SOURCE2; break;
    default: c.engine = ICKY_ENGINE_UNKNOWN; break;
    }

    std::printf("\n  SDK type (this is the important one):\n");
    set_color(FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY);
    std::printf("    [1] Internal  - inject/same-process headers (absolute + RVA helpers)\n");
    std::printf("    [2] External  - RPM/out-of-process headers (RVA-only offsets)\n");
    set_color(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    std::printf("    [3] Both      - generate internal and external trees\n");

    // We encode "both" by returning a special path in run — menu returns mode
    // and interactive runner checks for both. Use mode INTERNAL and store flag in out_dir? 
    // Cleaner: use engine field... We'll use out_dir sentinel or second function.
    // Actually store both as mode with high bit — keep simple: return mode, 
    // interactive handles 3 via local.

    int mode_choice = read_choice(1, 3, 1);
    // Stash both-request in out_dir as marker checked by caller
    if (mode_choice == 2)
        c.mode = ICKY_SDK_EXTERNAL;
    else if (mode_choice == 3) {
        c.mode = static_cast<icky_sdk_mode>(3); // both
    } else
        c.mode = ICKY_SDK_INTERNAL;

    c.out_dir = default_output_dir();
    std::printf("\n  Output directory:\n    %s\n", c.out_dir.c_str());
    std::printf("  Website dump will also be written as:\n");
    std::printf("    icky.dump.json  (upload this on the Icky Dumps site)\n");
    std::printf("  Press Enter to start dump...\n");
    {
        std::string dummy;
        std::getline(std::cin, dummy);
    }
    return c;
}

} // namespace icky
