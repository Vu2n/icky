#include <ue_sdk/api.h>

#include "core/logger.h"
#include "core/memory.h"
#include "core/process.h"
#include "dumper/sdk_dumper.h"
#include "engine/version_db.h"
#include "generator/cpp_generator.h"
#include "generator/json_export.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

// ── Opaque session ──────────────────────────────────────────────────────────

struct ue_session {
    ue::Memory      memory;
    ue_offsets      offsets{};
    ue_globals      globals{};
    ue_version      version{4, 27, 0};
    const ue::EngineProfile* profile = nullptr;
    std::unique_ptr<ue::SdkDumper> dumper;

    // Optional owned backend state
    enum class BackendKind { None, Process, Image };
    BackendKind            backend_kind = BackendKind::None;
    ue::ProcessBackendState* process_state = nullptr;
    ue::ImageBackendState*   image_state   = nullptr;
    ue_memory_backend        owned_backend{};

    void rebuild_dumper() {
        dumper = std::make_unique<ue::SdkDumper>(memory, offsets, globals);
        dumper->set_profile(profile);
    }
};

namespace {

bool g_inited = false;

// Process backends opened via C API without a session still need cleanup tracking.
// We store state pointer inside backend->user (already done by process layer).

} // namespace

// ── Lifecycle ───────────────────────────────────────────────────────────────

ue_status UE_SDK_CALL ue_init(void) {
    if (g_inited)
        return UE_ERR_ALREADY_INIT;
    g_inited = true;
    UE_LOG_I("ue_sdk_gen %s initialized", ue_version_string());
    return UE_OK;
}

void UE_SDK_CALL ue_shutdown(void) {
    g_inited = false;
}

const char* UE_SDK_CALL ue_version_string(void) {
    return "1.0.0";
}

const char* UE_SDK_CALL ue_status_string(ue_status status) {
    switch (status) {
    case UE_OK:               return "OK";
    case UE_ERR_INVALID_ARG:  return "Invalid argument";
    case UE_ERR_NOT_FOUND:    return "Not found";
    case UE_ERR_PROCESS:      return "Process error";
    case UE_ERR_MEMORY:       return "Memory error";
    case UE_ERR_PATTERN:      return "Pattern scan failed";
    case UE_ERR_VERSION:      return "Unknown engine version";
    case UE_ERR_DUMP:         return "Dump failed";
    case UE_ERR_IO:           return "I/O error";
    case UE_ERR_ALREADY_INIT: return "Already initialized";
    case UE_ERR_NOT_INIT:     return "Not initialized";
    case UE_ERR_INTERNAL:     return "Internal error";
    default:                  return "Unknown error";
    }
}

void UE_SDK_CALL ue_set_log_level(ue_log_level level) {
    ue::set_log_level(level);
}

void UE_SDK_CALL ue_set_log_callback(ue_log_callback cb, void* user) {
    ue::set_log_callback(cb, user);
}

// ── Backends ────────────────────────────────────────────────────────────────

ue_status UE_SDK_CALL ue_backend_open_process(uint32_t pid, ue_memory_backend* out_backend) {
    if (!out_backend || pid == 0)
        return UE_ERR_INVALID_ARG;
    ue::ProcessBackendState* st = nullptr;
    if (ue::process_backend_open(pid, out_backend, &st) != 0)
        return UE_ERR_PROCESS;
    return UE_OK;
}

void UE_SDK_CALL ue_backend_close_process(ue_memory_backend* backend) {
    if (!backend)
        return;
    auto* st = static_cast<ue::ProcessBackendState*>(backend->user);
    ue::process_backend_close(backend, st);
}

ue_status UE_SDK_CALL ue_backend_open_image(const void* image, size_t image_size,
                                            uint64_t image_base, const char* module_name,
                                            ue_memory_backend* out_backend) {
    if (!image || !image_size || !out_backend)
        return UE_ERR_INVALID_ARG;
    ue::ImageBackendState* st = nullptr;
    if (ue::image_backend_open(image, image_size, image_base, module_name, out_backend, &st) != 0)
        return UE_ERR_MEMORY;
    return UE_OK;
}

void UE_SDK_CALL ue_backend_close_image(ue_memory_backend* backend) {
    if (!backend)
        return;
    auto* st = static_cast<ue::ImageBackendState*>(backend->user);
    ue::image_backend_close(backend, st);
}

