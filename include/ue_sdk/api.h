#pragma once

/**
 * ue_sdk_gen — Multi-engine Unreal Engine SDK generation DLL
 *
 * Typical flow:
 *   1. ue_init()
 *   2. ue_session_create() with a memory backend (live process, dump, emulator…)
 *   3. ue_session_set_version() or ue_session_detect_version()
 *   4. ue_session_set_globals() and/or ue_session_find_globals()
 *   5. ue_session_dump()  → writes SDK headers / JSON
 *   6. ue_session_destroy(); ue_shutdown()
 */

#include "export.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── Lifecycle ───────────────────────────────────────────────────────────────

/** Initialize library (logging, internal state). Call once per process. */
UE_SDK_API ue_status UE_SDK_CALL ue_init(void);

/** Tear down library. */
UE_SDK_API void UE_SDK_CALL ue_shutdown(void);

/** Library version string, e.g. "1.0.0". */
UE_SDK_API const char* UE_SDK_CALL ue_version_string(void);

/** Human-readable message for a status code. */
UE_SDK_API const char* UE_SDK_CALL ue_status_string(ue_status status);

// ── Logging ─────────────────────────────────────────────────────────────────

UE_SDK_API void UE_SDK_CALL ue_set_log_level(ue_log_level level);
UE_SDK_API void UE_SDK_CALL ue_set_log_callback(ue_log_callback cb, void* user);

// ── Built-in memory backends ────────────────────────────────────────────────

/**
 * Attach to a local Windows process by PID.
 * Owns nothing; process must stay alive for the session.
 * Returns a backend filled for use with ue_session_create.
 * On failure returns UE_ERR_PROCESS and leaves *out zeroed.
 */
UE_SDK_API ue_status UE_SDK_CALL ue_backend_open_process(
    uint32_t pid,
    ue_memory_backend* out_backend);

/** Close a process backend opened by ue_backend_open_process. */
UE_SDK_API void UE_SDK_CALL ue_backend_close_process(ue_memory_backend* backend);

/**
 * Backend over a raw memory image (crash dump / emulator RAM).
 * `image` must remain valid for the session lifetime.
 * module_base is treated as the image base for pattern scans.
 */
UE_SDK_API ue_status UE_SDK_CALL ue_backend_open_image(
    const void* image,
    size_t image_size,
    uint64_t image_base,
    const char* module_name,
    ue_memory_backend* out_backend);

UE_SDK_API void UE_SDK_CALL ue_backend_close_image(ue_memory_backend* backend);

// ── Session ─────────────────────────────────────────────────────────────────

UE_SDK_API ue_status UE_SDK_CALL ue_session_create(
    const ue_memory_backend* backend,
    ue_session** out_session);

UE_SDK_API void UE_SDK_CALL ue_session_destroy(ue_session* session);

/**
 * Select a known engine profile by version (offsets + name layout).
 * Built-in profiles cover common UE4.20–UE5.4 layouts; custom offsets via
 * ue_session_set_offsets override after this call.
 */
UE_SDK_API ue_status UE_SDK_CALL ue_session_set_version(
    ue_session* session,
    ue_version version);

/** Best-effort version guess from module PE resources / patterns. */
UE_SDK_API ue_status UE_SDK_CALL ue_session_detect_version(
    ue_session* session,
    ue_version* out_version);

/** Replace offset profile (deep copy). */
UE_SDK_API ue_status UE_SDK_CALL ue_session_set_offsets(
    ue_session* session,
    const ue_offsets* offsets);

UE_SDK_API ue_status UE_SDK_CALL ue_session_get_offsets(
    const ue_session* session,
    ue_offsets* out_offsets);

/** Provide known globals (0 fields are left for auto-find). */
UE_SDK_API ue_status UE_SDK_CALL ue_session_set_globals(
    ue_session* session,
    const ue_globals* globals);

UE_SDK_API ue_status UE_SDK_CALL ue_session_get_globals(
    const ue_session* session,
    ue_globals* out_globals);

/**
 * Pattern-scan the main module for GObjects / GNames / GWorld / ProcessEvent
 * using version-specific signatures. Requires version (or offsets) set first.
 */
UE_SDK_API ue_status UE_SDK_CALL ue_session_find_globals(
    ue_session* session,
    const char* module_name);

/** Resolve an FName ComparisonIndex to a UTF-8 string (null-terminated). */
UE_SDK_API ue_status UE_SDK_CALL ue_session_get_name(
    ue_session* session,
    int32_t comparison_index,
    int32_t number,
    char* buffer,
    size_t buffer_size);

// ── Dump / generate ─────────────────────────────────────────────────────────

/**
 * Walk GObjects, reconstruct packages/classes/structs/enums, write SDK.
 * options->output_dir must exist or be creatable.
 */
UE_SDK_API ue_status UE_SDK_CALL ue_session_dump(
    ue_session* session,
    const ue_dump_options* options,
    ue_dump_stats* out_stats);

/**
 * Dump only offsets + globals to a single C header (fast path, no full walk).
 */
UE_SDK_API ue_status UE_SDK_CALL ue_session_export_offsets_header(
    ue_session* session,
    const char* output_path);

// ── Version database helpers ────────────────────────────────────────────────

/** Number of built-in engine profiles. */
UE_SDK_API int UE_SDK_CALL ue_builtin_profile_count(void);

/** Get built-in profile name and version by index (0 .. count-1). */
UE_SDK_API ue_status UE_SDK_CALL ue_builtin_profile_info(
    int index,
    char* name_buf,
    size_t name_buf_size,
    ue_version* out_version);

/** Fill default offsets for a version; returns UE_ERR_VERSION if unknown. */
UE_SDK_API ue_status UE_SDK_CALL ue_offsets_for_version(
    ue_version version,
    ue_offsets* out_offsets);

// ── Defaults ────────────────────────────────────────────────────────────────

/** Fill dump options with sensible defaults (caller still sets output_dir). */
UE_SDK_API void UE_SDK_CALL ue_dump_options_init(ue_dump_options* options);

#ifdef __cplusplus
}
#endif
