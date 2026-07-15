#include "memory_meta.h"
#include "core/logger.h"

#include <Windows.h>
#include <Psapi.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

#pragma comment(lib, "psapi.lib")

namespace icky::il2cpp {
namespace {

constexpr uint8_t  kMagicBytes[4] = {0xAF, 0x1B, 0xB1, 0xFA};
constexpr uint32_t kMagicU32      = 0xFAB11BAFu;

bool is_readable(const MEMORY_BASIC_INFORMATION& mbi) {
    if (mbi.State != MEM_COMMIT)
        return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
        return false;
    const DWORD p = mbi.Protect & 0xFF;
    return p == PAGE_READONLY || p == PAGE_READWRITE || p == PAGE_WRITECOPY ||
           p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
}

bool is_good_candidate(const MEMORY_BASIC_INFORMATION& mbi) {
    if (!is_readable(mbi) || mbi.RegionSize < 0x1000)
        return false;
    const DWORD p = mbi.Protect & 0xFF;
    // Metadata lives in RW private/mapped heaps, not code sections.
    if (p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY)
        return false;
    return true;
}

bool looks_like_metadata(const uint8_t* data, size_t avail) {
    if (avail < 0x200)
        return false;
    if (*reinterpret_cast<const uint32_t*>(data) != kMagicU32)
        return false;
    const int32_t version = *reinterpret_cast<const int32_t*>(data + 4);
    if (version < 16 || version > 100)
        return false;
    const int32_t stringSz = *reinterpret_cast<const int32_t*>(data + 28);
    if (stringSz < 0x100 || stringSz > 0x20000000)
        return false;
    const int32_t tdSz = *reinterpret_cast<const int32_t*>(data + 164);
    const uint32_t tdOff = *reinterpret_cast<const uint32_t*>(data + 160);
    if (tdSz < 0x40 || tdSz > 0x20000000)
        return false;
    if (tdOff < 0x100)
        return false;
    return true;
}

size_t required_size_from_header(const uint8_t* data, size_t avail) {
    constexpr size_t kMax = 512ull * 1024ull * 1024ull;
    size_t max_end = 0x200;
    for (int i = 0; i < 28; ++i) {
        const size_t off = 8 + static_cast<size_t>(i) * 8;
        if (off + 8 > avail)
            break;
        const uint32_t o = *reinterpret_cast<const uint32_t*>(data + off);
        const int32_t  s = *reinterpret_cast<const int32_t*>(data + off + 4);
        if (s <= 0 || s > 0x20000000 || o == 0)
            continue;
        const size_t end = static_cast<size_t>(o) + static_cast<size_t>(s);
        if (end > max_end && end < kMax)
            max_end = end;
    }
    if (max_end < 0x1000)
        max_end = (std::min)(avail, size_t{0x100000});
    return (std::min)(max_end, kMax);
}

size_t readable_span_from(const uint8_t* start, size_t want) {
    size_t got = 0;
    const auto* p = start;
    while (got < want) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0)
            break;
        if (!is_readable(mbi))
            break;
        const auto* base = static_cast<const uint8_t*>(mbi.BaseAddress);
        const size_t region_end = static_cast<size_t>(mbi.RegionSize);
        const size_t offset_in  = static_cast<size_t>(p - base);
        if (offset_in >= region_end)
            break;
        const size_t chunk = region_end - offset_in;
        const size_t take  = (std::min)(chunk, want - got);
        got += take;
        p += take;
        if (take < chunk)
            break;
    }
    return got;
}

struct Region {
    const uint8_t* base = nullptr;
    size_t size = 0;
    bool prioritize = false;
};

