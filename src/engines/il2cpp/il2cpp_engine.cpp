#include "engine/engine_iface.h"
#include "engines/il2cpp/metadata.h"
#include "engines/il2cpp/memory_meta.h"
#include "engines/il2cpp/runtime_dump.h"
#include "engines/il2cpp/deobf.h"
#include "engines/il2cpp/decrypt_recover.h"
#include "engines/il2cpp/string_decrypt.h"
#include "core/logger.h"
#include "core/modules.h"
#include "core/pattern.h"
#include "core/memory.h"
#include <algorithm>
#include <Windows.h>

namespace icky {
namespace {

class Il2CppEngine final : public IEngine {
public:
    icky_engine id() const override { return ICKY_ENGINE_IL2CPP; }
    const char* name() const override { return "IL2CPP"; }

    DetectResult detect() const override {
        DetectResult d;
        // Module name only — do not call any game code during detect
        auto ga = find_module("GameAssembly.dll");
        if (!ga) ga = find_module("GameAssembly");
        auto up = find_module("UnityPlayer");
        if (ga) {
            d.matched = true;
            d.primary = *ga;
            d.confidence = 0.98f;
            d.detail = ga->name;
            if (up) d.detail += " + UnityPlayer";
            if (get_export(ga->base, "il2cpp_domain_get") ||
                get_export(ga->base, "il2cpp_init") ||
                get_export(ga->base, "il2cpp_class_from_name")) {
                d.confidence = 1.0f;
                d.detail += " (il2cpp exports)";
            } else {
                d.detail += " (exports stripped/obfuscated — common on Rust)";
                d.confidence = 0.9f;
            }
            return d;
        }
        if (up) {
            d.matched = true;
            d.primary = *up;
            d.confidence = 0.4f;
            d.detail = "UnityPlayer without GameAssembly (maybe Mono)";
        } else {
            d.detail = "no GameAssembly";
        }
        return d;
    }

