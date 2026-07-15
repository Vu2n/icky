#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <utility>

namespace icky::mono_str {

struct MonoStringApi {
    uint64_t mono_base = 0;
    // mono_string_to_utf8(MonoString*) -> char* (g_free)
    using to_utf8_t = char* (*)(void*);
    using free_t = void (*)(void*);
    using string_new_t = void* (*)(void* domain, const char*);

    to_utf8_t   to_utf8 = nullptr;
    free_t      g_free = nullptr;
    string_new_t string_new = nullptr;

    bool init(uint64_t mono_module_base);
    std::string read_string(uint64_t mono_string) const;

    // Heuristic decrypt samples from module image (XOR schemes used by some protectors)
    std::vector<std::pair<std::string, std::string>>
    sample_decrypted(const uint8_t* image, size_t size, size_t max_samples = 32) const;
};

} // namespace icky::mono_str
