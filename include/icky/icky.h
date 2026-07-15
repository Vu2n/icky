#pragma once

/**
 * Icky — multi-engine SDK dumper (injectable DLL)
 *
 * Inject into a game process. A console opens automatically:
 *   1. Auto-detects engine (Unreal / IL2CPP / Mono / Source1 / Source2)
 *   2. Asks Internal vs External SDK
 *   3. Writes SDK next to the DLL (or %TEMP%\icky_sdk)
 *
 * You can also drive it from code via the C API below.
 */

#include "export.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Full interactive dump (console). Called automatically on inject. */
ICKY_API icky_status ICKY_CALL icky_run_interactive(void);

/**
 * Non-interactive dump.
 * @param engine  ICKY_ENGINE_UNKNOWN = auto-detect
 * @param mode    Internal or External
 * @param out_dir Output directory (created if needed). NULL = auto path.
 */
ICKY_API icky_status ICKY_CALL icky_run(
    icky_engine engine,
    icky_sdk_mode mode,
    const char* out_dir);

/** Detect engine in the current process. */
ICKY_API icky_engine ICKY_CALL icky_detect_engine(void);

ICKY_API const char* ICKY_CALL icky_engine_name(icky_engine e);
ICKY_API const char* ICKY_CALL icky_status_string(icky_status s);
ICKY_API const char* ICKY_CALL icky_version(void);

ICKY_API void ICKY_CALL icky_set_log_level(icky_log_level level);
ICKY_API void ICKY_CALL icky_set_log_callback(icky_log_fn fn, void* user);

#ifdef __cplusplus
}
#endif
