#include "dump_format.h"
#include "core/fsutil.h"
#include "core/logger.h"

#include <sstream>
#include <iomanip>
#include <cctype>
#include <chrono>
#include <ctime>
#include <unordered_set>

namespace icky {
namespace {

std::string json_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
        case '"':  o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                o += buf;
            } else {
                o.push_back(static_cast<char>(c));
            }
        }
    }
    return o;
}

std::string hex_u64(uint64_t v) {
    std::ostringstream os;
    os << "0x" << std::hex << std::uppercase << v;
    return os.str();
}

std::string iso_utc_now() {
    using clock = std::chrono::system_clock;
    const auto t = clock::to_time_t(clock::now());
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[40];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string game_display_name(const std::string& exe) {
    std::string s = exe;
    const auto slash = s.find_last_of("\\/");
    if (slash != std::string::npos) s = s.substr(slash + 1);
    // strip -Win64-Shipping.exe etc.
    auto pos = s.find("-Win64");
    if (pos != std::string::npos) s = s.substr(0, pos);
    pos = s.find(".exe");
    if (pos != std::string::npos) s = s.substr(0, pos);
    pos = s.find(".EXE");
    if (pos != std::string::npos) s = s.substr(0, pos);
    return s.empty() ? exe : s;
}

const char* kind_str(TypeKind k) {
    switch (k) {
    case TypeKind::Class: return "class";
    case TypeKind::Struct: return "struct";
    case TypeKind::Enum: return "enum";
    case TypeKind::Interface: return "interface";
    case TypeKind::Namespace: return "namespace";
    default: return "class";
    }
}

} // namespace

