#include "ue_internals.h"
#include "ue_fname_call.h"
#include "core/logger.h"
#include "core/memory.h"
#include "core/modules.h"
#include "core/pattern.h"

#include <algorithm>
#include <cmath>
#include <cctype>

namespace icky::ue {
namespace {

struct Pat {
    const char* ida;
    int disp;
    int len;
    const char* tag;
};

// Broad set of shipping signatures (UE4.2x–UE5.4-ish)
const Pat kGObjectsPats[] = {
    {"48 8B 05 ?? ?? ?? ?? 48 8B 0C C8 48 8D 04 D1", 3, 7, "gobj_a"},
    {"48 8B 05 ?? ?? ?? ?? 48 8B 0C C8 4C 8D 04 D1", 3, 7, "gobj_b"},
    {"48 8B 05 ?? ?? ?? ?? 48 8B 0C C8", 3, 7, "gobj_c"},
    {"48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8D 8D", 3, 7, "gobj_lea"},
    {"4C 8B 15 ?? ?? ?? ?? 4D 85 D2 74", 3, 7, "gobj_r15"},
    {"48 8B 1D ?? ?? ?? ?? 48 85 DB 75 ?? B9", 3, 7, "gobj_rbx"},
    // GUObjectArray direct LEA
    {"48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 4C 8B C0", 3, 7, "gobj_lea2"},
    {"48 8D 15 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? E8", 3, 7, "gobj_lea3"},
};

const Pat kGNamesPats[] = {
    {"48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? C6 05 ?? ?? ?? ?? 01 0F 10 03", 3, 7, "gnames_a"},
    {"48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? C6 05", 3, 7, "gnames_b"},
    {"48 8D 05 ?? ?? ?? ?? EB ?? 48 8D 0D ?? ?? ?? ?? E8", 3, 7, "gnames_c"},
    {"48 8D 0D ?? ?? ?? ?? 48 03 D0 E8", 3, 7, "gnames_d"},
    {"4C 8D 05 ?? ?? ?? ?? EB ?? 48 8D 0D", 3, 7, "gnames_e"},
    {"48 8D 0D ?? ?? ?? ?? E9 ?? ?? ?? ?? 48 8B D0", 3, 7, "gnames_f"},
    // FNamePool
    {"48 8D 35 ?? ?? ?? ?? EB ?? 48 8D 0D", 3, 7, "gnames_rsi"},
};

const Pat kGWorldPats[] = {
    {"48 8B 1D ?? ?? ?? ?? 48 85 DB 74 ?? 41 B0 01", 3, 7, "gworld_a"},
    {"48 8B 05 ?? ?? ?? ?? 48 8B 88 ?? ?? ?? ?? 48 85 C9 74", 3, 7, "gworld_b"},
    {"48 8B 1D ?? ?? ?? ?? 48 85 DB 74 3B", 3, 7, "gworld_c"},
};

const Pat kProcessEventPats[] = {
    // function start patterns (return match address, not RIP)
    {"40 55 56 57 41 54 41 55 41 56 41 57 48 81 EC", 0, 0, "pe_a"},
    {"48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC 20 49 8B F0", 0, 0, "pe_b"},
};

const Pat kAppendStringPats[] = {
    // FName::AppendString-ish
    {"48 89 5C 24 ?? 57 48 83 EC 20 8B 01 48 8B FA 8B", 0, 0, "append_a"},
    {"48 89 5C 24 ?? 48 89 74 24 ?? 48 89 7C 24 ?? 41 56 48 83 EC 20 8B 01", 0, 0, "append_b"},
    {"48 89 5C 24 ?? 55 56 57 48 8B EC 48 83 EC 30 8B", 0, 0, "append_c"},
};

uint64_t scan_rip_list(uint64_t base, size_t size, const Pat* pats, size_t count, const char** hit_tag = nullptr) {
    for (size_t i = 0; i < count; ++i) {
        if (pats[i].len == 0) continue;
        uint64_t a = find_rip(base, size, pats[i].ida, pats[i].disp, pats[i].len);
        if (a) {
            if (hit_tag) *hit_tag = pats[i].tag;
            ILOG_I("  hit %s → 0x%llX", pats[i].tag, (unsigned long long)a);
            return a;
        }
    }
    return 0;
}

uint64_t scan_func_list(uint64_t base, size_t size, const Pat* pats, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (pats[i].len != 0) continue;
        uint64_t a = scan_ida(base, size, pats[i].ida);
        if (a) {
            ILOG_I("  hit %s → 0x%llX", pats[i].tag, (unsigned long long)a);
            return a;
        }
    }
    return 0;
}

bool valid_num(int32_t n) {
    return n >= 100 && n <= 10'000'000;
}

int score_object_ptr(uint64_t obj) {
    if (!Mem::valid_user_ptr(obj)) return 0;
    uint64_t vt = 0;
    if (!Mem::read(obj, vt) || !Mem::valid_user_ptr(vt)) return 0;
    // vtable should point into a module image roughly
    int score = 1;
    uint64_t cls = 0;
    if (Mem::read(obj + 0x10, cls) && Mem::valid_user_ptr(cls)) score += 2;
    uint64_t name = 0;
    if (Mem::read(obj + 0x18, name)) score += 1; // FName raw
    return score;
}

// Try to read object pointer at index with a candidate layout
uint64_t try_get_object(uint64_t gobjects, const Layout& L, int32_t index) {
    uint64_t arr = gobjects;
    if (L.gobjects_is_outer)
        arr = gobjects + L.objobjects_offset;

    uint64_t objects = 0;
    if (!Mem::read(arr + L.objects_ptr, objects) || !objects)
        return 0;

    const int32_t item = L.item_size > 0 ? L.item_size : 0x18;
    uint64_t item_addr = 0;
    if (L.chunked) {
        const int32_t pc = L.per_chunk > 0 ? L.per_chunk : 65536;
        const int32_t ci = index / pc;
        const int32_t wi = index % pc;
        uint64_t chunk = 0;
        if (!Mem::read(objects + static_cast<uint64_t>(ci) * 8, chunk) || !chunk)
            return 0;
        item_addr = chunk + static_cast<uint64_t>(wi) * item;
    } else {
        item_addr = objects + static_cast<uint64_t>(index) * item;
    }
    uint64_t obj = 0;
    Mem::read(item_addr + L.item_object, obj);
    return obj;
}

int32_t try_num(uint64_t gobjects, const Layout& L) {
    uint64_t arr = gobjects;
    if (L.gobjects_is_outer)
        arr = gobjects + L.objobjects_offset;
    int32_t n = 0;
    Mem::read(arr + L.num_elements, n);
    return n;
}

int score_layout(uint64_t gobjects, const Layout& L) {
    const int32_t n = try_num(gobjects, L);
    if (!valid_num(n)) return -1;

    int score = 10;
    // Sample several indices
    int valid = 0;
    const int samples[] = {0, 1, 2, 3, 4, 5, 10, 50, 100, 200, 500, 1000};
    for (int idx : samples) {
        if (idx >= n) break;
        uint64_t obj = try_get_object(gobjects, L, idx);
        int s = score_object_ptr(obj);
        if (s > 0) ++valid;
        score += s;
    }
    if (valid < 3) return -1;
    // Prefer sensible object counts
    if (n > 5000 && n < 2'000'000) score += 20;
    return score;
}

// Also try treating the scanned address as pointer-to-array
std::vector<uint64_t> expand_candidates(uint64_t addr) {
    std::vector<uint64_t> c;
    if (!addr) return c;
    c.push_back(addr);
    uint64_t p = Mem::ptr(addr);
    if (Mem::valid_user_ptr(p)) {
        c.push_back(p);
        uint64_t p2 = Mem::ptr(p);
        if (Mem::valid_user_ptr(p2))
            c.push_back(p2);
    }
    // Align down sometimes patterns hit mid-struct — try -0x10..0
    for (int off = 0; off <= 0x30; off += 8) {
        if (addr > static_cast<uint64_t>(off))
            c.push_back(addr - off);
    }
    // unique
    std::sort(c.begin(), c.end());
    c.erase(std::unique(c.begin(), c.end()), c.end());
    return c;
}

bool try_name_pool_config(uint64_t gnames, Layout& L) {
    if (!gnames) return false;

    const int32_t block_offs[] = {0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30};
    const int len_offs[] = {1, 6};
    const int len_bits[] = {10, 15, 9};

    int best = -1;
    Layout bestL = L;

    for (int bo : block_offs) {
        for (int lo : len_offs) {
            for (int lb : len_bits) {
                Layout t = L;
                t.pool_blocks_off = bo;
                t.fname_len_bit_off = lo;
                t.fname_len_bit_count = lb;
                t.fname_encrypted = false;
                t.fname_xor_key = 0;

                NamePool pool;
                pool.init(gnames, t, 0);
                int score = 0;
                auto n0 = pool.get(0, 0);
                if (n0 == "None") score += 100;
                // xor single-byte on raw if not None
                if (score == 0) {
                    // manual raw
                    for (int k = 0; k < 256; ++k) {
                        Layout te = t;
                        te.fname_encrypted = k != 0;
                        te.fname_xor_key = static_cast<uint8_t>(k);
                        NamePool p2;
                        p2.init(gnames, te, 0);
                        if (p2.get(0, 0) == "None") {
                            score = 100;
                            t = te;
                            break;
                        }
                    }
                }
                if (pool.looks_sane()) score += 30;
                // Check a few names look like identifiers
                for (int i = 1; i < 20; ++i) {
                    auto s = pool.get(i, 0);
                    if (s.size() >= 2 && s.size() < 40) {
                        bool id = true;
                        for (size_t j = 0; j < s.size(); ++j) {
                            unsigned char c = s[j];
                            if (!(std::isalnum(c) || c == '_' || c == '/')) { id = false; break; }
                        }
                        if (id) score += 2;
                    }
                }
                if (score > best) {
                    best = score;
                    bestL = t;
                }
            }
        }
    }

    if (best < 20) return false;
    L = bestL;
    NamePool verify;
    verify.init(gnames, L, 0);
    ILOG_I("FName pool config: blocks=+0x%X None='%s' score=%d",
           L.pool_blocks_off, verify.get(0, 0).c_str(), best);
    return true;
}

} // namespace

