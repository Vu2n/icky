/**
 * Example host for ue_sdk_gen.dll
 *
 * Usage:
 *   ue_sdk_example list-profiles
 *   ue_sdk_example dump --pid <pid> --module Game.exe --version 5.3 --out ./sdk_out
 *   ue_sdk_example dump --pid <pid> --module Game.exe --gobjects 0x... --gnames 0x... --out ./sdk
 */

#define _CRT_SECURE_NO_WARNINGS
#include <ue_sdk/ue_sdk.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static void log_cb(ue_log_level level, const char* message, void*) {
    const char* tag = "INFO";
    switch (level) {
    case UE_LOG_TRACE: tag = "TRACE"; break;
    case UE_LOG_DEBUG: tag = "DEBUG"; break;
    case UE_LOG_INFO:  tag = "INFO";  break;
    case UE_LOG_WARN:  tag = "WARN";  break;
    case UE_LOG_ERROR: tag = "ERROR"; break;
    default: break;
    }
    std::fprintf(stderr, "[%s] %s\n", tag, message);
}

static void print_usage(const char* argv0) {
    std::printf(
        "ue_sdk_gen example host\n"
        "\n"
        "Commands:\n"
        "  list-profiles\n"
        "  dump --pid <pid> --module <name.exe|dll> --out <dir> [options]\n"
        "\n"
        "Options:\n"
        "  --version M.m[.p]   Engine profile (default 4.27)\n"
        "  --gobjects 0xHEX    Skip scan for GObjects\n"
        "  --gnames   0xHEX    Skip scan for GNames\n"
        "  --gworld   0xHEX\n"
        "  --process-event 0xHEX\n"
        "  --json-only         Write only sdk.json\n"
        "  --offsets-only      Write only Offsets.hpp\n"
        "  --engine-only       Filter to engine packages\n"
        "  --max-objects N     Cap GObject walk\n"
        "  --verbose\n"
        "\n"
        "Example:\n"
        "  %s dump --pid 1234 --module MyGame-Win64-Shipping.exe --version 5.1 --out .\\sdk\n",
        argv0);
}

static bool parse_u64(const char* s, uint64_t* out) {
    if (!s || !out)
        return false;
    char* end = nullptr;
    *out = std::strtoull(s, &end, 0);
    return end && end != s;
}

static bool parse_version(const char* s, ue_version* v) {
    if (!s || !v)
        return false;
    int a = 0, b = 0, c = 0;
    const int n = std::sscanf(s, "%d.%d.%d", &a, &b, &c);
    if (n < 2)
        return false;
    v->major = a;
    v->minor = b;
    v->patch = (n >= 3) ? c : 0;
    return true;
}

static int cmd_list_profiles() {
    const int n = ue_builtin_profile_count();
    std::printf("Built-in engine profiles (%d):\n", n);
    for (int i = 0; i < n; ++i) {
        char name[64];
        ue_version ver{};
        if (ue_builtin_profile_info(i, name, sizeof(name), &ver) == UE_OK)
            std::printf("  [%2d] %-10s  %d.%d.%d\n", i, name, ver.major, ver.minor, ver.patch);
    }
    return 0;
}

