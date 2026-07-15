#pragma once

#include <ue_sdk/types.h>
#include <string>
#include <vector>

namespace ue {

struct EngineProfile {
    std::string name;
    ue_version  version{};
    ue_offsets  offsets{};
    // IDA patterns for globals (empty = skip)
    std::string pat_gobjects;
    int         pat_gobjects_disp = 3;
    int         pat_gobjects_len  = 7;
    std::string pat_gnames;
    int         pat_gnames_disp = 3;
    int         pat_gnames_len  = 7;
    std::string pat_gworld;
    int         pat_gworld_disp = 3;
    int         pat_gworld_len  = 7;
    std::string pat_process_event;
    // process_event is often a direct function address match
    bool        process_event_is_func = true;
};

const std::vector<EngineProfile>& builtin_profiles();

// Nearest matching profile (exact major.minor preferred).
const EngineProfile* find_profile(ue_version v);

ue_offsets offsets_for_version(ue_version v, bool* found = nullptr);

// Heuristic version from PE ProductVersion-style string "4.27.2-0+UE4"
bool parse_version_string(const std::string& s, ue_version* out);

} // namespace ue
