#include "version_db.h"
#include "core/logger.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ue {
namespace {

// ── Offset builders for common UE layouts ───────────────────────────────────
// Values are well-known community defaults for x64 shipping builds.
// Custom games often shift these; use ue_session_set_offsets to override.

ue_offsets base_ue4_pre_ffield() {
    ue_offsets o{};
    o.uobject_vtable          = 0x00;
    o.uobject_flags           = 0x08;
    o.uobject_internal_index  = 0x0C;
    o.uobject_class           = 0x10;
    o.uobject_name            = 0x18;
    o.uobject_outer           = 0x20;

    o.ustruct_super           = 0x40;
    o.ustruct_children        = 0x48;
    o.ustruct_child_properties = 0; // N/A
    o.ustruct_size            = 0x58;
    o.ustruct_min_alignment   = 0x5C;
    o.uclass_cast_flags       = 0xD0;
    o.uclass_default_object   = 0x118;
    o.uclass_functions        = 0;

    o.ufunction_function_flags       = 0xB0;
    o.ufunction_num_parms            = 0xB4;
    o.ufunction_parms_size           = 0xB6;
    o.ufunction_return_value_offset  = 0xB8;
    o.ufunction_func                 = 0xD8;

    o.property_array_dim      = 0x30;
    o.property_element_size   = 0x34;
    o.property_property_flags = 0x38;
    o.property_offset         = 0x44;
    o.property_size           = 0x70;

    o.ffield_class = o.ffield_owner = o.ffield_next = o.ffield_name = o.ffield_flags = 0;

    o.fname_comparison_index = 0x00;
    o.fname_number           = 0x04;
    o.fname_display_index    = 0x00;
    o.fname_size             = 0x08;

    o.fuobjectarray_objects       = 0x10;
    o.fuobjectarray_num_elements  = 0x14;
    o.fuobjectarray_max_elements  = 0x18;
    o.fuobjectarray_num_chunks    = 0;
    o.fuobjectitem_size           = 0x18;
    o.fuobjectitem_object         = 0x00;

    o.fnamepool_blocks = o.fnamepool_current_block = o.fnamepool_current_byte_cursor = 0;
    o.fnameentry_header_size = 0;
    o.fnameentry_len_bit_offset = 0;
    o.fnameentry_len_bit_count  = 0;
    o.uses_name_pool   = 0;
    o.uses_ffield      = 0;
    o.objects_chunked  = 0;
    o.objects_elements_per_chunk = 0;
    return o;
}

ue_offsets base_ue4_25() {
    // FField property system introduced ~4.25
    ue_offsets o = base_ue4_pre_ffield();
    o.uses_ffield = 1;
    o.ustruct_child_properties = 0x50;
    o.ustruct_size             = 0x58;

    o.ffield_class  = 0x08;
    o.ffield_owner  = 0x10;
    o.ffield_next   = 0x18;
    o.ffield_name   = 0x20;
    o.ffield_flags  = 0x28;

    // FProperty offsets relative to FField
    o.property_array_dim      = 0x30;
    o.property_element_size   = 0x34;
    o.property_property_flags = 0x38;
    o.property_offset         = 0x4C;
    o.property_size           = 0x78;

    o.ufunction_function_flags      = 0xB0;
    o.ufunction_num_parms           = 0xB4;
    o.ufunction_parms_size          = 0xB6;
    o.ufunction_return_value_offset = 0xB8;
    o.ufunction_func                = 0xD8;
    return o;
}

ue_offsets base_ue4_23_pool() {
    // FNamePool (with name encryption variants in games)
    ue_offsets o = base_ue4_pre_ffield();
    o.uses_name_pool = 1;
    o.fnamepool_blocks               = 0x00; // after header quirks — absolute pool layout
    o.fnamepool_current_block        = 0x08;
    o.fnamepool_current_byte_cursor  = 0x0C;
    o.fnameentry_header_size         = 2;
    o.fnameentry_len_bit_offset      = 6;
    o.fnameentry_len_bit_count       = 10;
    // Chunked objects become common
    o.objects_chunked                = 1;
    o.objects_elements_per_chunk     = 64 * 1024;
    o.fuobjectarray_objects          = 0x10;
    o.fuobjectarray_num_elements     = 0x14;
    o.fuobjectarray_max_elements     = 0x18;
    o.fuobjectarray_num_chunks       = 0x1C;
    o.fuobjectitem_size              = 0x18;
    return o;
}

ue_offsets base_ue4_27() {
    ue_offsets o = base_ue4_25();
    o.uses_name_pool = 1;
    o.fnamepool_blocks               = 0x00;
    o.fnamepool_current_block        = 0x08;
    o.fnamepool_current_byte_cursor  = 0x0C;
    o.fnameentry_header_size         = 2;
    o.fnameentry_len_bit_offset      = 6;
    o.fnameentry_len_bit_count       = 10;
    o.objects_chunked                = 1;
    o.objects_elements_per_chunk     = 64 * 1024;
    o.fuobjectarray_objects          = 0x10;
    o.fuobjectarray_num_elements     = 0x14;
    o.fuobjectarray_max_elements     = 0x18;
    o.fuobjectarray_num_chunks       = 0x1C;
    o.fuobjectitem_size              = 0x18;

    o.ustruct_super            = 0x40;
    o.ustruct_children         = 0x48;
    o.ustruct_child_properties = 0x50;
    o.ustruct_size             = 0x58;
    o.uclass_cast_flags        = 0xD0;
    o.uclass_default_object    = 0x118;
    return o;
}

ue_offsets base_ue5_0() {
    ue_offsets o = base_ue4_27();
    // UE5: FName often 8 bytes still; some layouts use DisplayIndex
    o.fname_comparison_index = 0x00;
    o.fname_number           = 0x04;
    o.fname_display_index    = 0x00;
    o.fname_size             = 0x08;

    // UObject slightly different in some UE5.0/5.1 games
    o.uobject_vtable         = 0x00;
    o.uobject_flags          = 0x08;
    o.uobject_internal_index = 0x0C;
    o.uobject_class          = 0x10;
    o.uobject_name           = 0x18;
    o.uobject_outer          = 0x20;

    o.ustruct_super            = 0x40;
    o.ustruct_children         = 0x48;
    o.ustruct_child_properties = 0x50;
    o.ustruct_size             = 0x58;
    o.uclass_cast_flags        = 0xD0;
    o.uclass_default_object    = 0x110;

    o.ffield_class  = 0x08;
    o.ffield_owner  = 0x10;
    o.ffield_next   = 0x20;
    o.ffield_name   = 0x28;
    o.ffield_flags  = 0x30;

    o.property_array_dim      = 0x38;
    o.property_element_size   = 0x3C;
    o.property_property_flags = 0x40;
    o.property_offset         = 0x4C;
    o.property_size           = 0x78;

    o.ufunction_function_flags      = 0xB0;
    o.ufunction_num_parms           = 0xB4;
    o.ufunction_parms_size          = 0xB6;
    o.ufunction_return_value_offset = 0xB8;
    o.ufunction_func                = 0xD8;

    o.objects_elements_per_chunk = 64 * 1024;
    o.fuobjectitem_size          = 0x18;
    return o;
}

ue_offsets base_ue5_3() {
    ue_offsets o = base_ue5_0();
    // Common UE5.3+ tweaks (still game-dependent)
    o.uclass_default_object = 0x118;
    o.ffield_next           = 0x20;
    o.ffield_name           = 0x28;
    return o;
}

// Common x64 patterns (shipping / development often differ — these are starting points)
const char* kPatGObjects_UE4 =
    "48 8B 05 ?? ?? ?? ?? 48 8B 0C C8 48 8D 04 D1";
const char* kPatGNames_UE4 =
    "48 8D 05 ?? ?? ?? ?? EB ?? 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? C6 05";
const char* kPatGWorld_UE4 =
    "48 8B 1D ?? ?? ?? ?? 48 85 DB 74 ?? 41 B0 01";
const char* kPatProcessEvent_UE4 =
    "40 55 56 57 41 54 41 55 41 56 41 57 48 81 EC ?? ?? ?? ?? 48 8D 6C 24 ?? 48 89 9D";

const char* kPatGObjects_UE5 =
    "48 8B 05 ?? ?? ?? ?? 48 8B 0C C8 48 8D 04 D1 48 89 44 24";
const char* kPatGNames_UE5 =
    "48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? C6 05 ?? ?? ?? ?? 01 0F 10 03";
const char* kPatGWorld_UE5 =
    "48 8B 1D ?? ?? ?? ?? 48 85 DB 74 3B 41 B0 01";

std::vector<EngineProfile> make_profiles() {
    std::vector<EngineProfile> v;

    auto add = [&](const char* name, int maj, int min, int pat,
                   ue_offsets offs, bool ue5) {
        EngineProfile p;
        p.name    = name;
        p.version = {maj, min, pat};
        p.offsets = offs;
        if (ue5) {
            p.pat_gobjects = kPatGObjects_UE5;
            p.pat_gnames   = kPatGNames_UE5;
            p.pat_gworld   = kPatGWorld_UE5;
            p.pat_gobjects_disp = 3;
            p.pat_gobjects_len  = 7;
            p.pat_gnames_disp   = 3;
            p.pat_gnames_len    = 7;
        } else {
            p.pat_gobjects = kPatGObjects_UE4;
            p.pat_gnames   = kPatGNames_UE4;
            p.pat_gworld   = kPatGWorld_UE4;
            p.pat_gobjects_disp = 3;
            p.pat_gobjects_len  = 7;
            p.pat_gnames_disp   = 3;
            p.pat_gnames_len    = 7;
        }
        p.pat_process_event      = kPatProcessEvent_UE4;
        p.process_event_is_func  = true;
        v.push_back(std::move(p));
    };

    add("UE4.20", 4, 20, 0, base_ue4_pre_ffield(), false);
    add("UE4.21", 4, 21, 0, base_ue4_pre_ffield(), false);
    add("UE4.22", 4, 22, 0, base_ue4_pre_ffield(), false);

    auto o23 = base_ue4_23_pool();
    add("UE4.23", 4, 23, 0, o23, false);
    add("UE4.24", 4, 24, 0, o23, false);

    auto o25 = base_ue4_25();
    o25.uses_name_pool = 1;
    o25.fnameentry_header_size = 2;
    o25.fnameentry_len_bit_offset = 6;
    o25.fnameentry_len_bit_count  = 10;
    o25.objects_chunked = 1;
    o25.objects_elements_per_chunk = 64 * 1024;
    o25.fuobjectarray_num_chunks = 0x1C;
    add("UE4.25", 4, 25, 0, o25, false);
    add("UE4.26", 4, 26, 0, o25, false);
    add("UE4.27", 4, 27, 0, base_ue4_27(), false);

    add("UE5.0", 5, 0, 0, base_ue5_0(), true);
    add("UE5.1", 5, 1, 0, base_ue5_0(), true);
    add("UE5.2", 5, 2, 0, base_ue5_0(), true);
    add("UE5.3", 5, 3, 0, base_ue5_3(), true);
    add("UE5.4", 5, 4, 0, base_ue5_3(), true);
    add("UE5.5", 5, 5, 0, base_ue5_3(), true);

    return v;
}

const std::vector<EngineProfile>& profiles() {
    static const std::vector<EngineProfile> g = make_profiles();
    return g;
}

int version_score(ue_version a, ue_version b) {
    // Lower is better
    if (a.major != b.major)
        return 10000 + std::abs(a.major - b.major) * 100;
    if (a.minor != b.minor)
        return std::abs(a.minor - b.minor) * 10;
    return std::abs(a.patch - b.patch);
}

} // namespace

