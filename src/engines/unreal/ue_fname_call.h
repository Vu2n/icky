#pragma once

#include <cstdint>
#include <string>

namespace icky::ue {

// Call game FName::ToString / AppendString safely. Returns UTF-8.
// fn must point at a suitable FName stringifier (validated by try_validate_name_fn).
std::string call_fname_to_string(uint64_t fn, int32_t comparison_index, int32_t number);

// Returns true if fn(0,0) produces "None" (stock UE).
bool try_validate_name_fn(uint64_t fn);

// Scan module for FName ToString/AppendString by trying candidates near GNames xrefs
// and known prologues; returns first validated function or 0.
uint64_t find_fname_tostring(uint64_t module_base, size_t module_size,
                             uint64_t gnames, uint64_t preferred_candidate);

} // namespace icky::ue
