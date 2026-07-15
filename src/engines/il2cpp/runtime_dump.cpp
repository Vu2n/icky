#include "runtime_dump.h"
#include "memory_meta.h"
#include "core/logger.h"
#include "core/pattern.h"

#include <Windows.h>

#include <cstring>
#include <string>
#include <vector>

namespace icky::il2cpp {
namespace {

// ---- IL2CPP export ABI (opaque pointers) ----
using domain_get_t            = void* (*)();
using domain_get_assemblies_t = void** (*)(void* domain, size_t* size);
using assembly_get_image_t    = void* (*)(void* assembly);
using image_get_name_t        = const char* (*)(void* image);
using image_get_class_count_t = size_t (*)(void* image);
using image_get_class_t       = void* (*)(void* image, size_t index);
using class_get_name_t        = const char* (*)(void* klass);
using class_get_namespace_t   = const char* (*)(void* klass);
using class_get_parent_t      = void* (*)(void* klass);
using class_get_fields_t      = void* (*)(void* klass, void** iter);
using class_get_methods_t     = void* (*)(void* klass, void** iter);
using class_get_flags_t       = int (*)(const void* klass);
using class_is_enum_t         = bool (*)(const void* klass);
using class_is_valuetype_t    = bool (*)(const void* klass);
using class_is_interface_t    = bool (*)(const void* klass);
using field_get_name_t        = const char* (*)(void* field);
using field_get_offset_t      = size_t (*)(void* field);
using field_get_type_t        = void* (*)(void* field);
using field_get_flags_t       = int (*)(void* field);
using method_get_name_t       = const char* (*)(void* method);
using method_get_param_count_t = uint32_t (*)(void* method);
using method_get_param_t      = void* (*)(void* method, uint32_t index);
using method_get_param_name_t = const char* (*)(void* method, uint32_t index);
using method_get_return_type_t = void* (*)(void* method);
using method_get_flags_t      = uint32_t (*)(void* method, uint32_t* iflags);
using method_is_inflated_t    = bool (*)(void* method);
using method_is_instance_t    = bool (*)(void* method);
using type_get_name_t         = char* (*)(void* type);
using free_t                  = void (*)(void* ptr);
using thread_attach_t         = void* (*)(void* domain);
using thread_detach_t         = void (*)(void* thread);

struct Api {
    uint64_t module_base = 0;
    size_t   module_size = 0;

