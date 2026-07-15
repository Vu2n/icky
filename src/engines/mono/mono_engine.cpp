#include "engine/engine_iface.h"
#include "engines/mono/string_decrypt.h"
#include "core/logger.h"
#include "core/modules.h"
#include "core/pattern.h"
#include "core/memory.h"

namespace icky {
namespace {

const char* kMonoNames[] = {
    "mono-2.0-bdwgc.dll",
    "mono-2.0-sgen.dll",
    "mono.dll",
    "libmono.so", // wine etc.
};

class MonoEngine final : public IEngine {
public:
    icky_engine id() const override { return ICKY_ENGINE_MONO; }
    const char* name() const override { return "Mono"; }

    DetectResult detect() const override {
        DetectResult d;
        // Prefer Mono over bare UnityPlayer when GameAssembly missing
        if (find_module("GameAssembly")) {
            d.detail = "GameAssembly present (IL2CPP wins)";
            return d;
        }
        for (auto* n : kMonoNames) {
            auto m = find_module(n);
            if (!m) m = find_module("mono-2.0");
            if (!m) continue;
            d.matched = true;
            d.primary = *m;
            d.confidence = 0.95f;
            d.detail = m->name;
            if (get_export(m->base, "mono_get_root_domain") ||
                get_export(m->base, "mono_domain_get")) {
                d.confidence = 1.0f;
                d.detail += " (mono exports)";
            }
            return d;
        }
        // Unity mono: UnityPlayer + mono embed
        if (auto up = find_module("UnityPlayer")) {
            if (find_module("mono")) {
                d.matched = true;
                d.primary = *up;
                d.confidence = 0.7f;
                d.detail = "UnityPlayer + mono";
                return d;
            }
        }
        d.detail = "no mono module";
        return d;
    }

