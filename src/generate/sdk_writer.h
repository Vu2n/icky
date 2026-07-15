#pragma once

#include "model/sdk_model.h"
#include <string>

namespace icky {

struct WriteResult {
    bool ok = false;
    std::string out_dir;
    int files = 0;
    std::string message;
};

// Write Internal and/or External SDK trees under out_dir.
WriteResult write_sdk(const SdkModel& model, icky_sdk_mode mode, const std::string& out_dir);

std::string sanitize_ident(const std::string& name);
std::string map_cpp_type(const std::string& engine_type);

} // namespace icky
