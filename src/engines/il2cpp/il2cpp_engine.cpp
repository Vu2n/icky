#include "engine/engine_iface.h"
#include "engines/il2cpp/metadata.h"
#include "engines/il2cpp/string_decrypt.h"
#include "core/logger.h"
#include "core/modules.h"
#include "core/pattern.h"
#include "core/memory.h"

#include <algorithm>

namespace icky {
namespace {

class Il2CppEngine final : public IEngine {
public:
    icky_engine id() const override { return ICKY_ENGINE_IL2CPP; }
    const char* name() const override { return "IL2CPP"; }

    DetectResult detect() const override {
        DetectResult d;
        auto ga = find_module("GameAssembly.dll");
        if (!ga) ga = find_module("GameAssembly");
        auto up = find_module("UnityPlayer");
        if (ga) {
            d.matched = true;
            d.primary = *ga;
            d.confidence = 0.98f;
            d.detail = "GameAssembly.dll";
            if (up) d.detail += " + UnityPlayer";
            if (get_export(ga->base, "il2cpp_domain_get") ||
                get_export(ga->base, "il2cpp_class_from_name")) {
                d.confidence = 1.0f;
                d.detail += " (il2cpp exports)";
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

        auto meta = il2cpp::load_global_metadata();
        if (meta) {
            return il2cpp::dump_from_metadata(*meta, det.primary.base, det.primary.size, out);
        }

        out.engine = ICKY_ENGINE_IL2CPP;
        out.engine_detail = "IL2CPP (exports only — metadata not found)";
        out.primary_module = {det.primary.name, det.primary.base, det.primary.size};

        const char* exports[] = {
            "il2cpp_domain_get", "il2cpp_domain_get_assemblies", "il2cpp_assembly_get_image",
            "il2cpp_image_get_class_count", "il2cpp_image_get_class", "il2cpp_class_get_name",
            "il2cpp_class_get_namespace", "il2cpp_class_get_fields", "il2cpp_class_get_methods",
            "il2cpp_field_get_name", "il2cpp_field_get_offset", "il2cpp_method_get_name",
            "il2cpp_method_get_param_count", "il2cpp_runtime_invoke", "il2cpp_string_new",
            "il2cpp_string_chars", "il2cpp_string_length", "il2cpp_thread_attach",
        };

        using domain_get_t = void* (*)();
        using domain_assemblies_t = void** (*)(void*, size_t*);
        using assembly_image_t = void* (*)(void*);
        using image_class_count_t = size_t (*)(void*);
        using image_class_t = void* (*)(void*, size_t);
        using class_get_name_t = const char* (*)(void*);
        using class_get_ns_t = const char* (*)(void*);
        using class_fields_t = void* (*)(void*, void**);
        using field_name_t = const char* (*)(void*);
        using field_offset_t = size_t (*)(void*);

        auto exp = [&](const char* n) -> uint64_t {
            uint64_t a = get_export(det.primary.base, n);
            if (a)
                out.globals.push_back({n, a, a - det.primary.base, "function", ""});
            return a;
        };

        for (auto* e : exports) exp(e);

        auto p_domain = reinterpret_cast<domain_get_t>(exp("il2cpp_domain_get"));
        auto p_asms   = reinterpret_cast<domain_assemblies_t>(get_export(det.primary.base, "il2cpp_domain_get_assemblies"));
        auto p_img    = reinterpret_cast<assembly_image_t>(get_export(det.primary.base, "il2cpp_assembly_get_image"));
        auto p_count  = reinterpret_cast<image_class_count_t>(get_export(det.primary.base, "il2cpp_image_get_class_count"));
        auto p_class  = reinterpret_cast<image_class_t>(get_export(det.primary.base, "il2cpp_image_get_class"));
        auto p_name   = reinterpret_cast<class_get_name_t>(get_export(det.primary.base, "il2cpp_class_get_name"));
        auto p_ns     = reinterpret_cast<class_get_ns_t>(get_export(det.primary.base, "il2cpp_class_get_namespace"));
        auto p_fields = reinterpret_cast<class_fields_t>(get_export(det.primary.base, "il2cpp_class_get_fields"));
        auto p_fname  = reinterpret_cast<field_name_t>(get_export(det.primary.base, "il2cpp_field_get_name"));
        auto p_foff   = reinterpret_cast<field_offset_t>(get_export(det.primary.base, "il2cpp_field_get_offset"));

        if (p_domain && p_asms && p_img && p_count && p_class && p_name) {
            void* domain = p_domain();
            if (domain) {
                size_t nasm = 0;
                void** asms = p_asms(domain, &nasm);
                ILOG_I("IL2CPP domain assemblies: %zu", nasm);
                if (asms && nasm > 0 && nasm < 10000) {
                    for (size_t ai = 0; ai < nasm && ai < 64; ++ai) {
                        if (!asms[ai]) continue;
                        void* image = p_img(asms[ai]);
                        if (!image) continue;
                        size_t cc = p_count(image);
                        if (cc > 500000) continue;
                        for (size_t ci = 0; ci < cc && ci < 5000; ++ci) {
                            void* klass = p_class(image, ci);
                            if (!klass) continue;
                            SdkType t;
                            t.kind = TypeKind::Class;
                            t.address = reinterpret_cast<uint64_t>(klass);
                            const char* nm = p_name(klass);
                            const char* ns = p_ns ? p_ns(klass) : nullptr;
                            if (nm && is_readable(nm, 1)) t.name = nm;
                            if (ns && is_readable(ns, 1)) t.ns = ns;
                            t.full_name = t.ns.empty() ? t.name : t.ns + "." + t.name;
                            if (t.name.empty()) continue;

                            if (p_fields && p_fname && p_foff) {
                                void* iter = nullptr;
                                while (void* field = p_fields(klass, &iter)) {
                                    SdkField f;
                                    const char* fn = p_fname(field);
                                    if (fn && is_readable(fn, 1)) f.name = fn;
                                    f.offset = static_cast<int32_t>(p_foff(field));
                                    f.type_name = "unknown";
                                    if (!f.name.empty())
                                        t.fields.push_back(std::move(f));
                                    if (t.fields.size() > 256) break;
                                }
                            }
                            out.types.push_back(std::move(t));
                            if (out.types.size() > 30000) break;
                        }
                    }
                }
            }
        }

        il2cpp::StringDecryptor dec;
        dec.init(det.primary.base, det.primary.size);
        auto bytes = Mem::bytes(det.primary.base, std::min<size_t>(det.primary.size, 4 * 1024 * 1024));
        out.decrypted_strings = dec.decrypt_literal_samples(bytes, 32);

        return !out.types.empty() || !out.globals.empty();
    }
};

} // namespace

EnginePtr create_il2cpp_engine() {
    return std::make_unique<Il2CppEngine>();
}

} // namespace icky