static int cmd_dump(int argc, char** argv) {
    uint32_t pid = 0;
    const char* module_name = nullptr;
    const char* out_dir = nullptr;
    ue_version ver{4, 27, 0};
    bool have_version = false;
    ue_globals g{};
    bool json_only = false, offsets_only = false, engine_only = false;
    int max_objects = 0;

    for (int i = 0; i < argc; ++i) {
        auto need = [&](const char* opt) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "Missing value for %s\n", opt);
                return nullptr;
            }
            return argv[++i];
        };

        if (std::strcmp(argv[i], "--pid") == 0) {
            const char* v = need("--pid");
            if (!v) return 2;
            pid = static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
        } else if (std::strcmp(argv[i], "--module") == 0) {
            module_name = need("--module");
            if (!module_name) return 2;
        } else if (std::strcmp(argv[i], "--out") == 0) {
            out_dir = need("--out");
            if (!out_dir) return 2;
        } else if (std::strcmp(argv[i], "--version") == 0) {
            const char* v = need("--version");
            if (!v || !parse_version(v, &ver)) {
                std::fprintf(stderr, "Bad --version\n");
                return 2;
            }
            have_version = true;
        } else if (std::strcmp(argv[i], "--gobjects") == 0) {
            const char* v = need("--gobjects");
            if (!v || !parse_u64(v, &g.gobjects)) return 2;
        } else if (std::strcmp(argv[i], "--gnames") == 0) {
            const char* v = need("--gnames");
            if (!v || !parse_u64(v, &g.gnames)) return 2;
        } else if (std::strcmp(argv[i], "--gworld") == 0) {
            const char* v = need("--gworld");
            if (!v || !parse_u64(v, &g.gworld)) return 2;
        } else if (std::strcmp(argv[i], "--process-event") == 0) {
            const char* v = need("--process-event");
            if (!v || !parse_u64(v, &g.process_event)) return 2;
        } else if (std::strcmp(argv[i], "--json-only") == 0) {
            json_only = true;
        } else if (std::strcmp(argv[i], "--offsets-only") == 0) {
            offsets_only = true;
        } else if (std::strcmp(argv[i], "--engine-only") == 0) {
            engine_only = true;
        } else if (std::strcmp(argv[i], "--max-objects") == 0) {
            const char* v = need("--max-objects");
            if (!v) return 2;
            max_objects = std::atoi(v);
        } else if (std::strcmp(argv[i], "--verbose") == 0) {
            ue_set_log_level(UE_LOG_DEBUG);
        } else {
            std::fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 2;
        }
    }

    if (!pid || !module_name || !out_dir) {
        std::fprintf(stderr, "dump requires --pid, --module, and --out\n");
        return 2;
    }

    ue_memory_backend backend{};
    ue_status st = ue_backend_open_process(pid, &backend);
    if (st != UE_OK) {
        std::fprintf(stderr, "Failed to open process %u: %s\n", pid, ue_status_string(st));
        return 1;
    }

    ue_session* session = nullptr;
    st = ue_session_create(&backend, &session);
    if (st != UE_OK) {
        std::fprintf(stderr, "session_create: %s\n", ue_status_string(st));
        ue_backend_close_process(&backend);
        return 1;
    }

    if (have_version) {
        st = ue_session_set_version(session, ver);
        if (st != UE_OK)
            std::fprintf(stderr, "Warning: version profile: %s (using nearest)\n",
                         ue_status_string(st));
    }

    if (g.gobjects || g.gnames || g.gworld || g.process_event)
        ue_session_set_globals(session, &g);

    if (!g.gobjects || !g.gnames) {
        st = ue_session_find_globals(session, module_name);
        if (st != UE_OK)
            std::fprintf(stderr, "Warning: find_globals: %s (provide --gobjects/--gnames)\n",
                         ue_status_string(st));
    }

    ue_globals resolved{};
    ue_session_get_globals(session, &resolved);
    std::printf("Globals:\n");
    std::printf("  GObjects     = 0x%llX\n", (unsigned long long)resolved.gobjects);
    std::printf("  GNames       = 0x%llX\n", (unsigned long long)resolved.gnames);
    std::printf("  GWorld       = 0x%llX\n", (unsigned long long)resolved.gworld);
    std::printf("  ProcessEvent = 0x%llX\n", (unsigned long long)resolved.process_event);

    ue_dump_options opts{};
    ue_dump_options_init(&opts);
    opts.output_dir = out_dir;
    opts.filter_engine_only = engine_only ? 1 : 0;
    opts.max_objects = max_objects;
    if (json_only)
        opts.formats = UE_FMT_JSON;
    else if (offsets_only)
        opts.formats = UE_FMT_OFFSETS_H;

    ue_dump_stats stats{};
    st = ue_session_dump(session, &opts, &stats);
    if (st != UE_OK) {
        std::fprintf(stderr, "dump failed: %s\n", ue_status_string(st));
        ue_session_destroy(session);
        ue_backend_close_process(&backend);
        return 1;
    }

    std::printf("\nDump complete → %s\n", out_dir);
    std::printf("  objects scanned : %llu\n", (unsigned long long)stats.objects_scanned);
    std::printf("  packages        : %llu\n", (unsigned long long)stats.packages);
    std::printf("  classes         : %llu\n", (unsigned long long)stats.classes);
    std::printf("  structs         : %llu\n", (unsigned long long)stats.structs);
    std::printf("  enums           : %llu\n", (unsigned long long)stats.enums);
    std::printf("  functions       : %llu\n", (unsigned long long)stats.functions);
    std::printf("  files written   : %llu\n", (unsigned long long)stats.files_written);
    std::printf("  elapsed         : %.2f s\n", stats.elapsed_seconds);

    ue_session_destroy(session);
    ue_backend_close_process(&backend);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 2;
    }

    ue_set_log_callback(log_cb, nullptr);
    ue_set_log_level(UE_LOG_INFO);

    ue_status st = ue_init();
    if (st != UE_OK && st != UE_ERR_ALREADY_INIT) {
        std::fprintf(stderr, "ue_init failed: %s\n", ue_status_string(st));
        return 1;
    }

    int rc = 2;
    if (std::strcmp(argv[1], "list-profiles") == 0) {
        rc = cmd_list_profiles();
    } else if (std::strcmp(argv[1], "dump") == 0) {
        rc = cmd_dump(argc - 2, argv + 2);
    } else if (std::strcmp(argv[1], "-h") == 0 || std::strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        rc = 0;
    } else {
        print_usage(argv[0]);
        rc = 2;
    }

    ue_shutdown();
    return rc;
}
