#pragma once

#include "model/sdk_model.h"
#include <cstdint>
#include <string>
#include <vector>
#include <utility>

namespace icky::il2cpp {

// Resolve / call il2cpp string APIs and sample decrypted literals.
struct StringDecryptor {
    uint64_t game_assembly = 0;
    size_t   ga_size = 0;

    // Function pointers (RVA resolved)
    using il2cpp_string_new_t = void* (*)(const char*);
    using il2cpp_string_chars_t = uint16_t* (*)(void*);
    using il2cpp_string_length_t = int32_t (*)(void*);

    il2cpp_string_new_t    string_new = nullptr;
    il2cpp_string_chars_t  string_chars = nullptr;
    il2cpp_string_length_t string_length = nullptr;
    uint64_t               string_new_addr = 0;

    bool init(uint64_t ga_base, size_t ga_size);

    // Read System.String* to UTF-8 (handles runtime objects)
    std::string read_il2cpp_string(uint64_t string_obj) const;

    // Heuristic: find encrypted string tables and XOR-decrypt with discovered keys
    std::vector<std::pair<std::string, std::string>>
    decrypt_literal_samples(const std::vector<uint8_t>& metadata_or_image,
                            size_t max_samples = 64) const;

    // Common game protections: byte XOR key, rolling XOR, NOT+XOR
    static std::string xor_decrypt(const uint8_t* data, size_t len, uint8_t key);
    static std::string rolling_xor_decrypt(const uint8_t* data, size_t len, uint8_t key);
    static bool looks_printable(const std::string& s);
};

} // namespace icky::il2cpp
