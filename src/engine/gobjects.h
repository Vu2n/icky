#pragma once

#include "core/memory.h"
#include <ue_sdk/types.h>
#include <cstdint>
#include <functional>
#include <optional>

namespace ue {

class GObjects {
public:
    GObjects() = default;

    void configure(const Memory* mem, const ue_offsets* offs, uint64_t gobjects);

    bool ready() const { return mem_ && offs_ && gobjects_; }

    int32_t num_elements() const;
    uint64_t get_object(int32_t index) const;

    // Iterate valid UObject* addresses
    void for_each(const std::function<void(int32_t index, uint64_t object)>& fn,
                  int32_t max_count = 0) const;

private:
    uint64_t object_item_address(int32_t index) const;

    const Memory*     mem_      = nullptr;
    const ue_offsets* offs_     = nullptr;
    uint64_t          gobjects_ = 0;
};

} // namespace ue
