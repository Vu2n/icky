#pragma once

#include "engine/uobject.h"
#include <ue_sdk/types.h>
#include <string>
#include <vector>

namespace ue {

// Minimal JSON writer (no third-party dependency).
// Returns 1 on success, -1 on failure.
int export_json(const std::vector<PackageInfo>& packages,
                const ue_offsets& offsets,
                const ue_globals& globals,
                const ue_version& version,
                const std::string& output_path);

} // namespace ue
