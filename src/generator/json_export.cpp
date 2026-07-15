#include "json_export.h"
#include "core/logger.h"
#include "cpp_generator.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace ue {
namespace {

std::string esc(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '"':  o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n";  break;
        case '\r': o += "\\r";  break;
        case '\t': o += "\\t";  break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                o += buf;
            } else {
                o += c;
            }
        }
    }
    return o;
}

void prop_json(std::ostream& os, const PropertyInfo& p, const char* indent) {
    os << indent << "{\n";
    os << indent << "  \"name\": \"" << esc(p.name) << "\",\n";
    os << indent << "  \"type\": \"" << esc(p.type_name) << "\",\n";
    os << indent << "  \"offset\": " << p.offset << ",\n";
    os << indent << "  \"size\": " << p.size << ",\n";
    os << indent << "  \"array_dim\": " << p.array_dim << ",\n";
    os << indent << "  \"flags\": " << p.property_flags << "\n";
    os << indent << "}";
}

} // namespace

int export_json(const std::vector<PackageInfo>& packages,
                const ue_offsets& offsets,
                const ue_globals& globals,
                const ue_version& version,
                const std::string& output_path) {
    std::ofstream os(output_path, std::ios::binary);
    if (!os) {
        UE_LOG_E("Failed to open JSON output: %s", output_path.c_str());
        return -1;
    }

    os << "{\n";
    os << "  \"generator\": \"ue_sdk_gen\",\n";
    os << "  \"engine\": { \"major\": " << version.major
       << ", \"minor\": " << version.minor
       << ", \"patch\": " << version.patch << " },\n";
    os << "  \"globals\": {\n";
    os << "    \"gobjects\": " << globals.gobjects << ",\n";
    os << "    \"gnames\": " << globals.gnames << ",\n";
    os << "    \"gworld\": " << globals.gworld << ",\n";
    os << "    \"process_event\": " << globals.process_event << "\n";
    os << "  },\n";
    os << "  \"features\": {\n";
    os << "    \"uses_name_pool\": " << (offsets.uses_name_pool ? "true" : "false") << ",\n";
    os << "    \"uses_ffield\": " << (offsets.uses_ffield ? "true" : "false") << ",\n";
    os << "    \"objects_chunked\": " << (offsets.objects_chunked ? "true" : "false") << "\n";
    os << "  },\n";
    os << "  \"packages\": [\n";

    for (size_t pi = 0; pi < packages.size(); ++pi) {
        const auto& pkg = packages[pi];
        os << "    {\n";
        os << "      \"name\": \"" << esc(pkg.name) << "\",\n";
        os << "      \"address\": " << pkg.address << ",\n";

        os << "      \"enums\": [\n";
        for (size_t i = 0; i < pkg.enums.size(); ++i) {
            const auto& e = pkg.enums[i];
            os << "        { \"name\": \"" << esc(e.name) << "\", \"members\": [";
            for (size_t j = 0; j < e.members.size(); ++j) {
                os << "{ \"name\": \"" << esc(e.members[j].first) << "\", \"value\": "
                   << e.members[j].second << "}";
                if (j + 1 < e.members.size()) os << ", ";
            }
            os << "] }";
            if (i + 1 < pkg.enums.size()) os << ",";
            os << "\n";
        }
        os << "      ],\n";

        auto emit_structs = [&](const char* key, const std::vector<StructInfo>& list) {
            os << "      \"" << key << "\": [\n";
            for (size_t i = 0; i < list.size(); ++i) {
                const auto& s = list[i];
                os << "        {\n";
                os << "          \"name\": \"" << esc(s.name) << "\",\n";
                os << "          \"full_name\": \"" << esc(s.full_name) << "\",\n";
                os << "          \"super\": \"" << esc(s.super_name) << "\",\n";
                os << "          \"size\": " << s.size << ",\n";
                os << "          \"address\": " << s.address << ",\n";
                os << "          \"properties\": [\n";
                for (size_t j = 0; j < s.properties.size(); ++j) {
                    prop_json(os, s.properties[j], "            ");
                    if (j + 1 < s.properties.size()) os << ",";
                    os << "\n";
                }
                os << "          ],\n";
                os << "          \"functions\": [\n";
                for (size_t j = 0; j < s.functions.size(); ++j) {
                    const auto& f = s.functions[j];
                    os << "            { \"name\": \"" << esc(f.name)
                       << "\", \"flags\": " << f.function_flags
                       << ", \"func\": " << f.func << " }";
                    if (j + 1 < s.functions.size()) os << ",";
                    os << "\n";
                }
                os << "          ]\n";
                os << "        }";
                if (i + 1 < list.size()) os << ",";
                os << "\n";
            }
            os << "      ]";
        };

        emit_structs("structs", pkg.structs);
        os << ",\n";
        emit_structs("classes", pkg.classes);
        os << "\n    }";
        if (pi + 1 < packages.size()) os << ",";
        os << "\n";
    }

    os << "  ]\n";
    os << "}\n";
    UE_LOG_I("Wrote JSON: %s", output_path.c_str());
    return 1;
}

} // namespace ue
