#include "ue_internals.h"
#include "ue_fname_call.h"
#include "core/logger.h"
#include "core/memory.h"

#include <cctype>

namespace icky::ue {

void NamePool::init(uint64_t gnames, const Layout& layout, uint64_t append_string) {
    gnames_ = gnames;
    layout_ = layout;
    append_string_ = append_string;
    cache_.clear();
    use_game_fn_ = false;

    if (append_string_ && try_validate_name_fn(append_string_)) {
        use_game_fn_ = true;
        ILOG_I("NamePool: using game FName ToString/AppendString @ 0x%llX",
               (unsigned long long)append_string_);
    } else if (append_string_) {
        ILOG_W("NamePool: provided FName fn 0x%llX did not validate — pool/decrypt only",
               (unsigned long long)append_string_);
    }
}

std::string NamePool::decrypt_entry(std::string s, int32_t index) const {
    if (s.empty()) return s;

    auto printable = [](const std::string& t) {
        if (t.empty()) return false;
        for (unsigned char c : t) {
            if (c < 0x20 || c > 0x7E) return false;
        }
        return true;
    };

    if (s == "None" || printable(s)) return s;

    // 1) Single-byte XOR whole string
    for (int k = 1; k < 256; ++k) {
        std::string t = s;
        for (char& c : t)
            c = static_cast<char>(static_cast<uint8_t>(c) ^ static_cast<uint8_t>(k));
        if (t == "None" || (printable(t) && t.size() >= 2))
            return t;
    }

    // 2) Rolling XOR
    for (int k = 1; k < 256; ++k) {
        std::string t = s;
        uint8_t key = static_cast<uint8_t>(k);
        for (char& c : t) {
            c = static_cast<char>(static_cast<uint8_t>(c) ^ key);
            key = static_cast<uint8_t>(key + 1);
        }
        if (t == "None" || (printable(t) && t.size() >= 2))
            return t;
    }

    // 3) Index-derived key (common in some protectors)
    {
        const uint8_t key = static_cast<uint8_t>(index);
        std::string t = s;
        for (size_t i = 0; i < t.size(); ++i)
            t[i] = static_cast<char>(static_cast<uint8_t>(t[i]) ^ static_cast<uint8_t>(key + i));
        if (t == "None" || printable(t))
            return t;
    }

    // 4) Layout-configured XOR
    if (layout_.fname_xor_key) {
        std::string t = s;
        for (char& c : t)
            c = static_cast<char>(static_cast<uint8_t>(c) ^ layout_.fname_xor_key);
        if (printable(t)) return t;
    }

    return s;
}

std::string NamePool::get_raw(int32_t index) const {
    if (!gnames_ || index < 0) return {};

    const uint32_t block  = static_cast<uint32_t>(index) >> 16;
    const uint32_t offset = (static_cast<uint32_t>(index) & 0xFFFF) * 2;

    uint64_t block_ptr = 0;
    const uint64_t blocks = gnames_ + static_cast<uint64_t>(layout_.pool_blocks_off);
    if (!Mem::read(blocks + static_cast<uint64_t>(block) * 8, block_ptr) || !block_ptr)
        return {};

    const uint64_t entry = block_ptr + offset;
    uint16_t header = 0;
    if (!Mem::read(entry, header)) return {};

    const int len_off  = layout_.fname_len_bit_off;
    const int len_bits = layout_.fname_len_bit_count;
    const int len = (header >> len_off) & ((1 << len_bits) - 1);
    const bool wide = (header & 1) != 0;
    if (len <= 0 || len > 1024) return {};

    const uint64_t data = entry + layout_.fname_header_size;
    std::string result;
    if (!wide) {
        auto b = Mem::bytes(data, static_cast<size_t>(len));
        if ((int)b.size() != len) return {};
        result.assign(reinterpret_cast<char*>(b.data()), b.size());
    } else {
        result = Mem::wstr(data, static_cast<size_t>(len));
    }
    return decrypt_entry(std::move(result), index);
}

std::string NamePool::via_append_string(int32_t index, int32_t number) const {
    if (!append_string_) return {};
    return call_fname_to_string(append_string_, index, number);
}

std::string NamePool::get(int32_t comparison_index, int32_t number) const {
    const int64_t key = (static_cast<int64_t>(comparison_index) << 32) ^
                        static_cast<uint32_t>(number);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;

    std::string base;

    // Prefer game ToString when validated (handles all encryption)
    if (use_game_fn_) {
        base = via_append_string(comparison_index, number);
        // ToString usually already includes _N suffix for number != 0
        if (!base.empty()) {
            cache_[key] = base;
            return base;
        }
    }

    base = get_raw(comparison_index);

    // If pool still garbage, try game fn even if not pre-validated
    if (!use_game_fn_ && (base.empty() || base[0] < 0x20 || base.find('\x01') != std::string::npos)) {
        auto via = via_append_string(comparison_index, number);
        if (!via.empty()) {
            cache_[key] = via;
            return via;
        }
    }

    if (base.empty()) return {};

    // Only append number for pool path (game ToString already did it)
    if (!use_game_fn_ && number > 0)
        base += "_" + std::to_string(number - 1);

    cache_[key] = base;
    return base;
}

bool NamePool::looks_sane() const {
    // Strict: index 0 must be None (via game fn or pool)
    if (get(0, 0) == "None") return true;
    return false;
}

std::string Layout::describe() const {
    char buf[256];
    snprintf(buf, sizeof(buf),
             "ObjObjects=+0x%X Objects=+0x%X Num=+0x%X Item=0x%X Chunked=%d PoolBlocks=+0x%X",
             objobjects_offset, objects_ptr, num_elements, item_size, chunked ? 1 : 0,
             pool_blocks_off);
    return buf;
}

} // namespace icky::ue