    bool dump(SdkModel& out) override {
        auto det = detect();
        if (!det.matched || !det.primary.base) {
            ILOG_E("IL2CPP not present");
            return false;
        }

        ILOG_I("IL2CPP dump: module=%s base=0x%llX size=0x%zX",
               det.primary.name.c_str(),
               (unsigned long long)det.primary.base, det.primary.size);

        const uint64_t ga_base = det.primary.base;
        const size_t   ga_size = det.primary.size;
        bool got_types = false;
        // Preserve process name — sub-dumps assign a fresh SdkModel and would wipe it
        // (otherwise game.slug becomes "gameassembly-dll" and catalog updates break).
        const std::string keep_game = out.game_name;

        auto adopt = [&](SdkModel&& m) {
            const auto g = keep_game.empty() ? out.game_name : keep_game;
            out = std::move(m);
            if (!g.empty())
                out.game_name = g;
        };

        // ──────────────────────────────────────────────────────────────
        // 1) Runtime walk (Testing\Icky style) — BEST for encrypted games.
        //    Names/fields/methods come from live IL2CPP which has already
        //    decrypted metadata. Dump only after the game has fully loaded
        //    (console "Enter to dump"), same as Icky's RSHIFT dump.
        // ──────────────────────────────────────────────────────────────
        if (get_export(ga_base, "il2cpp_domain_get") &&
            get_export(ga_base, "il2cpp_domain_get_assemblies") &&
            get_export(ga_base, "il2cpp_image_get_class")) {
            ILOG_I("Trying runtime class walk (decrypted names via API)...");
            SdkModel runtime;
            if (il2cpp::dump_from_runtime(ga_base, ga_size, runtime) &&
                !runtime.types.empty()) {
                adopt(std::move(runtime));
                got_types = true;
                ILOG_I("Runtime walk OK: %zu types", out.types.size());
            } else {
                ILOG_W("Runtime walk failed or empty — trying memory metadata");
            }
        } else {
            ILOG_W("Required domain exports missing — skip runtime walk");
        }

        // ──────────────────────────────────────────────────────────────
        // 2) Scan process memory for decrypted global-metadata.dat buffer
        //    (magic 0xFAB11BAF in private heap — Testing\Icky MetadataDump)
        // ──────────────────────────────────────────────────────────────
        if (!got_types) {
            auto mem = il2cpp::scan_decrypted_metadata_in_memory();
            if (mem) {
                ILOG_I("Using in-memory decrypted metadata (%zu bytes)", mem->data.size());
                SdkModel m;
                if (il2cpp::dump_from_metadata(*mem, ga_base, ga_size, m) &&
                    !m.types.empty()) {
                    adopt(std::move(m));
                    out.metadata["dump_source"] = "memory_metadata";
                    got_types = true;
                }
            }
        }

        // ──────────────────────────────────────────────────────────────
        // 3) Disk global-metadata.dat — only if names pass quality check.
        //    Rust often has valid magic/version but encrypted string heaps
        //    that produce garbage type names (what you saw before).
        // ──────────────────────────────────────────────────────────────
        if (!got_types) {
            auto meta = il2cpp::load_global_metadata();
            if (meta) {
                ILOG_I("Disk metadata: encrypted=%d version=%d path=%s",
                       meta->was_encrypted ? 1 : 0, meta->version, meta->path.c_str());
                if (il2cpp::metadata_names_usable(*meta)) {
                    SdkModel m;
                    if (il2cpp::dump_from_metadata(*meta, ga_base, ga_size, m) &&
                        !m.types.empty()) {
                        adopt(std::move(m));
                        out.metadata["dump_source"] = "disk_metadata";
                        got_types = true;
                    }
                } else {
                    ILOG_W("Disk metadata names look encrypted/garbage — NOT using "
                           "(this was the bad dump path). Use runtime or memory scan.");
                    out.metadata["disk_metadata_rejected"] = "name_quality_low";
                    out.metadata["disk_metadata_path"] = meta->path;
                }
            } else {
                ILOG_W("global-metadata.dat not found near executable");
            }
        }

        // ──────────────────────────────────────────────────────────────
        // 4) Always keep export RVAs; if no types, export-only package.
        // ──────────────────────────────────────────────────────────────
        if (!got_types) {
            out.engine = ICKY_ENGINE_IL2CPP;
            out.engine_detail = "IL2CPP export map only (no usable type source)";
            out.primary_module = {det.primary.name, ga_base, ga_size};
            out.metadata["note"] =
                "Could not get real types. Ensure the game is fully loaded, then dump again. "
                "Runtime walk needs domain exports; memory scan needs decrypted metadata in heap.";
        }

        il2cpp::append_il2cpp_exports(ga_base, ga_size, out);

        // String samples from module image (no game calls)
        {
            il2cpp::StringDecryptor dec;
            dec.init(ga_base, ga_size);
            const size_t sample = std::min<size_t>(ga_size, 2 * 1024 * 1024);
            auto bytes = Mem::bytes(ga_base, sample);
            if (!bytes.empty()) {
                auto more = dec.decrypt_literal_samples(bytes, 24);
                out.decrypted_strings.insert(out.decrypted_strings.end(),
                                             more.begin(), more.end());
            }
        }

        // ──────────────────────────────────────────────────────────────
        // Name recovery (Facepunch %sha1… is one-way — recover usable names
        // via types, Rust layout rules, getters, and code string xrefs).
        // ──────────────────────────────────────────────────────────────
        if (got_types && out.types.size() > 10) {
            ILOG_I("Running multi-pass deobfuscation (may take a while)…");
            il2cpp::DeobfOptions dopt;
            dopt.structural = true;
            dopt.semantic_rust = true;
            dopt.getter_heuristic = true;
            dopt.string_xref = true;
            dopt.method_scan_bytes = 0x200;
            // Full scan — quality over speed (user OK with long dumps)
            dopt.max_string_xref_methods = 0;

            auto dst = il2cpp::deobfuscate_sdk(out, ga_base, ga_size, dopt);
            out.metadata["deobf_done"] = "1";

            // Extra artifacts written next to SDK package by writer if paths set;
            // also stash stats for summary.json consumers.
            char buf[64];
            sprintf_s(buf, "%zu", dst.semantic_hits);
            out.metadata["deobf_semantic"] = buf;
            sprintf_s(buf, "%zu", dst.string_xref_hits);
            out.metadata["deobf_string_xref"] = buf;
            sprintf_s(buf, "%zu", dst.renamed_fields);
            out.metadata["deobf_renamed_fields"] = buf;
            sprintf_s(buf, "%zu", dst.renamed_methods);
            out.metadata["deobf_renamed_methods"] = buf;

            // Encrypted field decrypt recovery (getter → decrypt stub + constants)
            ILOG_I("Recovering encrypted field decrypts…");
            auto dr = il2cpp::recover_field_decrypts(out, ga_base, ga_size);
            sprintf_s(buf, "%zu", dr.fields_annotated);
            out.metadata["decrypt_fields"] = buf;
            sprintf_s(buf, "%zu", dr.algos_recovered);
            out.metadata["decrypt_algos"] = buf;
            sprintf_s(buf, "%zu", dr.getters_found);
            out.metadata["decrypt_getters"] = buf;
            sprintf_s(buf, "%zu", dr.getters_rejected);
            out.metadata["decrypt_rejected"] = buf;
            sprintf_s(buf, "%zu", dr.fields_wrapper_only);
            out.metadata["decrypt_wrapper_only"] = buf;
        }

        if (!got_types) {
            SdkType tip;
            tip.kind = TypeKind::Namespace;
            tip.name = "IckyIl2CppHint";
            tip.ns = "Icky";
            tip.comment =
                "No real types. Load into main menu, wait for assemblies, dump again. "
                "Path: runtime API walk → memory FAB11BAF scan → disk (quality-gated).";
            out.types.push_back(std::move(tip));
        }

        ILOG_I("IL2CPP dump finished: types=%zu globals=%zu source=%s deobf=%s",
               out.types.size(), out.globals.size(),
               out.metadata.count("dump_source")
                   ? out.metadata["dump_source"].c_str()
                   : "exports_only",
               out.metadata.count("deobf_done") ? "yes" : "no");

        return got_types || !out.globals.empty();
    }
};

} // namespace

EnginePtr create_il2cpp_engine() {
    return std::make_unique<Il2CppEngine>();
}

} // namespace icky
