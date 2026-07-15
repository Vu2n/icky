#include "ue_fname_call.h"
#include "core/logger.h"
#include "core/memory.h"
#include "core/pattern.h"

#include <Windows.h>
#include <vector>
#include <cstring>
#include <cctype>

namespace icky::ue {
namespace {

#pragma pack(push, 1)
struct FNameRaw {
    int32_t ComparisonIndex;
    int32_t Number;
};

// TArray-like FString (UE4/UE5 x64)
struct FStringRaw {
    wchar_t* Data;
    int32_t  Num;
    int32_t  Max;
};
#pragma pack(pop)

bool is_exec(uint64_t addr) {
    if (!addr) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi)))
        return false;
    if (mbi.State != MEM_COMMIT) return false;
    const DWORD p = mbi.Protect & 0xFF;
    return p == PAGE_EXECUTE || p == PAGE_EXECUTE_READ ||
           p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
}

// Isolated SEH trampolines (no C++ objects with dtors)
int seh_call_fname_a(uint64_t fn, FNameRaw* name, FStringRaw* out) {
    __try {
        using Fn = void(*)(FNameRaw*, FStringRaw*);
        reinterpret_cast<Fn>(fn)(name, out);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// Some builds: ToString returns FString by value (hidden pointer sret in RCX)
// void FName::ToString(FString* sret) — actually member: RCX=this, RDX=sret? 
// MSVC: return struct > 8 bytes → RCX = sret, RDX = this for members... messy.
// Try: void(*)(FString* out, FNameRaw* name)  [sret first]
int seh_call_fname_b(uint64_t fn, FNameRaw* name, FStringRaw* out) {
    __try {
        using Fn = void(*)(FStringRaw*, FNameRaw*);
        reinterpret_cast<Fn>(fn)(out, name);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// FName passed by value in RCX (cmp|num<<32), FString* in RDX
int seh_call_fname_c(uint64_t fn, int32_t idx, int32_t number, FStringRaw* out) {
    __try {
        const uint64_t packed =
            static_cast<uint64_t>(static_cast<uint32_t>(idx)) |
            (static_cast<uint64_t>(static_cast<uint32_t>(number)) << 32);
        using Fn = void(*)(uint64_t, FStringRaw*);
        reinterpret_cast<Fn>(fn)(packed, out);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// sret first, FName by value second
int seh_call_fname_d(uint64_t fn, int32_t idx, int32_t number, FStringRaw* out) {
    __try {
        const uint64_t packed =
            static_cast<uint64_t>(static_cast<uint32_t>(idx)) |
            (static_cast<uint64_t>(static_cast<uint32_t>(number)) << 32);
        using Fn = void(*)(FStringRaw*, uint64_t);
        reinterpret_cast<Fn>(fn)(out, packed);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

std::string fstring_to_utf8(const FStringRaw& s) {
    if (!s.Data || s.Num <= 0 || s.Num > 4096) return {};
    if (!is_readable(s.Data, static_cast<size_t>(s.Num) * sizeof(wchar_t)))
        return {};
    std::string out;
    out.reserve(static_cast<size_t>(s.Num));
    for (int32_t i = 0; i < s.Num; ++i) {
        const wchar_t c = s.Data[i];
        if (c == 0) break;
        // Stop at first non-printable (AppendString can leave heap noise)
        if (c < 32 || c > 126) {
            if (c == '/' || c == '_' || c == '.' || c == '-') {
                out.push_back(static_cast<char>(c));
                continue;
            }
            break;
        }
        out.push_back(static_cast<char>(c));
    }
    // Trim trailing junk fragments if multiple tokens got glued
    // Prefer first path-like or identifier segment
    while (!out.empty() && (out.back() == '?' || out.back() < 32))
        out.pop_back();
    return out;
}

// Free FString allocator if it used game's FMemory — we can't free safely.
// Leak per call is bad for 50k names — reuse one buffer by clearing Num only
// and hoping AppendString appends; for ToString it usually replaces.
// Prefer ToString-style that sets Data via game allocator — leak is OK for dump once.

// Preferred convention once validated (-1 = try all)
static int g_pref_conv = -1;

std::string try_call(uint64_t fn, int32_t idx, int32_t number, int convention) {
    if (!is_exec(fn)) return {};

    FNameRaw name{idx, number};
    FStringRaw str{};
    str.Data = nullptr;
    str.Num = 0;
    str.Max = 0;

    int rc = -1;
    switch (convention) {
    case 0: rc = seh_call_fname_a(fn, &name, &str); break;
    case 1: rc = seh_call_fname_b(fn, &name, &str); break;
    case 2: rc = seh_call_fname_c(fn, idx, number, &str); break;
    case 3: rc = seh_call_fname_d(fn, idx, number, &str); break;
    default: return {};
    }

    if (rc != 0) return {};
    return fstring_to_utf8(str);
}

bool is_none(const std::string& s) {
    return s == "None" || s == "none" || s == "NONE";
}

bool looks_like_ue_name(const std::string& s) {
    if (s.size() < 1 || s.size() > 256) return false;
    // Allow None, Class, /Script/..., identifiers
    size_t good = 0;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '_' || c == '/' || c == '.' || c == '-')
            ++good;
    }
    return good == s.size();
}

} // namespace

std::string call_fname_to_string(uint64_t fn, int32_t comparison_index, int32_t number) {
    if (g_pref_conv >= 0) {
        auto s = try_call(fn, comparison_index, number, g_pref_conv);
        if (looks_like_ue_name(s) || is_none(s)) return s;
    }
    for (int conv = 0; conv < 4; ++conv) {
        auto s = try_call(fn, comparison_index, number, conv);
        if (looks_like_ue_name(s) || is_none(s)) {
            if (g_pref_conv < 0) g_pref_conv = conv;
            return s;
        }
    }
    return {};
}

bool try_validate_name_fn(uint64_t fn) {
    if (!fn || !is_exec(fn)) return false;

    for (int conv = 0; conv < 4; ++conv) {
        auto s0 = try_call(fn, 0, 0, conv);
        if (is_none(s0)) {
            auto s1 = try_call(fn, 1, 0, conv);
            auto s2 = try_call(fn, 2, 0, conv);
            ILOG_I("FName fn OK (conv=%d): [0]='%s' [1]='%s' [2]='%s'",
                   conv, s0.c_str(), s1.c_str(), s2.c_str());
            g_pref_conv = conv;
            return true;
        }
        if (!s0.empty())
            ILOG_D("FName fn candidate conv=%d got '%s' (not None)", conv, s0.c_str());
    }
    return false;
}

uint64_t find_fname_tostring(uint64_t module_base, size_t module_size,
                             uint64_t /*gnames*/, uint64_t preferred_candidate) {
    std::vector<uint64_t> candidates;

    auto add = [&](uint64_t a) {
        if (a && is_exec(a)) {
            for (auto c : candidates) if (c == a) return;
            candidates.push_back(a);
        }
    };

    if (preferred_candidate) add(preferred_candidate);

    // Broad prologues used by FName::ToString / AppendString / GetPlainNameString
    const char* pats[] = {
        // Common MSVC prologues with 8B 01 (mov eax, [rcx] = load ComparisonIndex)
        "48 89 5C 24 ?? 57 48 83 EC 20 8B 01",
        "48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC 20 8B 01",
        "48 89 5C 24 ?? 48 89 74 24 ?? 48 89 7C 24 ?? 41 56 48 83 EC 20 8B 01",
        "40 53 48 83 EC 20 8B 01 48 8B D9",
        "40 53 48 83 EC 30 8B 01 48 8B DA",
        "48 89 5C 24 ?? 55 56 57 48 8B EC 48 83 EC ?? 8B 01",
        "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC 20 8B 01",
        // LEA FNamePool then call — ToString body often starts with test/jz
        "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24",
        // AppendString alternate
        "48 89 5C 24 ?? 57 48 83 EC 30 48 8B F9 48 8B DA 48 8B 0D",
        "48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC 20 8B 19",
    };

    for (auto* p : pats) {
        // Collect multiple hits (scan repeatedly by advancing) — simple: first hit only per pat
        // Do chunked multi-hit scan
        auto pat = Pattern::parse(p);
        if (!pat) continue;
        const size_t n = pat->bytes.size();
        const auto* data = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(module_base));
        int hits = 0;
        for (size_t i = 0; i + n <= module_size && hits < 8; ++i) {
            if ((i & 0xFFF) == 0 && !is_readable(data + i, n)) {
                i += 0xFFF;
                continue;
            }
            bool ok = true;
            for (size_t j = 0; j < n; ++j) {
                if (pat->mask[j] && data[i + j] != pat->bytes[j]) {
                    ok = false;
                    break;
                }
            }
            if (!ok) continue;
            add(module_base + i);
            ++hits;
            i += n; // skip ahead
        }
    }

    // Also: functions that reference GNames via RIP after a short prologue —
    // too heavy; rely on patterns + preferred.

    ILOG_I("FName ToString candidates: %zu", candidates.size());
    for (uint64_t c : candidates) {
        if (try_validate_name_fn(c)) {
            ILOG_I("Selected FName ToString @ 0x%llX", (unsigned long long)c);
            return c;
        }
    }

    ILOG_W("No FName ToString candidate returned 'None' for index 0");
    return 0;
}

} // namespace icky::ue