ModuleInfo find_game_module() {
    auto by_name = find_module(process_name().c_str());
    if (by_name && by_name->size > 0x100000)
        return *by_name;

    ModuleInfo best{};
    for (auto& m : list_modules()) {
        if (m.name.find("-Win64-Shipping") != std::string::npos ||
            m.name.find("Shipping.exe") != std::string::npos) {
            if (m.size > best.size) best = m;
        }
    }
    if (best.base) return best;

    // Largest non-system module
    for (auto& m : list_modules()) {
        auto lower = m.name;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower.find("windows") != std::string::npos) continue;
        if (lower.find("ntdll") != std::string::npos) continue;
        if (lower.find("kernel") != std::string::npos) continue;
        if (lower.find("d3d") != std::string::npos) continue;
        if (lower.find("vcruntime") != std::string::npos) continue;
        if (lower.find("msvcp") != std::string::npos) continue;
        if (lower.find("icky") != std::string::npos) continue;
        if (m.size > best.size) best = m;
    }
    return best;
}

bool find_globals(Globals& g) {
    auto mod = find_game_module();
    if (!mod.base) {
        ILOG_E("UE: no game module");
        return false;
    }
    g.module_base = mod.base;
    g.module_size = mod.size;
    g.module_name = mod.name;
    ILOG_I("UE module: %s @ 0x%llX size=0x%zX",
           mod.name.c_str(), (unsigned long long)mod.base, mod.size);

    const char* tag = nullptr;
    g.gobjects = scan_rip_list(mod.base, mod.size, kGObjectsPats,
                               sizeof(kGObjectsPats) / sizeof(kGObjectsPats[0]), &tag);
    g.gnames = scan_rip_list(mod.base, mod.size, kGNamesPats,
                             sizeof(kGNamesPats) / sizeof(kGNamesPats[0]), &tag);
    g.gworld = scan_rip_list(mod.base, mod.size, kGWorldPats,
                             sizeof(kGWorldPats) / sizeof(kGWorldPats[0]), &tag);
    g.process_event = scan_func_list(mod.base, mod.size, kProcessEventPats,
                                     sizeof(kProcessEventPats) / sizeof(kProcessEventPats[0]));
    g.append_string = scan_func_list(mod.base, mod.size, kAppendStringPats,
                                     sizeof(kAppendStringPats) / sizeof(kAppendStringPats[0]));

    ILOG_I("UE candidates: GObjects=0x%llX GNames=0x%llX GWorld=0x%llX PE=0x%llX Append=0x%llX",
           (unsigned long long)g.gobjects, (unsigned long long)g.gnames,
           (unsigned long long)g.gworld, (unsigned long long)g.process_event,
           (unsigned long long)g.append_string);

    return g.gobjects != 0 || g.gnames != 0;
}

