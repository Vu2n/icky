#include "fname.h"
#include "core/logger.h"

#include <cstdio>

namespace ue {

void FNameSystem::configure(const Memory* mem, const ue_offsets* offs, uint64_t gnames) {
    mem_    = mem;
    offs_   = offs;
    gnames_ = gnames;
    cache_.clear();
}

std::string FNameSystem::get(int32_t comparison_index, int32_t number) const {
    std::string base = get_entry(comparison_index);
    if (base.empty())
        return {};
    if (number == 0)
        return base;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "_%d", number - 1);
    return base + buf;
}

std::string FNameSystem::get_entry(int32_t comparison_index) const {
    if (!ready() || comparison_index < 0)
        return {};

    auto it = cache_.find(comparison_index);
    if (it != cache_.end())
        return it->second;

    std::string name;
    if (offs_->uses_name_pool)
        name = read_from_pool(comparison_index);
    else
        name = read_from_array(comparison_index);

    if (!name.empty())
        cache_[comparison_index] = name;
    return name;
}

std::string FNameSystem::read_from_pool(int32_t index) const {
    // FNamePool layout (simplified, matches most UE4.23+/UE5 shipping builds):
    //   Blocks: FNameEntry** or contiguous block table at pool+0
    //   Entry:  uint16 header (length in high bits), then ANSI or wide chars
    //
    // Index encoding: block = index >> 16, offset = (index & 0xFFFF) * stride
    // Stride is often 2 (aligned to 2 bytes).

    constexpr int kBlockBits   = 16;
    constexpr int kBlockOffset = 0xFFFF;
    constexpr int kStride      = 2;

    const uint32_t block  = static_cast<uint32_t>(index) >> kBlockBits;
    const uint32_t offset = (static_cast<uint32_t>(index) & kBlockOffset) * kStride;

    // Blocks pointer: commonly GNames points at FNamePool; Blocks[block]
    // Layout used by many dumpers:
    //   uint8_t* Blocks[8192] at pool + 0x10 (varies) OR embedded
    // We use offsets from profile for current_block etc., but block table
    // address is typically gnames + 0x10 on pool implementations.

    // Try standard: Blocks at gnames_ + 0x10 (UE4.23+ default after Lock fields)
    uint64_t blocks_base = gnames_ + 0x10;
    uint64_t block_ptr = 0;
    if (!mem_->read(blocks_base + block * sizeof(uint64_t), block_ptr) || !block_ptr)
        return {};

    const uint64_t entry = block_ptr + offset;
    uint16_t header = 0;
    if (!mem_->read(entry, header))
        return {};

    // Header bit layout (common):
    //   bIsWide : 1
    //   Len     : 10 (or 15 depending on version) starting at bit 1 or 6
    const int len_off  = offs_->fnameentry_len_bit_offset ? offs_->fnameentry_len_bit_offset : 6;
    const int len_bits = offs_->fnameentry_len_bit_count ? offs_->fnameentry_len_bit_count : 10;
    const int len_mask = (1 << len_bits) - 1;
    const int len      = (header >> len_off) & len_mask;
    const bool is_wide = (header & 1) != 0;

    if (len <= 0 || len > 1024)
        return {};

    const uint64_t data = entry + (offs_->fnameentry_header_size ? offs_->fnameentry_header_size : 2);

    if (!is_wide) {
        auto bytes = mem_->read_bytes(data, static_cast<size_t>(len));
        if (static_cast<int>(bytes.size()) != len)
            return {};
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    auto wbytes = mem_->read_bytes(data, static_cast<size_t>(len) * 2);
    if (wbytes.size() < static_cast<size_t>(len) * 2)
        return {};
    std::string out;
    out.reserve(static_cast<size_t>(len));
    for (int i = 0; i < len; ++i) {
        const wchar_t wc = static_cast<wchar_t>(
            wbytes[static_cast<size_t>(i) * 2] |
            (wbytes[static_cast<size_t>(i) * 2 + 1] << 8));
        out.push_back(wc < 128 ? static_cast<char>(wc) : '?');
    }
    return out;
}

std::string FNameSystem::read_from_array(int32_t index) const {
    // TNameEntryArray: Chunks of FNameEntry*
    // GNames -> TNameEntryArray, GetEntry(index)
    constexpr int kElementsPerChunk = 16384;

    const int chunk_index = index / kElementsPerChunk;
    const int within      = index % kElementsPerChunk;

    // Typical: NumElements, NumChunks, then pointer to chunks array
    // Simplified layout: GNames is TNameEntryArray*
    // Many games: GNames is the object itself not a pointer — try both.

    uint64_t chunks_ptr = 0;
    // Common: +0x00 = chunks** or first fields
    if (!mem_->read(gnames_ + 0x00, chunks_ptr) || !chunks_ptr) {
        // Maybe gnames_ is already the chunks base
        chunks_ptr = gnames_;
    }

    uint64_t chunk = 0;
    if (!mem_->read(chunks_ptr + static_cast<uint64_t>(chunk_index) * 8, chunk) || !chunk)
        return {};

    uint64_t entry = 0;
    if (!mem_->read(chunk + static_cast<uint64_t>(within) * 8, entry) || !entry)
        return {};

    // FNameEntry: Index, then AnsiName[1024] or Header+string
    // Pre-pool: often [int32 Index][AnsiName...]
    char buf[1025]{};
    // Skip 0x0C or 0x10 header variants
    if (!mem_->read(entry + 0x10, buf, 1024)) {
        if (!mem_->read(entry + 0x0C, buf, 1024))
            return {};
    }
    buf[1024] = '\0';
    // Ensure printable
    for (int i = 0; i < 1024 && buf[i]; ++i) {
        if (static_cast<unsigned char>(buf[i]) < 0x20 && buf[i] != 0) {
            buf[i] = '\0';
            break;
        }
    }
    return std::string(buf);
}

} // namespace ue
