#pragma once

#include "model/sdk_model.h"
#include <cstdint>
#include <string>
#include <vector>

namespace icky::il2cpp {

struct DeobfStats {
    size_t hashed_fields_seen = 0;
    size_t hashed_methods_seen = 0;
    size_t renamed_fields = 0;
    size_t renamed_methods = 0;
    size_t renamed_types = 0;
    size_t semantic_hits = 0;
    size_t string_xref_hits = 0;
    size_t getter_hits = 0;
    size_t unique_type_hits = 0;
};

struct DeobfOptions {
    // Rename %hash / _hash members from field types + offsets
    bool structural = true;
    // Known Rust class layouts (BasePlayer.playerModel, etc.)
    bool semantic_rust = true;
    // 0-arg instance methods with clear return type → get_X / X
    bool getter_heuristic = true;
    // Scan GameAssembly prologues for LEA → string (IDA-style, slower, high value)
    bool string_xref = true;
    // Max bytes of each method body to scan for string refs
    size_t method_scan_bytes = 0x180;
    // Cap methods scanned for string xrefs (0 = all hashed methods)
    size_t max_string_xref_methods = 0; // 0 = unlimited
};

// True if name looks like Facepunch %sha1 or pure hex junk.
bool is_hashed_name(const std::string& name);

// Multi-pass name recovery on a live SdkModel. Safe to run after runtime dump.
// module_base/size used for string-xref scanning of method RVAs.
DeobfStats deobfuscate_sdk(SdkModel& model, uint64_t module_base, size_t module_size,
                           const DeobfOptions& opt = {});

// Write hash → recovered name map next to dump (for diffs / external tools).
bool write_name_map(const SdkModel& model, const std::string& path, const DeobfStats& stats);

// Write high-confidence semantic offsets header (Rust-oriented).
bool write_semantic_offsets(const SdkModel& model, const std::string& path);

} // namespace icky::il2cpp