std::string make_game_slug(const std::string& name) {
    std::string s = game_display_name(name);
    if (s.empty()) s = name;
    std::string out;
    out.reserve(s.size());
    bool prev_dash = false;
    for (unsigned char c : s) {
        if (std::isalnum(c)) {
            out.push_back(static_cast<char>(std::tolower(c)));
            prev_dash = false;
        } else if (!prev_dash) {
            out.push_back('-');
            prev_dash = true;
        }
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    while (!out.empty() && out.front() == '-') out.erase(out.begin());
    if (out.empty()) out = "unknown-game";
    return out;
}

const char* engine_id_string(icky_engine e) {
    switch (e) {
    case ICKY_ENGINE_UNREAL:  return "unreal";
    case ICKY_ENGINE_IL2CPP:  return "il2cpp";
    case ICKY_ENGINE_MONO:    return "mono";
    case ICKY_ENGINE_SOURCE1: return "source1";
    case ICKY_ENGINE_SOURCE2: return "source2";
    default: return "unknown";
    }
}

const char* engine_label_string(icky_engine e) {
    switch (e) {
    case ICKY_ENGINE_UNREAL:  return "Unreal Engine";
    case ICKY_ENGINE_IL2CPP:  return "Unity IL2CPP";
    case ICKY_ENGINE_MONO:    return "Mono";
    case ICKY_ENGINE_SOURCE1: return "Source 1";
    case ICKY_ENGINE_SOURCE2: return "Source 2";
    default: return "Unknown";
    }
}

bool write_icky_dump_json(const SdkModel& model, icky_sdk_mode mode, const std::string& path) {
    std::ostringstream os;
    os << std::setfill('0');

    int classes = 0, structs = 0, enums = 0, functions = 0;
    std::unordered_set<std::string> packages;
    for (auto& t : model.types) {
        if (t.kind == TypeKind::Class) ++classes;
        else if (t.kind == TypeKind::Struct) ++structs;
        else if (t.kind == TypeKind::Enum) ++enums;
        functions += static_cast<int>(t.methods.size());
        if (!t.ns.empty()) packages.insert(t.ns);
    }

    // Prefer process exe (RustClient.exe) over module (GameAssembly.dll) for catalog slug
    std::string exe = model.game_name;
    if (exe.empty() || exe == "GameAssembly.dll" || exe == "GameAssembly" ||
        exe == "UserAssembly.dll") {
        exe = model.primary_module.name;
        // still bad — keep as last resort
    }
    // Strip .exe for nicer display name when we have a real process name
    const std::string display = game_display_name(exe);
    const std::string slug = make_game_slug(display.empty() ? exe : display);

    // Encryption stats (IL2CPP / Rust)
    int enc_fields = 0, enc_with_decrypt = 0, enc_with_algo = 0;
    for (auto& t : model.types) {
        for (auto& f : t.fields) {
            if (f.is_encrypted || f.decrypt.valid)
                ++enc_fields;
            if (f.decrypt.valid && f.decrypt.decrypt_rva)
                ++enc_with_decrypt;
            if (f.decrypt.valid &&
                (!f.decrypt.xor_imms.empty() || !f.decrypt.rol_amounts.empty() ||
                 !f.decrypt.add_imms.empty()))
                ++enc_with_algo;
        }
    }

    os << "{\n";
    os << "  \"schema\": \"icky.dump/v1\",\n";
    os << "  \"generated_at\": \"" << iso_utc_now() << "\",\n";
    os << "  \"tool\": { \"name\": \"Icky\", \"version\": \"1.0.0\" },\n";
    os << "  \"game\": {\n";
    os << "    \"name\": \"" << json_escape(display) << "\",\n";
    os << "    \"executable\": \"" << json_escape(exe) << "\",\n";
    os << "    \"slug\": \"" << json_escape(slug) << "\"\n";
    os << "  },\n";
    os << "  \"engine\": {\n";
    os << "    \"id\": \"" << engine_id_string(model.engine) << "\",\n";
    os << "    \"label\": \"" << engine_label_string(model.engine) << "\",\n";
    os << "    \"detail\": \"" << json_escape(model.engine_detail) << "\"\n";
    os << "  },\n";
    os << "  \"module\": {\n";
    os << "    \"name\": \"" << json_escape(model.primary_module.name) << "\",\n";
    os << "    \"size\": " << model.primary_module.size << ",\n";
    os << "    \"base_at_dump\": \"" << hex_u64(model.primary_module.base) << "\"\n";
    os << "  },\n";
    os << "  \"mode\": \"" << (mode == ICKY_SDK_INTERNAL ? "internal" : "external") << "\",\n";
    os << "  \"stats\": {\n";
    os << "    \"types\": " << model.types.size() << ",\n";
    os << "    \"classes\": " << classes << ",\n";
    os << "    \"structs\": " << structs << ",\n";
    os << "    \"enums\": " << enums << ",\n";
    os << "    \"functions\": " << functions << ",\n";
    os << "    \"globals\": " << model.globals.size() << ",\n";
    os << "    \"packages\": " << packages.size() << ",\n";
    os << "    \"encrypted_fields\": " << enc_fields << ",\n";
    os << "    \"encrypted_with_decrypt\": " << enc_with_decrypt << ",\n";
    os << "    \"encrypted_with_algo\": " << enc_with_algo << "\n";
    os << "  },\n";
    if (enc_with_decrypt > 0) {
        os << "  \"encryption\": {\n";
        os << "    \"scheme\": \"il2cpp_encrypted_handle\",\n";
        os << "    \"note\": \"encrypted=true fields are Facepunch-style wrappers; "
              "use field.decrypt getter_rva/decrypt_rva/xor/rol/add to resolve\",\n";
        os << "    \"fields_total\": " << enc_fields << ",\n";
        os << "    \"fields_with_decrypt\": " << enc_with_decrypt << ",\n";
        os << "    \"fields_with_algo\": " << enc_with_algo << "\n";
        os << "  },\n";
    }

    // layout from metadata if present
    os << "  \"layout\": {\n";
    bool first_meta = true;
    for (auto& [k, v] : model.metadata) {
        if (!first_meta) os << ",\n";
        first_meta = false;
        os << "    \"" << json_escape(k) << "\": \"" << json_escape(v) << "\"";
    }
    // known globals as layout shortcuts
    for (auto& g : model.globals) {
        if (g.name.rfind("OFF_", 0) == 0) {
            if (!first_meta) os << ",\n";
            first_meta = false;
            os << "    \"" << json_escape(g.name) << "\": " << g.rva;
        }
    }
    if (!first_meta) os << "\n";
    os << "  },\n";

    os << "  \"globals\": [\n";
    for (size_t i = 0; i < model.globals.size(); ++i) {
        auto& g = model.globals[i];
        os << "    {\n";
        os << "      \"name\": \"" << json_escape(g.name) << "\",\n";
        os << "      \"rva\": \"" << hex_u64(g.rva) << "\",\n";
        os << "      \"address\": \"" << hex_u64(g.address) << "\",\n";
        os << "      \"type\": \"" << json_escape(g.type_name) << "\",\n";
        os << "      \"comment\": \"" << json_escape(g.comment) << "\"\n";
        os << "    }" << (i + 1 < model.globals.size() ? "," : "") << "\n";
    }
    os << "  ],\n";

    os << "  \"types\": [\n";
    for (size_t ti = 0; ti < model.types.size(); ++ti) {
        auto& t = model.types[ti];
        os << "    {\n";
        os << "      \"kind\": \"" << kind_str(t.kind) << "\",\n";
        os << "      \"name\": \"" << json_escape(t.name) << "\",\n";
        os << "      \"full_name\": \"" << json_escape(t.full_name) << "\",\n";
        os << "      \"package\": \"" << json_escape(t.ns) << "\",\n";
        os << "      \"parent\": \"" << json_escape(t.parent) << "\",\n";
        os << "      \"size\": " << t.size << ",\n";
        os << "      \"address\": \"" << hex_u64(t.address) << "\",\n";
        os << "      \"rva\": \"" << hex_u64(t.rva) << "\",\n";
        os << "      \"fields\": [\n";
        for (size_t fi = 0; fi < t.fields.size(); ++fi) {
            auto& f = t.fields[fi];
            os << "        { \"name\": \"" << json_escape(f.name)
               << "\", \"offset\": " << f.offset
               << ", \"size\": " << f.size
               << ", \"type\": \"" << json_escape(f.type_name) << "\""
               << ", \"static\": " << (f.is_static ? "true" : "false")
               << ", \"encrypted\": " << (f.is_encrypted ? "true" : "false");
            if (f.decrypt.valid && f.decrypt.decrypt_rva) {
                os << ", \"decrypt\": { \"getter_rva\": \"" << hex_u64(f.decrypt.getter_rva)
                   << "\", \"decrypt_rva\": \"" << hex_u64(f.decrypt.decrypt_rva)
                   << "\", \"typeinfo_rva\": \"" << hex_u64(f.decrypt.typeinfo_rva)
                   << "\", \"algo\": \"" << json_escape(f.decrypt.algo_summary)
                   << "\", \"inner_type\": \"" << json_escape(f.decrypt.inner_type) << "\"";
                if (!f.decrypt.xor_imms.empty()) {
                    os << ", \"xor\": [";
                    for (size_t i = 0; i < f.decrypt.xor_imms.size(); ++i) {
                        if (i) os << ", ";
                        os << "\"" << hex_u64(f.decrypt.xor_imms[i]) << "\"";
                    }
                    os << "]";
                }
                if (!f.decrypt.add_imms.empty()) {
                    os << ", \"add\": [";
                    for (size_t i = 0; i < f.decrypt.add_imms.size(); ++i) {
                        if (i) os << ", ";
                        os << "\"" << hex_u64(f.decrypt.add_imms[i]) << "\"";
                    }
                    os << "]";
                }
                if (!f.decrypt.rol_amounts.empty()) {
                    os << ", \"rol\": [";
                    for (size_t i = 0; i < f.decrypt.rol_amounts.size(); ++i) {
                        if (i) os << ", ";
                        os << f.decrypt.rol_amounts[i];
                    }
                    os << "]";
                }
                os << " }";
            }
            if (!f.comment.empty())
                os << ", \"comment\": \"" << json_escape(f.comment) << "\"";
            os << " }"
               << (fi + 1 < t.fields.size() ? "," : "") << "\n";
        }
        os << "      ],\n";
        os << "      \"methods\": [\n";
        for (size_t mi = 0; mi < t.methods.size(); ++mi) {
            auto& m = t.methods[mi];
            os << "        { \"name\": \"" << json_escape(m.name)
               << "\", \"return_type\": \"" << json_escape(m.return_type)
               << "\", \"rva\": \"" << hex_u64(m.rva)
               << "\", \"address\": \"" << hex_u64(m.address)
               << "\", \"flags\": " << m.flags
               << ", \"static\": " << (m.is_static ? "true" : "false");
            if (!m.comment.empty())
                os << ", \"comment\": \"" << json_escape(m.comment) << "\"";
            if (!m.params.empty()) {
                os << ", \"params\": [";
                for (size_t pi = 0; pi < m.params.size(); ++pi) {
                    if (pi) os << ", ";
                    os << "{ \"name\": \"" << json_escape(m.params[pi].name)
                       << "\", \"type\": \"" << json_escape(m.params[pi].type_name) << "\" }";
                }
                os << "]";
            }
            os << " }"
               << (mi + 1 < t.methods.size() ? "," : "") << "\n";
        }
        os << "      ],\n";
        os << "      \"enum_members\": [\n";
        for (size_t ei = 0; ei < t.enum_members.size(); ++ei) {
            auto& e = t.enum_members[ei];
            os << "        { \"name\": \"" << json_escape(e.name)
               << "\", \"value\": " << e.value << " }"
               << (ei + 1 < t.enum_members.size() ? "," : "") << "\n";
        }
        os << "      ]\n";
        os << "    }" << (ti + 1 < model.types.size() ? "," : "") << "\n";
    }
    os << "  ]\n";
    os << "}\n";

    if (!write_text_file(path, os.str())) {
        ILOG_E("Failed to write icky.dump.json: %s", path.c_str());
        return false;
    }
    ILOG_I("Wrote website dump: %s (%zu types)", path.c_str(), model.types.size());
    return true;
}

} // namespace icky
