#pragma once

#include "engine/uobject.h"
#include <ue_sdk/types.h>
#include <string>
#include <vector>

namespace ue {

struct GenerateOptions {
    std::string output_dir;
    bool one_header_per_package = true;
    bool include_padding = true;
    bool write_offsets_header = true;
};

// Returns number of files written, or -1 on failure.
int generate_cpp_sdk(const std::vector<PackageInfo>& packages,
                     const ue_offsets& offsets,
                     const ue_globals& globals,
                     const ue_version& version,
                     const GenerateOptions& options);

int write_offsets_header(const std::string& path,
                         const ue_offsets& offsets,
                         const ue_globals& globals,
                         const ue_version& version);

// Sanitize identifier for C++
std::string sanitize_ident(const std::string& name);

// Map UE property class name to a C++ type approximation
std::string map_property_cpp_type(const PropertyInfo& p);

} // namespace ue
