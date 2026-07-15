#include "gobjects.h"
#include "core/logger.h"

namespace ue {

void GObjects::configure(const Memory* mem, const ue_offsets* offs, uint64_t gobjects) {
    mem_      = mem;
    offs_     = offs;
    gobjects_ = gobjects;
}

int32_t GObjects::num_elements() const {
    if (!ready())
        return 0;
    int32_t n = 0;
    const int32_t off = offs_->fuobjectarray_num_elements
                            ? offs_->fuobjectarray_num_elements
                            : 0x14;
    if (!mem_->read(gobjects_ + static_cast<uint64_t>(off), n))
        return 0;
    if (n < 0 || n > 50'000'000)
        return 0;
    return n;
}

uint64_t GObjects::object_item_address(int32_t index) const {
    if (!ready() || index < 0)
        return 0;

    const int32_t item_size = offs_->fuobjectitem_size ? offs_->fuobjectitem_size : 0x18;
    const int32_t objects_off = offs_->fuobjectarray_objects ? offs_->fuobjectarray_objects : 0x10;

    if (offs_->objects_chunked) {
        const int32_t per_chunk = offs_->objects_elements_per_chunk
                                      ? offs_->objects_elements_per_chunk
                                      : (64 * 1024);
        const int32_t chunk_index = index / per_chunk;
        const int32_t within      = index % per_chunk;

        // Objects is TUObjectItem** or pointer to chunk table
        uint64_t objects = 0;
        if (!mem_->read(gobjects_ + static_cast<uint64_t>(objects_off), objects) || !objects)
            return 0;

        uint64_t chunk = 0;
        if (!mem_->read(objects + static_cast<uint64_t>(chunk_index) * 8, chunk) || !chunk)
            return 0;

        return chunk + static_cast<uint64_t>(within) * static_cast<uint64_t>(item_size);
    }

    // Flat array
    uint64_t objects = 0;
    if (!mem_->read(gobjects_ + static_cast<uint64_t>(objects_off), objects) || !objects)
        return 0;
    return objects + static_cast<uint64_t>(index) * static_cast<uint64_t>(item_size);
}

uint64_t GObjects::get_object(int32_t index) const {
    const uint64_t item = object_item_address(index);
    if (!item)
        return 0;
    const int32_t obj_off = offs_->fuobjectitem_object;
    uint64_t obj = 0;
    if (!mem_->read(item + static_cast<uint64_t>(obj_off), obj))
        return 0;
    // Basic pointer sanity (user-mode x64)
    if (obj < 0x10000 || obj > 0x00007FFFFFFFFFFFULL)
        return 0;
    return obj;
}

void GObjects::for_each(const std::function<void(int32_t, uint64_t)>& fn, int32_t max_count) const {
    const int32_t n = num_elements();
    const int32_t limit = (max_count > 0 && max_count < n) ? max_count : n;
    for (int32_t i = 0; i < limit; ++i) {
        const uint64_t obj = get_object(i);
        if (obj)
            fn(i, obj);
    }
}

} // namespace ue