bool discover_layout(Globals& g, Layout& layout) {
    layout = Layout{};

    // --- GObjects layout brute force ---
    int best_score = -1;
    uint64_t best_addr = 0;
    Layout best_layout = layout;

    auto candidates = expand_candidates(g.gobjects);
    // If scan failed entirely, we cannot brute whole image here (too big)
    if (candidates.empty()) {
        ILOG_E("UE: no GObjects candidates");
        return false;
    }

    // Outer offsets: 0 = already ObjObjects, 0x10 = classic GUObjectArray
    const int32_t outer_offs[] = {0x00, 0x10, 0x18, 0x20, 0x30, 0x40};
    const int32_t num_offs[]   = {0x14, 0x0C, 0x10, 0x18, 0x1C, 0x20, 0x24, 0x28};
    const int32_t obj_offs[]   = {0x00, 0x08, 0x10};
    const int32_t item_sizes[] = {0x18, 0x10, 0x20, 0x24};
    const int32_t chunks[]     = {64 * 1024, 65536, 0}; // 0 = flat

    for (uint64_t cand : candidates) {
        for (int32_t outer : outer_offs) {
            for (int32_t numo : num_offs) {
                for (int32_t objo : obj_offs) {
                    for (int32_t isz : item_sizes) {
                        for (int32_t pc : chunks) {
                            Layout L;
                            L.gobjects_is_outer = (outer != 0);
                            L.objobjects_offset = outer;
                            L.num_elements = numo;
                            L.objects_ptr = objo;
                            L.item_size = isz;
                            L.chunked = (pc != 0);
                            L.per_chunk = pc ? pc : 65536;

                            int sc = score_layout(cand, L);
                            if (sc > best_score) {
                                best_score = sc;
                                best_addr = cand;
                                best_layout = L;
                            }
                        }
                    }
                }
            }
        }
    }

    if (best_score < 0) {
        ILOG_E("UE: failed to find working GObjects layout (scanned %zu candidates)",
               candidates.size());
        // Keep raw addresses for Offsets.hpp debugging
        return false;
    }

    g.gobjects = best_addr;
    layout = best_layout;
    ILOG_I("UE GObjects layout score=%d addr=0x%llX num=%d | %s",
           best_score, (unsigned long long)best_addr, try_num(best_addr, layout),
           layout.describe().c_str());

    // --- GNames ---
    auto name_cands = expand_candidates(g.gnames);
    bool name_ok = false;
    for (uint64_t nc : name_cands) {
        Layout tmp = layout;
        if (try_name_pool_config(nc, tmp)) {
            g.gnames = nc;
            layout.pool_blocks_off = tmp.pool_blocks_off;
            layout.fname_len_bit_off = tmp.fname_len_bit_off;
            layout.fname_len_bit_count = tmp.fname_len_bit_count;
            layout.fname_encrypted = tmp.fname_encrypted;
            layout.fname_xor_key = tmp.fname_xor_key;
            name_ok = true;
            break;
        }
    }
    if (!name_ok) {
        ILOG_W("UE: FName pool not fully validated — dump may miss names");
        // Keep first candidate
        if (!name_cands.empty()) g.gnames = name_cands.front();
    }

    // Resolve FName::ToString / AppendString (critical for encrypted names)
    {
        const uint64_t resolved = find_fname_tostring(
            g.module_base, g.module_size, g.gnames, g.append_string);
        if (resolved) {
            g.append_string = resolved;
            ILOG_I("UE FName ToString validated @ 0x%llX", (unsigned long long)resolved);
        } else {
            ILOG_W("UE FName ToString NOT found — names may stay encrypted");
        }
    }

    // Quick UObject field probe using first valid object
    {
        int32_t n = try_num(g.gobjects, layout);
        uint64_t sample = 0;
        for (int i = 0; i < std::min(n, 200); ++i) {
            uint64_t o = try_get_object(g.gobjects, layout, i);
            if (score_object_ptr(o) >= 3) { sample = o; break; }
        }
        if (sample) {
            for (int32_t off : {0x10, 0x18, 0x08, 0x20}) {
                uint64_t cls = Mem::ptr(sample + off);
                if (!Mem::valid_user_ptr(cls)) continue;
                uint64_t cls2 = Mem::ptr(cls + off);
                if (Mem::valid_user_ptr(cls2)) {
                    layout.uobject_class = off;
                    break;
                }
            }
            layout.uobject_name = layout.uobject_class + 8;
            layout.uobject_outer = layout.uobject_name + 8;
            ILOG_I("UE UObject probe: Class=+0x%X Name=+0x%X Outer=+0x%X",
                   layout.uobject_class, layout.uobject_name, layout.uobject_outer);
        }
    }

    g.engine_guess = "UE4/5 (auto)";
    return true;
}

} // namespace icky::ue