// ── Session ─────────────────────────────────────────────────────────────────

ue_status UE_SDK_CALL ue_session_create(const ue_memory_backend* backend, ue_session** out_session) {
    if (!g_inited)
        return UE_ERR_NOT_INIT;
    if (!backend || !backend->read || !out_session)
        return UE_ERR_INVALID_ARG;

    auto* s = new (std::nothrow) ue_session{ue::Memory(*backend)};
    if (!s)
        return UE_ERR_INTERNAL;

    // Default profile UE4.27
    s->version = {4, 27, 0};
    s->profile = ue::find_profile(s->version);
    if (s->profile)
        s->offsets = s->profile->offsets;
    s->rebuild_dumper();

    *out_session = s;
    return UE_OK;
}

void UE_SDK_CALL ue_session_destroy(ue_session* session) {
    if (!session)
        return;
    if (session->backend_kind == ue_session::BackendKind::Process && session->process_state)
        ue::process_backend_close(&session->owned_backend, session->process_state);
    if (session->backend_kind == ue_session::BackendKind::Image && session->image_state)
        ue::image_backend_close(&session->owned_backend, session->image_state);
    delete session;
}

ue_status UE_SDK_CALL ue_session_set_version(ue_session* session, ue_version version) {
    if (!session)
        return UE_ERR_INVALID_ARG;
    session->version = version;
    session->profile = ue::find_profile(version);
    if (!session->profile)
        return UE_ERR_VERSION;
    session->offsets = session->profile->offsets;
    session->rebuild_dumper();
    UE_LOG_I("Engine profile: %s (%d.%d.%d)", session->profile->name.c_str(),
             version.major, version.minor, version.patch);
    return UE_OK;
}

ue_status UE_SDK_CALL ue_session_detect_version(ue_session* session, ue_version* out_version) {
    if (!session || !out_version || !session->dumper)
        return UE_ERR_INVALID_ARG;
    ue_status st = session->dumper->detect_version(out_version);
    if (st == UE_OK) {
        session->version = *out_version;
        session->profile = ue::find_profile(*out_version);
        if (session->profile) {
            session->offsets = session->profile->offsets;
            session->rebuild_dumper();
        }
    }
    return st;
}

ue_status UE_SDK_CALL ue_session_set_offsets(ue_session* session, const ue_offsets* offsets) {
    if (!session || !offsets)
        return UE_ERR_INVALID_ARG;
    session->offsets = *offsets;
    session->rebuild_dumper();
    return UE_OK;
}

ue_status UE_SDK_CALL ue_session_get_offsets(const ue_session* session, ue_offsets* out_offsets) {
    if (!session || !out_offsets)
        return UE_ERR_INVALID_ARG;
    *out_offsets = session->offsets;
    return UE_OK;
}

ue_status UE_SDK_CALL ue_session_set_globals(ue_session* session, const ue_globals* globals) {
    if (!session || !globals)
        return UE_ERR_INVALID_ARG;
    session->globals = *globals;
    session->rebuild_dumper();
    return UE_OK;
}

ue_status UE_SDK_CALL ue_session_get_globals(const ue_session* session, ue_globals* out_globals) {
    if (!session || !out_globals)
        return UE_ERR_INVALID_ARG;
    *out_globals = session->globals;
    return UE_OK;
}

ue_status UE_SDK_CALL ue_session_find_globals(ue_session* session, const char* module_name) {
    if (!session || !session->dumper)
        return UE_ERR_INVALID_ARG;
    session->dumper->set_profile(session->profile);
    ue_status st = session->dumper->find_globals(module_name ? module_name : "");
    session->globals = session->dumper->globals();
    return st;
}

ue_status UE_SDK_CALL ue_session_get_name(ue_session* session, int32_t comparison_index,
                                          int32_t number, char* buffer, size_t buffer_size) {
    if (!session || !session->dumper || !buffer || buffer_size == 0)
        return UE_ERR_INVALID_ARG;
    auto name = session->dumper->names().get(comparison_index, number);
    if (name.empty()) {
        buffer[0] = '\0';
        return UE_ERR_NOT_FOUND;
    }
    std::snprintf(buffer, buffer_size, "%s", name.c_str());
    return UE_OK;
}