const std::vector<EngineProfile>& builtin_profiles() {
    return profiles();
}

const EngineProfile* find_profile(ue_version v) {
    const auto& all = profiles();
    const EngineProfile* best = nullptr;
    int best_score = 0x7fffffff;
    for (const auto& p : all) {
        const int s = version_score(p.version, v);
        if (s < best_score) {
            best_score = s;
            best = &p;
        }
        if (s == 0)
            break;
    }
    return best;
}

ue_offsets offsets_for_version(ue_version v, bool* found) {
    const EngineProfile* p = find_profile(v);
    if (found)
        *found = p != nullptr;
    if (!p) {
        ue_offsets z{};
        return z;
    }
    return p->offsets;
}

bool parse_version_string(const std::string& s, ue_version* out) {
    if (!out || s.empty())
        return false;
    // Accept "5.3.2", "4.27", "UE5.1", "++UE5+Release-5.3-..."
    int maj = 0, min = 0, pat = 0;
    const char* c = s.c_str();
    // find first digit sequence that looks like major.minor
    while (*c) {
        if (*c >= '0' && *c <= '9') {
            char* end = nullptr;
            maj = static_cast<int>(std::strtol(c, &end, 10));
            if (end && *end == '.') {
                min = static_cast<int>(std::strtol(end + 1, &end, 10));
                if (end && *end == '.')
                    pat = static_cast<int>(std::strtol(end + 1, &end, 10));
                if (maj == 4 || maj == 5) {
                    out->major = maj;
                    out->minor = min;
                    out->patch = pat;
                    return true;
                }
            }
            c = end ? end : c + 1;
            continue;
        }
        ++c;
    }
    return false;
}

} // namespace ue
