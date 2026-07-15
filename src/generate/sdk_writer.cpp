#include "sdk_writer.h"
#include "dump_format.h"
#include "engines/il2cpp/deobf.h"
#include "engines/il2cpp/decrypt_recover.h"
#include "core/fsutil.h"
#include "core/logger.h"

#include <sstream>
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <iomanip>

namespace icky {
namespace {

const char* engine_folder(icky_engine e) {
    switch (e) {
    case ICKY_ENGINE_UNREAL:  return "unreal";
    case ICKY_ENGINE_IL2CPP:  return "il2cpp";
    case ICKY_ENGINE_MONO:    return "mono";
    case ICKY_ENGINE_SOURCE1: return "source1";
    case ICKY_ENGINE_SOURCE2: return "source2";
    default: return "unknown";
    }
}

const char* engine_title(icky_engine e) {
    switch (e) {
    case ICKY_ENGINE_UNREAL:  return "Unreal Engine";
    case ICKY_ENGINE_IL2CPP:  return "Unity IL2CPP";
    case ICKY_ENGINE_MONO:    return "Mono / Unity Mono";
    case ICKY_ENGINE_SOURCE1: return "Source 1";
    case ICKY_ENGINE_SOURCE2: return "Source 2";
    default: return "Unknown";
    }
}

std::string banner(const SdkModel& m, icky_sdk_mode mode) {
    std::ostringstream os;
    os << "// ============================================================\n"
       << "//  Icky SDK - " << (mode == ICKY_SDK_INTERNAL ? "INTERNAL" : "EXTERNAL") << "\n"
       << "//  Engine : " << engine_title(m.engine) << " (" << m.engine_detail << ")\n"
       << "//  Game   : " << m.game_name << "\n"
       << "//  Module : " << m.primary_module.name
       << "  base=0x" << std::hex << m.primary_module.base << std::dec << "\n"
       << "//  Generated automatically - do not hand-edit\n"
       << "// ============================================================\n\n"
       << "#pragma once\n\n"
       << "#include <cstdint>\n"
       << "#include <cstddef>\n"
       << "#include <cstring>\n\n";
    return os.str();
}

void write_globals_internal(std::ostringstream& os, const SdkModel& m) {
    os << "namespace Icky::Globals {\n";
    os << "    // Absolute addresses at dump time (ASLR — re-resolve via module base + RVA)\n";
    for (auto& g : m.globals) {
        os << "    constexpr std::uintptr_t " << sanitize_ident(g.name)
           << " = 0x" << std::hex << g.address << std::dec << "ULL;"
           << " // " << g.type_name;
        if (!g.comment.empty()) os << " - " << g.comment;
        os << "\n";
    }
    os << "}\n\n";
    os << "namespace Icky::RVA {\n";
    for (auto& g : m.globals) {
        os << "    constexpr std::uintptr_t " << sanitize_ident(g.name)
           << " = 0x" << std::hex << g.rva << std::dec << "ULL;\n";
    }
    os << "}\n\n";
}

void write_globals_external(std::ostringstream& os, const SdkModel& m) {
    os << "namespace Icky::External {\n";
    os << "    // All values are RVAs — add remote module base from your external cheat\n";
    os << "    constexpr const char* ModuleName = \"" << m.primary_module.name << "\";\n";
    os << "    struct Offsets {\n";
    for (auto& g : m.globals) {
        os << "        static constexpr std::uintptr_t " << sanitize_ident(g.name)
           << " = 0x" << std::hex << g.rva << std::dec << "ULL;\n";
    }
    os << "    };\n";
    os << "    inline std::uintptr_t Resolve(std::uintptr_t moduleBase, std::uintptr_t rva) {\n"
       << "        return moduleBase + rva;\n"
       << "    }\n";
    os << "}\n\n";
}

bool parent_ok(const std::string& p) {
    if (p.empty() || p == "None" || p == "object") return false;
    // Single-letter / garbage parents from bad Super reads
    if (p.size() <= 1) return false;
    for (unsigned char c : p)
        if (!(std::isalnum(c) || c == '_')) return false;
    return std::isalpha(static_cast<unsigned char>(p[0])) || p[0] == '_';
}

void write_type(std::ostringstream& os, const SdkType& t, icky_sdk_mode mode) {
    if (t.kind == TypeKind::Enum) {
        os << "enum class " << sanitize_ident(t.name) << " : int64_t {\n";
        for (auto& e : t.enum_members)
            os << "    " << sanitize_ident(e.name) << " = " << e.value << ",\n";
        os << "};\n\n";
        return;
    }

    if (t.kind == TypeKind::Namespace) {
        os << "// namespace/assembly: " << t.name;
        if (!t.comment.empty()) os << " - " << t.comment;
        os << "\n";
        for (auto& m : t.methods)
            os << "//   method " << m.name << "\n";
        os << "\n";
        return;
    }

    const char* kw = (t.kind == TypeKind::Struct) ? "struct" : "class";
    os << "// " << t.full_name;
    if (!t.ns.empty()) os << "  [" << t.ns << "]";
    if (t.size) os << "  size=0x" << std::hex << t.size << std::dec;
    if (mode == ICKY_SDK_INTERNAL && t.address)
        os << "  addr=0x" << std::hex << t.address << std::dec;
    if (t.rva)
        os << "  rva=0x" << std::hex << t.rva << std::dec;
    os << "\n";

    os << kw << " " << sanitize_ident(t.name);
    // Never inherit from Object (empty base) or garbage parents
    if (parent_ok(t.parent) && t.parent != "Object")
        os << " : public " << sanitize_ident(t.parent);
    else if (parent_ok(t.parent) && t.parent == "Object")
        os << " /* : public Object */";
    os << " {\n";
    os << "public:\n";

    auto fields = t.fields;
    std::sort(fields.begin(), fields.end(),
              [](const SdkField& a, const SdkField& b) { return a.offset < b.offset; });

    int pad = 0;
    int32_t cursor = 0;
    bool first = true;
    for (auto& f : fields) {
        if (f.is_static) continue;
        if (first) {
            cursor = f.offset;
            first = false;
        } else if (f.offset > cursor && f.offset - cursor < 0x10000) {
            os << "    char pad_" << pad++ << "[0x" << std::hex << (f.offset - cursor) << std::dec << "];\n";
        }
        std::string ty = map_cpp_type(f.type_name);
        if (f.is_encrypted)
            ty = "void*"; // encrypted wrapper / handle holder
        os << "    " << ty << " " << sanitize_ident(f.name);
        if (f.array_dim > 1) os << "[" << f.array_dim << "]";
        os << "; // 0x" << std::hex << f.offset << std::dec;
        if (!f.type_name.empty()) os << " " << f.type_name;
        if (f.is_encrypted)
            os << " [ENCRYPTED]";
        if (f.decrypt.valid && f.decrypt.decrypt_rva)
            os << " decrypt_rva=0x" << std::hex << f.decrypt.decrypt_rva << std::dec;
        if (!f.decrypt.algo_summary.empty())
            os << " " << f.decrypt.algo_summary;
        os << "\n";
        int32_t span = (f.size > 0 ? f.size : 8);
        if (f.array_dim > 1) span *= f.array_dim;
        cursor = f.offset + span;
    }

    if (!t.methods.empty()) {
        os << "\n    // Methods\n";
        for (auto& m : t.methods) {
            os << "    // " << m.return_type << " " << sanitize_ident(m.name) << "(";
            for (size_t i = 0; i < m.params.size(); ++i) {
                if (i) os << ", ";
                os << m.params[i].type_name << " " << m.params[i].name;
            }
            os << ")";
            if (mode == ICKY_SDK_INTERNAL && m.address)
                os << "  @ 0x" << std::hex << m.address << std::dec;
            if (m.rva)
                os << "  rva 0x" << std::hex << m.rva << std::dec;
            os << "\n";

            if (mode == ICKY_SDK_INTERNAL && m.address) {
                // Optional callable stub comment
            }
        }
    }

    // Static field RVAs for external
    bool any_static = false;
    for (auto& f : fields)
        if (f.is_static) any_static = true;
    if (any_static) {
        os << "\n    struct Static {\n";
        for (auto& f : fields) {
            if (!f.is_static) continue;
            os << "        // " << f.name << " offset/rva 0x" << std::hex << f.offset << std::dec << "\n";
        }
        os << "    };\n";
    }

    os << "};\n\n";
}

int write_tree(const SdkModel& model, icky_sdk_mode mode, const std::string& root) {
    int files = 0;
    const std::string mode_dir = join_path(root, mode == ICKY_SDK_INTERNAL ? "internal" : "external");
    const std::string eng_dir = join_path(mode_dir, engine_folder(model.engine));
    if (!ensure_dir(eng_dir)) return -1;

    // Offsets.hpp / Globals
    {
        std::ostringstream os;
        os << banner(model, mode);
        os << "namespace Icky {\n";
        os << "    constexpr int EngineId = " << static_cast<int>(model.engine) << ";\n";
        os << "    constexpr const char* EngineName = \"" << engine_title(model.engine) << "\";\n";
        os << "    constexpr const char* GameName = \"" << model.game_name << "\";\n";
        os << "    constexpr const char* ModuleName = \"" << model.primary_module.name << "\";\n";
        os << "    constexpr std::uintptr_t ModuleSize = 0x" << std::hex << model.primary_module.size << std::dec << "ULL;\n";
        if (mode == ICKY_SDK_INTERNAL)
            os << "    constexpr std::uintptr_t ModuleBaseAtDump = 0x" << std::hex
               << model.primary_module.base << std::dec << "ULL;\n";
        os << "}\n\n";

        if (mode == ICKY_SDK_INTERNAL)
            write_globals_internal(os, model);
        else
            write_globals_external(os, model);

        // Helper for internal resolve under ASLR
        if (mode == ICKY_SDK_INTERNAL) {
            os << "// Rebase helper (call after DLL inject)\n"
               << "inline std::uintptr_t IckyRebase(std::uintptr_t dumpAbs, std::uintptr_t newBase) {\n"
               << "    constexpr auto oldBase = Icky::ModuleBaseAtDump;\n"
               << "    return newBase + (dumpAbs - oldBase);\n"
               << "}\n\n";
        } else {
            os << "// External RPM helper sketch\n"
               << "// uintptr_t remote = moduleBase + Icky::External::Offsets::SomeGlobal;\n"
               << "// ReadProcessMemory(h, (void*)remote, &buf, sizeof(buf), nullptr);\n\n";
        }

        if (write_text_file(join_path(eng_dir, "Offsets.hpp"), os.str()))
            ++files;
    }

    // Group types by namespace
    std::map<std::string, std::vector<const SdkType*>> groups;
    for (auto& t : model.types) {
        std::string key = t.ns.empty() ? "Global" : t.ns;
        groups[key].push_back(&t);
    }

    std::ostringstream sdk_all;
    sdk_all << banner(model, mode);
    sdk_all << "#include \"Offsets.hpp\"\n";
    sdk_all << "#include \"BasicTypes.hpp\"\n\n";

    // BasicTypes
    {
        std::ostringstream os;
        os << banner(model, mode);
        os << "namespace Icky {\n"
           << "struct FName { int32_t ComparisonIndex; int32_t Number; };\n"
           << "struct FString { wchar_t* Data; int32_t Num; int32_t Max; };\n"
           << "template<typename T> struct TArray { T* Data; int32_t Num; int32_t Max; };\n"
           << "struct Vector3 { float x,y,z; };\n"
           << "struct Vector2 { float x,y; };\n"
           << "struct QAngle { float x,y,z; };\n"
           << "struct Il2CppObject { void* klass; void* monitor; };\n"
           << "struct MonoObject { void* vtable; void* sync; };\n"
           << "}\n";
        if (write_text_file(join_path(eng_dir, "BasicTypes.hpp"), os.str()))
            ++files;
    }

    int file_idx = 0;
    for (auto& [ns, list] : groups) {
        // Cap file size: split huge packages into chunks of ~400 types
        constexpr size_t kChunk = 400;
        size_t offset = 0;
        int part = 0;
        while (offset < list.size()) {
            const size_t end = std::min(offset + kChunk, list.size());
            std::ostringstream os;
            os << banner(model, mode);
            os << "#include \"BasicTypes.hpp\"\n\n";
            os << "// Package / namespace: " << ns;
            if (list.size() > kChunk)
                os << " (part " << part << ")";
            os << "\n\n";
            if (mode == ICKY_SDK_INTERNAL)
                os << "namespace Icky::Internal {\n\n";
            else
                os << "namespace Icky::External::Types {\n\n";

            for (size_t i = offset; i < end; ++i)
                write_type(os, *list[i], mode);

            os << "} // namespace\n";

            std::string fname = sanitize_ident(ns);
            if (fname.empty()) fname = "Package";
            if (list.size() > kChunk)
                fname += "_p" + std::to_string(part);
            fname += "_" + std::to_string(file_idx) + ".hpp";
            ++file_idx;
            ++part;
            offset = end;

            if (write_text_file(join_path(eng_dir, fname), os.str())) {
                ++files;
                sdk_all << "#include \"" << fname << "\"\n";
            }
        }
    }

    if (write_text_file(join_path(eng_dir, "SDK.hpp"), sdk_all.str()))
        ++files;

    // strings.txt — decrypted samples
    if (!model.decrypted_strings.empty()) {
        std::ostringstream os;
        os << "# Icky decrypted / recovered string samples\n";
        for (auto& [k, v] : model.decrypted_strings)
            os << "[" << k << "] " << v << "\n";
        if (write_text_file(join_path(eng_dir, "strings_decrypted.txt"), os.str()))
            ++files;
    }

    // Canonical website dump + short summary
    {
        const auto dump_path = join_path(eng_dir, "icky.dump.json");
        if (write_icky_dump_json(model, mode, dump_path))
            ++files;

        // Also copy-friendly path at SDK root for easy upload
        const auto root_dump = join_path(root, "icky.dump.json");
        write_icky_dump_json(model, mode, root_dump);

        // Deobf artifacts (hash → name map + semantic offsets for Rust)
        if (model.engine == ICKY_ENGINE_IL2CPP &&
            model.metadata.count("deobf_done")) {
            il2cpp::DeobfStats st{};
            if (model.metadata.count("deobf_renamed_fields"))
                st.renamed_fields = static_cast<size_t>(std::stoull(model.metadata.at("deobf_renamed_fields")));
            if (model.metadata.count("deobf_renamed_methods"))
                st.renamed_methods = static_cast<size_t>(std::stoull(model.metadata.at("deobf_renamed_methods")));
            if (model.metadata.count("deobf_semantic"))
                st.semantic_hits = static_cast<size_t>(std::stoull(model.metadata.at("deobf_semantic")));
            if (model.metadata.count("deobf_string_xref"))
                st.string_xref_hits = static_cast<size_t>(std::stoull(model.metadata.at("deobf_string_xref")));

            const auto map_path = join_path(eng_dir, "name_map.json");
            if (il2cpp::write_name_map(model, map_path, st))
                ++files;
            const auto sem_path = join_path(eng_dir, "RustOffsets.hpp");
            if (il2cpp::write_semantic_offsets(model, sem_path))
                ++files;
        }

        // Encrypted field decrypt helpers (always try if any field has decrypt info)
        {
            bool any_dec = false;
            for (const auto& t : model.types) {
                for (const auto& f : t.fields) {
                    if (f.decrypt.valid && f.decrypt.decrypt_rva) {
                        any_dec = true;
                        break;
                    }
                }
                if (any_dec) break;
            }
            if (any_dec) {
                const auto dec_path = join_path(eng_dir, "Decrypt.hpp");
                if (il2cpp::write_decrypt_header(model, dec_path))
                    ++files;
            }
        }

        std::ostringstream os;
        os << "{\n"
           << "  \"tool\": \"Icky\",\n"
           << "  \"schema\": \"icky.dump/v1\",\n"
           << "  \"mode\": \"" << (mode == ICKY_SDK_INTERNAL ? "internal" : "external") << "\",\n"
           << "  \"engine\": \"" << engine_title(model.engine) << "\",\n"
           << "  \"detail\": \"" << model.engine_detail << "\",\n"
           << "  \"game\": \"" << model.game_name << "\",\n"
           << "  \"slug\": \"" << make_game_slug(model.game_name.empty()
                                                     ? model.primary_module.name
                                                     : model.game_name)
           << "\",\n"
           << "  \"module\": \"" << model.primary_module.name << "\",\n"
           << "  \"types\": " << model.types.size() << ",\n"
           << "  \"globals\": " << model.globals.size() << ",\n"
           << "  \"dump_file\": \"icky.dump.json\"";
        if (model.metadata.count("deobf_done")) {
            os << ",\n  \"deobf\": true";
            if (model.metadata.count("deobf_semantic"))
                os << ",\n  \"deobf_semantic\": " << model.metadata.at("deobf_semantic");
            if (model.metadata.count("deobf_string_xref"))
                os << ",\n  \"deobf_string_xref\": " << model.metadata.at("deobf_string_xref");
        }
        if (model.metadata.count("decrypt_fields")) {
            os << ",\n  \"decrypt_fields\": " << model.metadata.at("decrypt_fields");
            if (model.metadata.count("decrypt_algos"))
                os << ",\n  \"decrypt_algos\": " << model.metadata.at("decrypt_algos");
            if (model.metadata.count("decrypt_rejected"))
                os << ",\n  \"decrypt_rejected\": " << model.metadata.at("decrypt_rejected");
            if (model.metadata.count("decrypt_wrapper_only"))
                os << ",\n  \"decrypt_wrapper_only\": " << model.metadata.at("decrypt_wrapper_only");
        }
        os << "\n}\n";
        if (write_text_file(join_path(eng_dir, "summary.json"), os.str()))
            ++files;
    }

    // README for the dump
    {
        std::ostringstream os;
        os << "# Icky " << (mode == ICKY_SDK_INTERNAL ? "Internal" : "External")
           << " SDK\n\n"
           << "- Engine: **" << engine_title(model.engine) << "** (" << model.engine_detail << ")\n"
           << "- Game: `" << model.game_name << "`\n"
           << "- Module: `" << model.primary_module.name << "`\n"
           << "- Types: " << model.types.size() << "\n"
           << "- Globals: " << model.globals.size() << "\n\n";
        if (mode == ICKY_SDK_INTERNAL) {
            os << "## Internal usage\n\n"
               << "Include `SDK.hpp` from an injected module in the same process.\n"
               << "Absolute addresses were valid at dump time — prefer **RVA** + current "
               << "`GetModuleHandle` base, or `IckyRebase`.\n";
        } else {
            os << "## External usage\n\n"
               << "All offsets in `Offsets.hpp` are **RVAs**.\n"
               << "```cpp\n"
               << "uintptr_t base = /* remote module base */;\n"
               << "uintptr_t addr = base + Icky::External::Offsets::SomeGlobal;\n"
               << "```\n";
        }
        if (write_text_file(join_path(eng_dir, "README.md"), os.str()))
            ++files;
    }

    return files;
}

} // namespace

std::string sanitize_ident(const std::string& name) {
    if (name.empty()) return "unnamed";
    std::string o;
    o.reserve(name.size());
    for (size_t i = 0; i < name.size(); ++i) {
        char c = name[i];
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
            o.push_back(c);
        else
            o.push_back('_');
    }
    if (std::isdigit(static_cast<unsigned char>(o[0])))
        o.insert(o.begin(), '_');
    static const char* kws[] = {"class","struct","enum","template","operator","new","delete",
        "default","float","int","bool","void","this","true","false","private","public"};
    for (auto* k : kws)
        if (o == k) { o += '_'; break; }
    return o;
}

std::string map_cpp_type(const std::string& t) {
    auto has = [&](const char* s) { return t.find(s) != std::string::npos; };
    if (t.empty()) return "std::uint8_t";
    if (has("Bool") || t == "bool") return "bool";
    if (has("Byte") || t == "byte") return "std::uint8_t";
    if (has("Int16") || t == "short") return "std::int16_t";
    if (has("Int32") || t == "int" || has("IntProperty")) return "std::int32_t";
    if (has("Int64") || t == "long") return "std::int64_t";
    if (has("UInt32") || t == "uint") return "std::uint32_t";
    if (has("UInt64")) return "std::uint64_t";
    if (has("Single") || has("Float") || t == "float") return "float";
    if (has("Double") || t == "double") return "double";
    if (has("String") || has("StrProperty")) return "void*";
    if (has("Object") || has("Class") || has("Ptr")) return "void*";
    if (has("Vector")) return "Icky::Vector3";
    if (has("NameProperty") || t == "FName") return "Icky::FName";
    if (has("netvar")) return "std::uint8_t";
    return "std::uint8_t";
}

WriteResult write_sdk(const SdkModel& model, icky_sdk_mode mode, const std::string& out_dir) {
    WriteResult r;
    r.out_dir = out_dir.empty() ? default_output_dir() : out_dir;
    if (!ensure_dir(r.out_dir)) {
        r.message = "cannot create output directory";
        return r;
    }

    int n = write_tree(model, mode, r.out_dir);
    if (n < 0) {
        r.message = "write failed";
        return r;
    }
    r.files = n;
    r.ok = true;
    r.message = "ok";
    ILOG_I("Wrote %d files to %s (%s)", n, r.out_dir.c_str(),
           mode == ICKY_SDK_INTERNAL ? "internal" : "external");
    return r;
}

} // namespace icky