    domain_get_t            domain_get = nullptr;
    domain_get_assemblies_t domain_get_assemblies = nullptr;
    assembly_get_image_t    assembly_get_image = nullptr;
    image_get_name_t        image_get_name = nullptr;
    image_get_class_count_t image_get_class_count = nullptr;
    image_get_class_t       image_get_class = nullptr;
    class_get_name_t        class_get_name = nullptr;
    class_get_namespace_t   class_get_namespace = nullptr;
    class_get_parent_t      class_get_parent = nullptr;
    class_get_fields_t      class_get_fields = nullptr;
    class_get_methods_t     class_get_methods = nullptr;
    class_get_flags_t       class_get_flags = nullptr;
    class_is_enum_t         class_is_enum = nullptr;
    class_is_valuetype_t    class_is_valuetype = nullptr;
    class_is_interface_t    class_is_interface = nullptr;
    field_get_name_t        field_get_name = nullptr;
    field_get_offset_t      field_get_offset = nullptr;
    field_get_type_t        field_get_type = nullptr;
    field_get_flags_t       field_get_flags = nullptr;
    method_get_name_t       method_get_name = nullptr;
    method_get_param_count_t method_get_param_count = nullptr;
    method_get_param_t      method_get_param = nullptr;
    method_get_param_name_t method_get_param_name = nullptr;
    method_get_return_type_t method_get_return_type = nullptr;
    method_get_flags_t      method_get_flags = nullptr;
    method_is_inflated_t    method_is_inflated = nullptr;
    method_is_instance_t    method_is_instance = nullptr;
    type_get_name_t         type_get_name = nullptr;
    free_t                  free = nullptr;
    thread_attach_t         thread_attach = nullptr;
    thread_detach_t         thread_detach = nullptr;
};

template <typename T>
T bind(uint64_t base, const char* name) {
    return reinterpret_cast<T>(get_export(base, name));
}

bool bind_api(uint64_t base, size_t size, Api& a) {
    a = {};
    a.module_base = base;
    a.module_size = size;

    a.domain_get            = bind<domain_get_t>(base, "il2cpp_domain_get");
    a.domain_get_assemblies = bind<domain_get_assemblies_t>(base, "il2cpp_domain_get_assemblies");
    a.assembly_get_image    = bind<assembly_get_image_t>(base, "il2cpp_assembly_get_image");
    a.image_get_name        = bind<image_get_name_t>(base, "il2cpp_image_get_name");
    a.image_get_class_count = bind<image_get_class_count_t>(base, "il2cpp_image_get_class_count");
    a.image_get_class       = bind<image_get_class_t>(base, "il2cpp_image_get_class");
    a.class_get_name        = bind<class_get_name_t>(base, "il2cpp_class_get_name");
    a.class_get_namespace   = bind<class_get_namespace_t>(base, "il2cpp_class_get_namespace");
    a.class_get_parent      = bind<class_get_parent_t>(base, "il2cpp_class_get_parent");
    a.class_get_fields      = bind<class_get_fields_t>(base, "il2cpp_class_get_fields");
    a.class_get_methods     = bind<class_get_methods_t>(base, "il2cpp_class_get_methods");
    a.class_get_flags       = bind<class_get_flags_t>(base, "il2cpp_class_get_flags");
    a.class_is_enum         = bind<class_is_enum_t>(base, "il2cpp_class_is_enum");
    a.class_is_valuetype    = bind<class_is_valuetype_t>(base, "il2cpp_class_is_valuetype");
    a.class_is_interface    = bind<class_is_interface_t>(base, "il2cpp_class_is_interface");
    a.field_get_name        = bind<field_get_name_t>(base, "il2cpp_field_get_name");
    a.field_get_offset      = bind<field_get_offset_t>(base, "il2cpp_field_get_offset");
    a.field_get_type        = bind<field_get_type_t>(base, "il2cpp_field_get_type");
    a.field_get_flags       = bind<field_get_flags_t>(base, "il2cpp_field_get_flags");
    a.method_get_name       = bind<method_get_name_t>(base, "il2cpp_method_get_name");
    a.method_get_param_count = bind<method_get_param_count_t>(base, "il2cpp_method_get_param_count");
    a.method_get_param      = bind<method_get_param_t>(base, "il2cpp_method_get_param");
    a.method_get_param_name = bind<method_get_param_name_t>(base, "il2cpp_method_get_param_name");
    a.method_get_return_type = bind<method_get_return_type_t>(base, "il2cpp_method_get_return_type");
    a.method_get_flags      = bind<method_get_flags_t>(base, "il2cpp_method_get_flags");
    a.method_is_inflated    = bind<method_is_inflated_t>(base, "il2cpp_method_is_inflated");
    a.method_is_instance    = bind<method_is_instance_t>(base, "il2cpp_method_is_instance");
    a.type_get_name         = bind<type_get_name_t>(base, "il2cpp_type_get_name");
    a.free                  = bind<free_t>(base, "il2cpp_free");
    a.thread_attach         = bind<thread_attach_t>(base, "il2cpp_thread_attach");
    a.thread_detach         = bind<thread_detach_t>(base, "il2cpp_thread_detach");

    return a.domain_get && a.domain_get_assemblies && a.assembly_get_image &&
           a.image_get_class_count && a.image_get_class && a.class_get_name &&
           a.class_get_fields && a.class_get_methods && a.field_get_name &&
           a.method_get_name && a.thread_attach;
}

bool is_system_image(const char* name) {
    if (!name)
        return true;
    static const char* kPrefixes[] = {
        "mscorlib", "System", "Mono.", "netstandard", "Microsoft.",
        "UnityEngine", "Unity.", "UnityUI", "UnityEngine.UI",
        "Newtonsoft.", "AOT", "__Generated", "Bee.", "nunit.", "I18N",
    };
    for (const char* p : kPrefixes) {
        if (_strnicmp(name, p, static_cast<int>(strlen(p))) == 0)
            return true;
    }
    return false;
}

std::string type_name(const Api& api, void* type) {
    if (!type || !api.type_get_name)
        return "object";
    char* n = api.type_get_name(type);
    if (!n)
        return "object";
    std::string r(n);
    if (api.free)
        api.free(n);
    return r.empty() ? "object" : r;
}

std::string class_full_name(const Api& api, void* klass) {
    if (!klass || !api.class_get_name)
        return "Unknown";
    const char* name = api.class_get_name(klass);
    const char* ns   = api.class_get_namespace ? api.class_get_namespace(klass) : nullptr;
    if (ns && ns[0])
        return std::string(ns) + "." + (name ? name : "?");
    return name ? name : "?";
}

// MethodInfo first field is Il2CppMethodPointer on most Unity versions.
void* method_pointer(void* method) {
    if (!method)
        return nullptr;
    return *reinterpret_cast<void* const*>(method);
}

uint64_t method_rva(const Api& api, void* method) {
    void* ptr = method_pointer(method);
    if (!ptr || !api.module_base)
        return 0;
    const auto va = reinterpret_cast<uint64_t>(ptr);
    if (va < api.module_base || va >= api.module_base + api.module_size)
        return va; // absolute fallback
    return va - api.module_base;
}

constexpr int kFieldAttrStatic  = 0x0010;
constexpr int kMethodAttrStatic = 0x0010;
constexpr int kTypeAttrInterface = 0x00000020;
constexpr int kTypeAttrVisibilityMask = 0x00000007;

TypeKind type_kind(const Api& api, void* klass, int flags) {
    if (api.class_is_enum && api.class_is_enum(klass))
        return TypeKind::Enum;
    if ((api.class_is_interface && api.class_is_interface(klass)) ||
        (flags & kTypeAttrInterface))
        return TypeKind::Interface;
    if (api.class_is_valuetype && api.class_is_valuetype(klass))
        return TypeKind::Struct;
    return TypeKind::Class;
}

bool dump_impl(const Api& api, SdkModel& out) {
    void* domain = api.domain_get();
    if (!domain) {
        ILOG_E("il2cpp_domain_get returned null (runtime not ready?)");
        return false;
    }

    void* thread = nullptr;
    if (api.thread_attach) {
        thread = api.thread_attach(domain);
        if (!thread)
            ILOG_W("il2cpp_thread_attach returned null (may already be attached)");
    }

    size_t assembly_count = 0;
    void** assemblies = api.domain_get_assemblies(domain, &assembly_count);
    if (!assemblies || assembly_count == 0) {
        ILOG_E("No IL2CPP assemblies loaded");
        if (thread && api.thread_detach)
            api.thread_detach(thread);
        return false;
    }

    ILOG_I("Runtime walk: %zu assemblies", assembly_count);

    out.engine = ICKY_ENGINE_IL2CPP;
    out.engine_detail = "IL2CPP runtime walk (decrypted names)";
    out.primary_module = {"GameAssembly.dll", api.module_base, api.module_size};
    out.metadata["dump_source"] = "runtime_api";
    out.metadata["assemblies"] = std::to_string(assembly_count);

    int classes = 0, fields = 0, methods = 0;
    int skipped_system = 0;
    int good_names = 0;

    // Prefer game assemblies first; still dump system if game is empty.
    auto walk = [&](bool skip_system) {
        for (size_t ai = 0; ai < assembly_count; ++ai) {
            void* image = api.assembly_get_image(assemblies[ai]);
            if (!image)
                continue;
            const char* image_name =
                api.image_get_name ? api.image_get_name(image) : nullptr;
            if (skip_system && is_system_image(image_name)) {
                ++skipped_system;
                continue;
            }

            const size_t class_count = api.image_get_class_count(image);
            for (size_t ci = 0; ci < class_count; ++ci) {
                void* klass = api.image_get_class(image, ci);
                if (!klass)
                    continue;

                const char* cname = api.class_get_name(klass);
                const char* cns =
                    api.class_get_namespace ? api.class_get_namespace(klass) : nullptr;
                if (!cname || !cname[0] || cname[0] == '<' ||
                    strcmp(cname, "<Module>") == 0)
                    continue;

                SdkType t;
                t.name = cname;
                t.ns = (cns && cns[0]) ? cns : "";
                t.full_name = t.ns.empty() ? t.name : (t.ns + "." + t.name);
                t.address = reinterpret_cast<uint64_t>(klass);
                if (api.module_base && t.address >= api.module_base &&
                    t.address < api.module_base + api.module_size)
                    t.rva = t.address - api.module_base;

                const int flags = api.class_get_flags ? api.class_get_flags(klass) : 0;
                t.kind = type_kind(api, klass, flags);

                if (looks_obfuscated_name(cname) ||
                    (cns && looks_obfuscated_name(cns))) {
                    t.comment = "obfuscated name";
                } else {
                    ++good_names;
                }

                if (api.class_get_parent && t.kind != TypeKind::Enum) {
                    void* parent = api.class_get_parent(klass);
                    if (parent) {
                        const char* pname = api.class_get_name(parent);
                        if (pname && strcmp(pname, "Object") != 0 &&
                            strcmp(pname, "ValueType") != 0 &&
                            strcmp(pname, "Enum") != 0) {
                            t.parent = class_full_name(api, parent);
                        }
                    }
                }

                if (image_name && image_name[0]) {
                    if (!t.comment.empty())
                        t.comment += "; ";
                    t.comment += std::string("image=") + image_name;
                }

                // Fields
                if (api.class_get_fields && api.field_get_name) {
                    void* fiter = nullptr;
                    while (void* field = api.class_get_fields(klass, &fiter)) {
                        const char* fname = api.field_get_name(field);
                        if (!fname)
                            continue;
                        SdkField f;
                        f.name = fname;
                        const int fflags =
                            api.field_get_flags ? api.field_get_flags(field) : 0;
                        f.is_static = (fflags & kFieldAttrStatic) != 0;
                        f.flags = static_cast<uint64_t>(fflags);
                        if (api.field_get_type)
                            f.type_name = type_name(api, api.field_get_type(field));
                        else
                            f.type_name = "object";
                        if (api.field_get_offset && !f.is_static)
                            f.offset = static_cast<int32_t>(api.field_get_offset(field));
                        t.fields.push_back(std::move(f));
                        ++fields;
                    }
                }

                // Methods
                if (api.class_get_methods && api.method_get_name) {
                    void* miter = nullptr;
                    while (void* method = api.class_get_methods(klass, &miter)) {
                        const char* mname = api.method_get_name(method);
                        if (!mname)
                            continue;
                        if (api.method_is_inflated && api.method_is_inflated(method))
                            continue;

                        SdkMethod m;
                        m.name = mname;
                        uint32_t iflags = 0;
                        const uint32_t mflags =
                            api.method_get_flags
                                ? api.method_get_flags(method, &iflags)
                                : 0;
                        m.flags = mflags;
                        m.is_static =
                            (mflags & kMethodAttrStatic) != 0 ||
                            (api.method_is_instance && !api.method_is_instance(method));

                        if (api.method_get_return_type)
                            m.return_type =
                                type_name(api, api.method_get_return_type(method));
                        else
                            m.return_type = "void";

                        void* mptr = method_pointer(method);
                        m.address = reinterpret_cast<uint64_t>(mptr);
                        m.rva = method_rva(api, method);

                        const uint32_t pcount =
                            api.method_get_param_count
                                ? api.method_get_param_count(method)
                                : 0;
                        for (uint32_t pi = 0; pi < pcount; ++pi) {
                            SdkMethodParam p;
                            if (api.method_get_param)
                                p.type_name =
                                    type_name(api, api.method_get_param(method, pi));
                            else
                                p.type_name = "object";
                            if (api.method_get_param_name) {
                                const char* pn =
                                    api.method_get_param_name(method, pi);
                                p.name = (pn && pn[0]) ? pn
                                                       : ("arg" + std::to_string(pi));
                            } else {
                                p.name = "arg" + std::to_string(pi);
                            }
                            m.params.push_back(std::move(p));
                        }

                        t.methods.push_back(std::move(m));
                        ++methods;
                    }
                }

                out.types.push_back(std::move(t));
                ++classes;

                if ((classes % 2000) == 0)
                    ILOG_I("Runtime walk progress: %d classes, %d fields, %d methods",
                           classes, fields, methods);
            }
        }
    };

    walk(/*skip_system=*/true);
    if (out.types.empty()) {
        ILOG_W("No game assemblies yielded types — including system images");
        walk(/*skip_system=*/false);
    }

    if (thread && api.thread_detach)
        api.thread_detach(thread);

    out.metadata["classes"] = std::to_string(classes);
    out.metadata["fields"] = std::to_string(fields);
    out.metadata["methods"] = std::to_string(methods);
    out.metadata["good_names"] = std::to_string(good_names);
    out.metadata["skipped_system_images"] = std::to_string(skipped_system);

    ILOG_I("Runtime dump: %d classes (%d good names), %d fields, %d methods",
           classes, good_names, fields, methods);

    // Require some real-looking names — pure garbage means exports are stubs/hooks.
    if (classes > 0 && good_names == 0 && classes > 20) {
        ILOG_W("Runtime names all look obfuscated/encrypted — still keeping dump");
    }

    return !out.types.empty();
}

} // namespace

void append_il2cpp_exports(uint64_t ga_base, size_t /*ga_size*/, SdkModel& out) {
    if (!ga_base)
        return;
    static const char* kExports[] = {
        "il2cpp_init", "il2cpp_shutdown",
        "il2cpp_domain_get", "il2cpp_domain_get_assemblies", "il2cpp_assembly_get_image",
        "il2cpp_image_get_class_count", "il2cpp_image_get_class", "il2cpp_image_get_name",
        "il2cpp_class_from_name", "il2cpp_class_get_name", "il2cpp_class_get_namespace",
        "il2cpp_class_get_fields", "il2cpp_class_get_methods", "il2cpp_class_get_parent",
        "il2cpp_field_get_name", "il2cpp_field_get_offset", "il2cpp_field_static_get_value",
        "il2cpp_method_get_name", "il2cpp_method_get_param_count",
        "il2cpp_runtime_invoke", "il2cpp_object_new",
        "il2cpp_string_new", "il2cpp_string_chars", "il2cpp_string_length",
        "il2cpp_thread_attach", "il2cpp_thread_detach",
        "il2cpp_resolve_icall", "il2cpp_gchandle_new", "il2cpp_gchandle_get_target",
        "il2cpp_array_new", "il2cpp_type_get_name", "il2cpp_free",
    };
    for (auto* ex : kExports) {
        uint64_t a = get_export(ga_base, ex);
        if (!a)
            continue;
        // Avoid duplicate globals
        bool exists = false;
        for (const auto& g : out.globals) {
            if (g.name == ex) {
                exists = true;
                break;
            }
        }
        if (exists)
            continue;
        out.globals.push_back({ex, a, a - ga_base, "function", "GameAssembly export"});
    }
}

bool dump_from_runtime(uint64_t game_assembly_base, size_t game_assembly_size, SdkModel& out) {
    Api api{};
    if (!bind_api(game_assembly_base, game_assembly_size, api)) {
        ILOG_W("Runtime dump: missing required IL2CPP exports");
        return false;
    }

    ILOG_I("Runtime dump: calling IL2CPP APIs (thread_attach + domain walk)...");
    out.types.clear();

    const bool ok = dump_impl(api, out);
    append_il2cpp_exports(game_assembly_base, game_assembly_size, out);

    if (!ok) {
        ILOG_W("Runtime dump failed");
        return false;
    }
    return true;
}

} // namespace icky::il2cpp
