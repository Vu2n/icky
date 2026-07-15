#pragma once

#include "metadata.h"
#include <cstdint>
#include <optional>
#include <string>

namespace icky::il2cpp {

// True if name looks like encrypted/random garbage (hex-ish, high non-alnum, etc.)
bool looks_obfuscated_name(const char* name);
bool looks_obfuscated_name(const std::string& name);

// Scan process memory for decrypted global-metadata (magic 0xFAB11BAF).
// Rust and other titles decrypt into a private heap buffer while on-disk remains encrypted.
std::optional<MetadataBlob> scan_decrypted_metadata_in_memory();

// Sample type names from a blob; returns fraction of "good" names in [0,1].
// Used to reject disk metadata that has valid magic but encrypted string heaps.
float metadata_name_quality(const MetadataBlob& blob, int sample_count = 64);

// True if quality is high enough to trust disk/memory metadata for type export.
bool metadata_names_usable(const MetadataBlob& blob);

} // namespace icky::il2cpp