void collect_regions(std::vector<Region>& out) {
    out.clear();
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    auto* addr = static_cast<uint8_t*>(si.lpMinimumApplicationAddress);
    auto* max_addr = static_cast<uint8_t*>(si.lpMaximumApplicationAddress);

    while (addr < max_addr) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(addr, &mbi, sizeof(mbi)) == 0)
            break;
        auto* next = static_cast<uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;

        if (is_good_candidate(mbi)) {
            Region r;
            r.base = static_cast<const uint8_t*>(mbi.BaseAddress);
            r.size = mbi.RegionSize;
            r.prioritize =
                (mbi.Type == MEM_PRIVATE || mbi.Type == MEM_MAPPED) &&
                mbi.RegionSize >= 0x10000;
            out.push_back(r);
        } else if (is_readable(mbi) && mbi.RegionSize >= 0x1000) {
            Region r;
            r.base = static_cast<const uint8_t*>(mbi.BaseAddress);
            r.size = mbi.RegionSize;
            r.prioritize = false;
            out.push_back(r);
        }
        addr = next;
    }

    std::stable_partition(out.begin(), out.end(),
                          [](const Region& r) { return r.prioritize; });
}

bool try_candidate(const uint8_t* data, size_t region_size, size_t offset,
                   const uint8_t*& best, size_t& best_size, uintptr_t& best_at) {
    if (offset + 0x200 > region_size)
        return false;
    const uint8_t* p = data + offset;
    if (!looks_like_metadata(p, region_size - offset))
        return false;

    size_t need = required_size_from_header(p, region_size - offset);
    const size_t span = readable_span_from(p, need);
    if (span < 0x1000)
        return false;
    need = (std::min)(need, span);

    if (need > best_size) {
        best = p;
        best_size = need;
        best_at = reinterpret_cast<uintptr_t>(p);
        return true;
    }
    return false;
}

// SEH-isolated copy — no C++ objects with destructors.
size_t safe_copy_pages(const uint8_t* src, uint8_t* dst, size_t size) {
    size_t written = 0;
    __try {
        for (size_t o = 0; o < size; o += 0x1000) {
            const size_t chunk = (size - o < 0x1000) ? (size - o) : 0x1000;
            memcpy(dst + o, src + o, chunk);
            written += chunk;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return written;
    }
    return written;
}

} // namespace

bool looks_obfuscated_name(const char* name) {
    if (!name || !name[0])
        return true;
    const size_t len = strlen(name);
    if (len <= 2 && !isalpha(static_cast<unsigned char>(name[0])))
        return true;
    int weird = 0, alnum = 0;
    for (size_t i = 0; i < len; ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (isalnum(c) || c == '_' || c == '`' || c == '<' || c == '>' || c == '.' || c == '/')
            ++alnum;
        else
            ++weird;
    }
    if (alnum == 0)
        return true;
    if (weird > alnum && len > 4)
        return true;
    bool all_hexish = true;
    for (size_t i = 0; i < len; ++i) {
        const char c = name[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            all_hexish = false;
            break;
        }
    }
    return all_hexish && len >= 6;
}

bool looks_obfuscated_name(const std::string& name) {
    return looks_obfuscated_name(name.c_str());
}

float metadata_name_quality(const MetadataBlob& blob, int sample_count) {
    if (blob.data.size() < sizeof(Il2CppGlobalMetadataHeader) || sample_count <= 0)
        return 0.f;

    Il2CppGlobalMetadataHeader hdr{};
    std::memcpy(&hdr, blob.data.data(), sizeof(hdr));
    if (hdr.sanity != static_cast<int32_t>(kMagicU32))
        return 0.f;
    if (hdr.typeDefinitionsOffset < 0 || hdr.typeDefinitionsSize <= 0)
        return 0.f;
    if (static_cast<size_t>(hdr.typeDefinitionsOffset + hdr.typeDefinitionsSize) > blob.data.size())
        return 0.f;

    const int candidates[] = {88, 92, 96, 100, 104, 108, 112, 116, 120};
    int stride = 0;
    for (int c : candidates) {
        if (hdr.typeDefinitionsSize % c != 0)
            continue;
        stride = c;
        break;
    }
    if (!stride)
        stride = 92;

    const int count = hdr.typeDefinitionsSize / stride;
    if (count <= 0)
        return 0.f;

    int good = 0, checked = 0;
    const int step = (std::max)(1, count / sample_count);
    for (int i = 0; i < count && checked < sample_count; i += step) {
        const uint8_t* row =
            blob.data.data() + hdr.typeDefinitionsOffset + static_cast<size_t>(i) * stride;
        int32_t nameIndex = 0;
        std::memcpy(&nameIndex, row, 4);
        auto s = meta_string(blob, nameIndex);
        ++checked;
        if (!s.empty() && !looks_obfuscated_name(s) && s.size() < 200) {
            // Prefer names that look like C# identifiers
            const unsigned char c0 = static_cast<unsigned char>(s[0]);
            if (isalpha(c0) || c0 == '_' || c0 == '<' || c0 == '`')
                ++good;
        }
    }
    if (checked == 0)
        return 0.f;
    return static_cast<float>(good) / static_cast<float>(checked);
}

