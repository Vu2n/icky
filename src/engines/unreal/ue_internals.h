#pragma once

#include "model/sdk_model.h"
#include "core/modules.h"
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>

namespace icky::ue {

struct Layout {
    // UObject
    int32_t uobject_vtable = 0x00;
    int32_t uobject_flags  = 0x08;
    int32_t uobject_index  = 0x0C;
    int32_t uobject_class  = 0x10;
    int32_t uobject_name   = 0x18;
    int32_t uobject_outer  = 0x20;

    // UStruct
    int32_t ustruct_super            = 0x40;
    int32_t ustruct_children         = 0x48;
    int32_t ustruct_child_properties = 0x50;
    int32_t ustruct_size             = 0x58;
    int32_t ustruct_min_align        = 0x5C;

    // UFunction
    int32_t ufunction_flags  = 0xB0;
    int32_t ufunction_native = 0xD8;

    // FField / FProperty (UE4.25+)
    int32_t ffield_class  = 0x08;
    int32_t ffield_owner  = 0x10;
    int32_t ffield_next   = 0x20;
    int32_t ffield_name   = 0x28;
    int32_t ffield_flags  = 0x30;
    int32_t prop_array_dim = 0x38;
    int32_t prop_elem_size = 0x3C;
    int32_t prop_flags     = 0x40;
    int32_t prop_offset    = 0x4C;

    // GUObjectArray / FChunkedFixedUObjectArray
    // gobjects_base points at either GUObjectArray or the chunked array itself
    int32_t objobjects_offset = 0x10; // GUObjectArray → ObjObjects
    int32_t objects_ptr       = 0x00; // within chunked array
    int32_t num_elements      = 0x14;
    int32_t max_elements      = 0x10;
    int32_t num_chunks        = 0x1C;
    int32_t item_size         = 0x18;
    int32_t item_object       = 0x00;
    int32_t per_chunk         = 64 * 1024;
    bool    chunked           = true;
    bool    gobjects_is_outer = true; // true: apply objobjects_offset

    // FNamePool
    int32_t pool_blocks_off     = 0x10;
    int32_t fname_header_size   = 2;
    int32_t fname_len_bit_off   = 6;
    int32_t fname_len_bit_count = 10;
    bool    fname_encrypted     = false;
    uint8_t fname_xor_key       = 0;

    std::string describe() const;
};

struct Globals {
    uint64_t gobjects = 0; // resolved base used with Layout
    uint64_t gnames   = 0;
    uint64_t gworld   = 0;
    uint64_t process_event = 0;
    uint64_t append_string = 0;
    uint64_t module_base = 0;
    size_t   module_size = 0;
    std::string module_name;
    std::string engine_guess; // "UE4.27" / "UE5.x"
};

// Find main shipping module
ModuleInfo find_game_module();

// Pattern + heuristic discovery
bool find_globals(Globals& g);
bool discover_layout(Globals& g, Layout& layout);

// FName resolve (game ToString preferred; pool + decrypt fallback)
class NamePool {
public:
    void init(uint64_t gnames, const Layout& layout, uint64_t append_string = 0);
    std::string get(int32_t comparison_index, int32_t number = 0) const;
    bool looks_sane() const; // true only if index 0 resolves to "None"
    bool using_game_fn() const { return use_game_fn_; }
    const Layout& layout() const { return layout_; }

private:
    std::string get_raw(int32_t index) const;
    std::string decrypt_entry(std::string s, int32_t index) const;
    std::string via_append_string(int32_t index, int32_t number) const;

    uint64_t gnames_ = 0;
    uint64_t append_string_ = 0;
    bool     use_game_fn_ = false;
    Layout layout_{};
    mutable std::unordered_map<int64_t, std::string> cache_;
};

// Full object dump into SdkModel
bool dump_sdk(const Globals& g, const Layout& layout, SdkModel& out);

} // namespace icky::ue
