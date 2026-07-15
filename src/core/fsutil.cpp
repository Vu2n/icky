#include "fsutil.h"
#include "modules.h"
#include "logger.h"

#include <filesystem>
#include <fstream>
#include <Windows.h>

namespace fs = std::filesystem;

namespace icky {

bool ensure_dir(const std::string& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    return !ec || fs::is_directory(path);
}

std::string join_path(const std::string& a, const std::string& b) {
    return (fs::path(a) / b).string();
}

std::string default_output_dir() {
    const std::string base = dll_directory();
    const std::string game = process_name();
    std::string stem = game;
    const auto dot = stem.find_last_of('.');
    if (dot != std::string::npos) stem = stem.substr(0, dot);
    const std::string dir = join_path(base, "icky_sdk_" + stem);
    if (ensure_dir(dir)) return dir;
    char temp[MAX_PATH]{};
    GetTempPathA(MAX_PATH, temp);
    const std::string fallback = join_path(temp, "icky_sdk_" + stem);
    ensure_dir(fallback);
    return fallback;
}

bool write_text_file(const std::string& path, const std::string& content) {
    std::ofstream os(path, std::ios::binary);
    if (!os) {
        ILOG_E("Failed to write %s", path.c_str());
        return false;
    }
    os.write(content.data(), static_cast<std::streamsize>(content.size()));
    return true;
}

} // namespace icky
