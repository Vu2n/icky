#pragma once

#include "model/sdk_model.h"
#include <cstdint>
#include <string>

namespace icky::il2cpp {

struct DecryptRecoverStats {
    size_t methods_scanned = 0;
    size_t getters_found = 0;
    size_t getters_rejected = 0;   // pattern ok but low-confidence / false positive
    size_t fields_annotated = 0;   // high-confidence with decrypt_rva
    size_t fields_wrapper_only = 0; // Encrypted<T> type rewrite, no decrypt yet
    size_t algos_recovered = 0;
    size_t wrapper_types_cleared = 0;
    size_t second_pass_hits = 0;
};

// Walk method RVAs, find real encrypted-field getters, recover decrypt + clean algo.
DecryptRecoverStats recover_field_decrypts(SdkModel& model, uint64_t module_base,
                                           size_t module_size);

// Write Decrypt.hpp — only high-confidence entries (real crypto / Encrypted<> / semantic).
bool write_decrypt_header(const SdkModel& model, const std::string& path);

} // namespace icky::il2cpp
