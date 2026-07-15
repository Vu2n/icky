#pragma once

#include "model/sdk_model.h"
#include <cstdint>

namespace icky::il2cpp {

// Walk live IL2CPP runtime via GameAssembly exports:
//   domain_get → assemblies → images → classes → fields / methods (+ RVAs)
// This is how Testing\Icky produces usable dumps on Rust — names are already
// decrypted by the runtime. Requires the game to be fully loaded (user dumps after menu).
//
// Returns false if required exports missing or domain not ready.
bool dump_from_runtime(uint64_t game_assembly_base, size_t game_assembly_size, SdkModel& out);

// Append GameAssembly export RVAs into out.globals (safe, no game calls).
void append_il2cpp_exports(uint64_t ga_base, size_t ga_size, SdkModel& out);

} // namespace icky::il2cpp
