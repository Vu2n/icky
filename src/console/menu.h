#pragma once

#include <icky/types.h>
#include <string>

namespace icky {

struct UserChoices {
    icky_engine   engine = ICKY_ENGINE_UNKNOWN; // UNKNOWN = auto
    icky_sdk_mode mode   = ICKY_SDK_INTERNAL;
    bool          cancelled = false;
    std::string   out_dir;
};

// Alloc console, print banner, detect engine, ask internal/external.
UserChoices run_console_menu();

bool alloc_console();
void free_console();

} // namespace icky
