#pragma once

#include "model/sdk_model.h"
#include <string>

namespace icky {

// Canonical website-facing dump (icky.dump/v1).
// Writes path (file). Returns true on success.
bool write_icky_dump_json(const SdkModel& model, icky_sdk_mode mode, const std::string& path);

// Build slug from executable / game name: "AVF2-Win64-Shipping.exe" → "avf2-win64-shipping"
std::string make_game_slug(const std::string& name);

// Engine id string for schema
const char* engine_id_string(icky_engine e);
const char* engine_label_string(icky_engine e);

} // namespace icky
