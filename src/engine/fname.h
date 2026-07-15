#pragma once

#include "core/memory.h"
#include <ue_sdk/types.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <optional>

namespace ue {

class FNameSystem {
public:
    FNameSystem() = default;

    void configure(const Memory* mem, const ue_offsets* offs, uint64_t gnames);

    bool ready() const { return mem_ && offs_ && gnames_; }

    std::string get(int32_t comparison_index, int32_t number = 0) const;

    // Raw pool / table read without number suffix
    std::string get_entry(int32_t comparison_index) const;

    void clear_cache() { cache_.clear(); }

private:
    std::string read_from_pool(int32_t index) const;
    std::string read_from_array(int32_t index) const;

    const Memory*     mem_    = nullptr;
    const ue_offsets* offs_   = nullptr;
    uint64_t          gnames_ = 0;
    mutable std::unordered_map<int32_t, std::string> cache_;
};

// Packed FName in memory
struct FNameRaw {
    int32_t comparison_index = 0;
    int32_t number           = 0;
};

} // namespace ue
