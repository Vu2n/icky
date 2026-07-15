#pragma once

#include <string>

namespace icky {

bool ensure_dir(const std::string& path);
std::string join_path(const std::string& a, const std::string& b);
std::string default_output_dir();
bool write_text_file(const std::string& path, const std::string& content);

} // namespace icky