    bool dump(SdkModel& out) override {
        auto det = detect();
        if (!det.matched) return false;

        auto mono = det.primary;
        // Prefer actual mono DLL for exports
        for (auto* n : kMonoNames) {
            if (auto m = find_module(n)) {
                mono = *m;
                break;
            }
        }
        if (auto m = find_module("mono-2.0")) mono = *m;

        out.engine = ICKY_ENGINE_MONO;
        out.engine_detail = "Mono / Unity Mono";
        out.primary_module = {mono.name, mono.base, mono.size};

        auto exp = [&](const char* n) -> uint64_t {
            uint64_t a = get_export(mono.base, n);
            if (a)
                out.globals.push_back({n, a, a - mono.base, "function", ""});
            return a;
        };

        const char* exports[] = {
            "mono_get_root_domain", "mono_domain_get", "mono_domain_assembly_open",
            "mono_assembly_get_image", "mono_image_get_name", "mono_class_from_name",
            "mono_class_get_name", "mono_class_get_namespace", "mono_class_get_fields",
            "mono_class_get_methods", "mono_field_get_name", "mono_field_get_offset",
            "mono_method_get_name", "mono_runtime_invoke", "mono_string_to_utf8",
            "mono_string_new", "mono_thread_attach", "mono_object_get_class",
            "mono_table_info_get_rows", "mono_metadata_string_heap",
            "mono_image_get_table_info",
        };
        for (auto* e : exports) exp(e);

        using domain_get_t = void* (*)();
        using assembly_foreach_t = void (*)(void (*)(void*, void*), void*);
        // Walk via mono_domain_get + mono_domain_foreach assemblies is complex;
        // use mono_assembly_foreach if present.

        using class_from_name_t = void* (*)(void* image, const char* ns, const char* name);
        using image_loaded_t = void* (*)(const char* name);
        using class_get_name_t = const char* (*)(void*);
        using class_get_ns_t = const char* (*)(void*);
        using class_fields_t = void* (*)(void*, void**);
        using field_name_t = const char* (*)(void*);
        using field_offset_t = int (*)(void*);
        using class_methods_t = void* (*)(void*, void**);
        using method_name_t = const char* (*)(void*);

        auto p_domain = reinterpret_cast<domain_get_t>(
            get_export(mono.base, "mono_get_root_domain"));
        if (!p_domain)
            p_domain = reinterpret_cast<domain_get_t>(get_export(mono.base, "mono_domain_get"));

        auto p_image_loaded = reinterpret_cast<image_loaded_t>(
            get_export(mono.base, "mono_image_loaded"));
        auto p_class_name = reinterpret_cast<class_get_name_t>(
            get_export(mono.base, "mono_class_get_name"));
        auto p_class_ns = reinterpret_cast<class_get_ns_t>(
            get_export(mono.base, "mono_class_get_namespace"));
        auto p_fields = reinterpret_cast<class_fields_t>(
            get_export(mono.base, "mono_class_get_fields"));
        auto p_fname = reinterpret_cast<field_name_t>(
            get_export(mono.base, "mono_field_get_name"));
        auto p_foff = reinterpret_cast<field_offset_t>(
            get_export(mono.base, "mono_field_get_offset"));
        auto p_methods = reinterpret_cast<class_methods_t>(
            get_export(mono.base, "mono_class_get_methods"));
        auto p_mname = reinterpret_cast<method_name_t>(
            get_export(mono.base, "mono_method_get_name"));
        auto p_class_from = reinterpret_cast<class_from_name_t>(
            get_export(mono.base, "mono_class_from_name"));

        // Attach thread if possible
        using thread_attach_t = void* (*)(void*);
        auto p_attach = reinterpret_cast<thread_attach_t>(
            get_export(mono.base, "mono_thread_attach"));
        if (p_domain && p_attach) {
            {
                void* dom = p_domain();
                if (dom) p_attach(dom);
            } 
        }

        // Dump well-known corlib types as seed + any loaded images we can open
        const char* images[] = {
            "mscorlib", "Assembly-CSharp", "Assembly-CSharp-firstpass",
            "UnityEngine", "UnityEngine.CoreModule", "System",
        };

        if (p_image_loaded && p_class_from && p_class_name) {
            {
                for (auto* img_name : images) {
                    void* image = p_image_loaded(img_name);
                    if (!image) continue;
                    ILOG_I("Mono image loaded: %s", img_name);

                    // We don't have easy class iteration without mono_image_get_table_info;
                    // dump marker type for the assembly
                    SdkType asm_t;
                    asm_t.kind = TypeKind::Namespace;
                    asm_t.name = img_name;
                    asm_t.ns = "Assembly";
                    asm_t.comment = "Loaded mono image";
                    out.types.push_back(std::move(asm_t));
                }

                // If Assembly-CSharp is loaded, try mono_class_get with table walk
                using table_info_t = void* (*)(void*, int);
                using table_rows_t = int (*)(const void*);
                auto p_table = reinterpret_cast<table_info_t>(
                    get_export(mono.base, "mono_image_get_table_info"));
                auto p_rows = reinterpret_cast<table_rows_t>(
                    get_export(mono.base, "mono_table_info_get_rows"));
                using metadata_string_t = const char* (*)(void*, uint32_t);
                auto p_str = reinterpret_cast<metadata_string_t>(
                    get_export(mono.base, "mono_metadata_string_heap"));
                using class_get_t = void* (*)(void*, uint32_t);
                auto p_class_get = reinterpret_cast<class_get_t>(
                    get_export(mono.base, "mono_class_get"));

                // MONO_TABLE_TYPEDEF = 2
                for (auto* img_name : images) {
                    void* image = p_image_loaded ? p_image_loaded(img_name) : nullptr;
                    if (!image || !p_table || !p_rows) continue;
                    void* tinfo = p_table(image, 2);
                    if (!tinfo) continue;
                    int rows = p_rows(tinfo);
                    ILOG_I("Mono %s TYPEDEF rows=%d", img_name, rows);
                    // Without full metadata decode, use mono_class_get if available
                    if (p_class_get && p_class_name) {
                        for (int i = 1; i < rows && i < 8000; ++i) {
                            // token = table<<24 | row
                            uint32_t token = (2u << 24) | static_cast<uint32_t>(i);
                            void* klass = p_class_get(image, token);
                            if (!klass) continue;
                            SdkType t;
                            t.kind = TypeKind::Class;
                            t.address = reinterpret_cast<uint64_t>(klass);
                            const char* nm = p_class_name(klass);
                            const char* ns = p_class_ns ? p_class_ns(klass) : nullptr;
                            t.name = nm ? nm : "";
                            t.ns = ns ? ns : "";
                            t.full_name = t.ns.empty() ? t.name : t.ns + "." + t.name;
                            t.comment = img_name;
                            if (t.name.empty()) continue;

                            if (p_fields && p_fname) {
                                void* iter = nullptr;
                                while (void* field = p_fields(klass, &iter)) {
                                    SdkField f;
                                    const char* fn = p_fname(field);
                                    f.name = fn ? fn : "";
                                    if (p_foff) f.offset = p_foff(field);
                                    f.type_name = "unknown";
                                    if (!f.name.empty()) t.fields.push_back(std::move(f));
                                    if (t.fields.size() > 128) break;
                                }
                            }
                            if (p_methods && p_mname) {
                                void* iter = nullptr;
                                while (void* method = p_methods(klass, &iter)) {
                                    SdkMethod m;
                                    const char* mn = p_mname(method);
                                    m.name = mn ? mn : "";
                                    if (!m.name.empty()) t.methods.push_back(std::move(m));
                                    if (t.methods.size() > 256) break;
                                }
                            }
                            out.types.push_back(std::move(t));
                        }
                    }
                    (void)p_str;
                }
            } 
        }

        mono_str::MonoStringApi sa;
        sa.init(mono.base);
        auto img = Mem::bytes(mono.base, std::min<size_t>(mono.size, 2 * 1024 * 1024));
        if (!img.empty())
            out.decrypted_strings = sa.sample_decrypted(img.data(), img.size(), 32);

        ILOG_I("Mono dump: %zu types, %zu globals", out.types.size(), out.globals.size());
        return !out.types.empty() || !out.globals.empty();
    }
};

} // namespace

EnginePtr create_mono_engine() {
    return std::make_unique<MonoEngine>();
}

} // namespace icky