bool metadata_names_usable(const MetadataBlob& blob) {
    const float q = metadata_name_quality(blob, 80);
    ILOG_I("Metadata name quality: %.0f%%", q * 100.f);
    // Disk-encrypted heaps often score near 0; real dumps usually > 40%.
    return q >= 0.25f;
}

std::optional<MetadataBlob> scan_decrypted_metadata_in_memory() {
    ILOG_I("Scanning process memory for decrypted global-metadata (0xFAB11BAF)...");

    std::vector<Region> regions;
    collect_regions(regions);
    if (regions.empty()) {
        ILOG_W("Memory meta: no readable regions");
        return std::nullopt;
    }

    const uint8_t* best = nullptr;
    size_t best_size = 0;
    uintptr_t best_at = 0;

    // Pass 1: allocation bases
    for (const auto& reg : regions)
        try_candidate(reg.base, reg.size, 0, best, best_size, best_at);

    // Pass 2: deep scan private heaps
    if (!best) {
        ILOG_I("Memory meta: deep scan private regions (%zu regions)...", regions.size());
        for (const auto& reg : regions) {
            if (!reg.prioritize)
                continue;
            for (size_t off = 0; off + 4 < reg.size; off += 0x1000) {
                if (reg.base[off] != kMagicBytes[0])
                    continue;
                if (*reinterpret_cast<const uint32_t*>(reg.base + off) != kMagicU32)
                    continue;
                try_candidate(reg.base, reg.size, off, best, best_size, best_at);
            }
            if (!best && reg.size <= 32ull * 1024ull * 1024ull) {
                for (size_t off = 0; off + 4 < reg.size; off += 0x40) {
                    if (reg.base[off] != kMagicBytes[0])
                        continue;
                    if (*reinterpret_cast<const uint32_t*>(reg.base + off) != kMagicU32)
                        continue;
                    try_candidate(reg.base, reg.size, off, best, best_size, best_at);
                }
            }
            if (best && best_size > 0x10000)
                break;
        }
    }

    if (!best || best_size < 0x1000) {
        ILOG_W("Memory meta: no decrypted metadata found (wait until game fully loads)");
        return std::nullopt;
    }

    std::vector<uint8_t> copy(best_size);
    const size_t written = safe_copy_pages(best, copy.data(), best_size);
    if (written < 0x1000) {
        ILOG_W("Memory meta: access violation while reading candidate");
        return std::nullopt;
    }
    if (written < best_size)
        copy.resize(written);

    MetadataBlob blob;
    blob.data = std::move(copy);
    blob.was_encrypted = true; // came from runtime decrypt buffer
    blob.path = "memory://0x" +
                ([](uintptr_t a) {
                    char buf[32]{};
                    sprintf_s(buf, "%llX", static_cast<unsigned long long>(a));
                    return std::string(buf);
                })(best_at);

    if (blob.data.size() >= 8) {
        int32_t ver = 0;
        std::memcpy(&ver, blob.data.data() + 4, 4);
        blob.version = ver;
    }

    ILOG_I("Memory meta: found %zu bytes at 0x%llX (version %d)",
           blob.data.size(),
           static_cast<unsigned long long>(best_at),
           blob.version);

    if (!metadata_names_usable(blob)) {
        ILOG_W("Memory meta: candidate found but name quality still poor — ignoring");
        return std::nullopt;
    }
    return blob;
}

} // namespace icky::il2cpp