ue_status UE_SDK_CALL ue_session_dump(ue_session* session, const ue_dump_options* options,
                                      ue_dump_stats* out_stats) {
    if (!session || !session->dumper || !options || !options->output_dir)
        return UE_ERR_INVALID_ARG;

    ue_dump_options opts = *options;
    if (opts.formats == 0)
        opts.formats = UE_FMT_ALL;
    // Defaults if caller zeroed flags incorrectly
    if (!opts.generate_packages && !opts.generate_structs && !opts.generate_classes &&
        !opts.generate_enums && !opts.generate_functions) {
        opts.generate_packages  = 1;
        opts.generate_structs   = 1;
        opts.generate_classes   = 1;
        opts.generate_enums     = 1;
        opts.generate_functions = 1;
    }

    std::error_code ec;
    fs::create_directories(opts.output_dir, ec);

    auto dump = session->dumper->dump(opts);
    session->globals = session->dumper->globals();

    int files = 0;
    if (opts.formats & UE_FMT_CPP_HEADERS) {
        ue::GenerateOptions go;
        go.output_dir = opts.output_dir;
        go.one_header_per_package = opts.one_header_per_package != 0;
        go.include_padding = opts.include_padding != 0;
        go.write_offsets_header = (opts.formats & UE_FMT_OFFSETS_H) != 0;
        int n = ue::generate_cpp_sdk(dump.packages, session->offsets, session->globals,
                                     session->version, go);
        if (n < 0)
            return UE_ERR_IO;
        files += n;
    } else if (opts.formats & UE_FMT_OFFSETS_H) {
        auto path = (fs::path(opts.output_dir) / "Offsets.hpp").string();
        if (ue::write_offsets_header(path, session->offsets, session->globals, session->version) < 0)
            return UE_ERR_IO;
        ++files;
    }

    if (opts.formats & UE_FMT_JSON) {
        auto path = (fs::path(opts.output_dir) / "sdk.json").string();
        if (ue::export_json(dump.packages, session->offsets, session->globals, session->version, path) < 0)
            return UE_ERR_IO;
        ++files;
    }

    dump.stats.files_written = static_cast<uint64_t>(files);
    if (out_stats)
        *out_stats = dump.stats;

    if (dump.stats.objects_scanned == 0 && session->globals.gobjects == 0)
        return UE_ERR_DUMP;
    return UE_OK;
}

ue_status UE_SDK_CALL ue_session_export_offsets_header(ue_session* session, const char* output_path) {
    if (!session || !output_path)
        return UE_ERR_INVALID_ARG;
    if (ue::write_offsets_header(output_path, session->offsets, session->globals, session->version) < 0)
        return UE_ERR_IO;
    return UE_OK;
}

// ── Version DB ──────────────────────────────────────────────────────────────

int UE_SDK_CALL ue_builtin_profile_count(void) {
    return static_cast<int>(ue::builtin_profiles().size());
}

ue_status UE_SDK_CALL ue_builtin_profile_info(int index, char* name_buf, size_t name_buf_size,
                                              ue_version* out_version) {
    const auto& all = ue::builtin_profiles();
    if (index < 0 || index >= static_cast<int>(all.size()))
        return UE_ERR_INVALID_ARG;
    const auto& p = all[static_cast<size_t>(index)];
    if (name_buf && name_buf_size)
        std::snprintf(name_buf, name_buf_size, "%s", p.name.c_str());
    if (out_version)
        *out_version = p.version;
    return UE_OK;
}

ue_status UE_SDK_CALL ue_offsets_for_version(ue_version version, ue_offsets* out_offsets) {
    if (!out_offsets)
        return UE_ERR_INVALID_ARG;
    bool found = false;
    *out_offsets = ue::offsets_for_version(version, &found);
    return found ? UE_OK : UE_ERR_VERSION;
}

void UE_SDK_CALL ue_dump_options_init(ue_dump_options* options) {
    if (!options)
        return;
    std::memset(options, 0, sizeof(*options));
    options->formats                 = UE_FMT_ALL;
    options->generate_packages       = 1;
    options->generate_structs        = 1;
    options->generate_classes        = 1;
    options->generate_enums          = 1;
    options->generate_functions      = 1;
    options->one_header_per_package  = 1;
    options->include_padding         = 1;
    options->filter_engine_only      = 0;
    options->max_objects             = 0;
}
