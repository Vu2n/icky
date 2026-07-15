#include "deobf.h"
#include "core/logger.h"
#include "core/memory.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace icky::il2cpp {
namespace {

bool is_hex_char(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool is_facepunch_hash(const std::string& name) {
    // % + 40 hex chars (SHA-1 hex)
    if (name.size() == 41 && name[0] == '%') {
        for (size_t i = 1; i < 41; ++i)
            if (!is_hex_char(name[i]))
                return false;
        return true;
    }
    // _ + long hex
    if (name.size() >= 33 && name[0] == '_') {
        size_t hex = 0;
        for (size_t i = 1; i < name.size(); ++i) {
            if (is_hex_char(name[i]))
                ++hex;
            else
                return false;
        }
        return hex >= 32;
    }
    return false;
}

std::string sanitize_ident(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
            o.push_back(c);
        else if (c == '`' || c == '<' || c == '>' || c == '.' || c == ',' || c == '-' || c == '/')
            o.push_back('_');
    }
    while (!o.empty() && o.front() == '_')
        o.erase(o.begin());
    if (o.empty())
        o = "renamed";
    if (std::isdigit(static_cast<unsigned char>(o[0])))
        o.insert(o.begin(), 'n');
    return o;
}

// Strip namespaces / generics / arrays → simple type token (or empty if hashed).
std::string simple_type(const std::string& type_name) {
    std::string t = type_name;
    while (t.size() >= 2 && t[t.size() - 2] == '[' && t.back() == ']')
        t.resize(t.size() - 2);

    // Prefer clear type inside generic: Foo<%hash%> ignored; %hash%<PlayerEyes> → PlayerEyes
    size_t lt = t.find('<');
    if (lt != std::string::npos) {
        size_t gt = t.rfind('>');
        if (gt != std::string::npos && gt > lt + 1) {
            std::string inner = t.substr(lt + 1, gt - lt - 1);
            // first type arg
            size_t comma = inner.find(',');
            if (comma != std::string::npos)
                inner = inner.substr(0, comma);
            // strip nested
            size_t nested = inner.find('<');
            if (nested != std::string::npos)
                inner = inner.substr(0, nested);
            size_t dot = inner.find_last_of('.');
            if (dot != std::string::npos)
                inner = inner.substr(dot + 1);
            if (!inner.empty() && inner[0] != '%' && !is_facepunch_hash(inner)) {
                // wrapper name
                std::string outer = t.substr(0, lt);
                size_t odot = outer.find_last_of('.');
                if (odot != std::string::npos)
                    outer = outer.substr(odot + 1);
                if (!outer.empty() && outer[0] != '%' && !is_facepunch_hash(outer))
                    return sanitize_ident(outer + "_" + inner);
                return sanitize_ident("enc_" + inner);
            }
        }
        t = t.substr(0, lt);
    }

    size_t tick = t.find('`');
    if (tick != std::string::npos)
        t = t.substr(0, tick);
    size_t dot = t.find_last_of('.');
    if (dot != std::string::npos)
        t = t.substr(dot + 1);
    if (t.empty() || t[0] == '%' || is_facepunch_hash(t))
        return {};
    return sanitize_ident(t);
}

std::string camel(const std::string& type_simple) {
    if (type_simple.empty())
        return {};
    std::string s = type_simple;
    s[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[0])));
    return s;
}

std::string unique_name(const std::string& base, std::unordered_map<std::string, int>& used) {
    std::string name = base.empty() ? "renamed" : base;
    int& n = used[name];
    if (n == 0) {
        ++n;
        return name;
    }
    ++n;
    return name + "_" + std::to_string(n - 1);
}

void note_orig(std::string& comment, const std::string& orig, const char* how) {
    std::string tag = std::string("orig=") + orig + "; via=" + how;
    if (comment.empty())
        comment = tag;
    else if (comment.find("orig=") == std::string::npos)
        comment = tag + "; " + comment;
}

bool type_is(const std::string& field_type, const char* name) {
    if (field_type == name)
        return true;
    const std::string suffix = std::string(".") + name;
    if (field_type.size() >= suffix.size() &&
        field_type.compare(field_type.size() - suffix.size(), suffix.size(), suffix) == 0)
        return true;
    if (field_type.find(std::string("<") + name + ">") != std::string::npos)
        return true;
    if (field_type.find(std::string("<") + name + ",") != std::string::npos)
        return true;
    return false;
}

bool type_contains(const std::string& field_type, const char* needle) {
    return field_type.find(needle) != std::string::npos;
}

// ── Pass: unique clear types in a class ───────────────────────────────────
size_t pass_unique_types(SdkType& t) {
    // Count clear type occurrences among hashed fields
    std::unordered_map<std::string, int> counts;
    std::unordered_map<std::string, std::string> type_key; // field idx later
    for (auto& f : t.fields) {
        if (!is_hashed_name(f.name))
            continue;
        auto st = simple_type(f.type_name);
        if (st.empty())
            continue;
        counts[st]++;
    }
    size_t n = 0;
    std::unordered_map<std::string, int> used;
    // Pre-seed used with already-clear names
    for (auto& f : t.fields)
        if (!is_hashed_name(f.name))
            used[f.name] = 1;

    for (auto& f : t.fields) {
        if (!is_hashed_name(f.name))
            continue;
        auto st = simple_type(f.type_name);
        if (st.empty() || counts[st] != 1)
            continue;
        std::string base = camel(st);
        if (f.is_static)
            base = "s_" + base;
        const std::string orig = f.name;
        f.name = unique_name(base, used);
        note_orig(f.comment, orig, "unique_type");
        ++n;
    }
    return n;
}

// ── Pass: structural rename remaining fields ──────────────────────────────
size_t pass_structural_fields(SdkType& t) {
    std::unordered_map<std::string, int> used;
    for (auto& f : t.fields)
        if (!is_hashed_name(f.name))
            used[f.name] = 1;

    size_t n = 0;
    for (auto& f : t.fields) {
        if (!is_hashed_name(f.name))
            continue;
        std::string base = camel(simple_type(f.type_name));
        char off[32]{};
        sprintf_s(off, "%x", static_cast<unsigned>(f.offset));
        if (base.empty()) {
            if (f.offset)
                base = std::string("field_0x") + off;
            else
                base = "field_" + f.name.substr(1, 8);
        } else if (used[base] > 0) {
            // type already used — disambiguate with offset
            base = base + "_0x" + off;
        }
        if (f.is_static && base.find("s_") != 0)
            base = "s_" + base;
        const std::string orig = f.name;
        f.name = unique_name(base, used);
        note_orig(f.comment, orig, "structural");
        ++n;
    }
    return n;
}

// ── Pass: getter heuristic for methods ────────────────────────────────────
size_t pass_getters(SdkType& t) {
    std::unordered_map<std::string, int> used;
    for (auto& m : t.methods)
        if (!is_hashed_name(m.name))
            used[m.name] = 1;

    // Group 0-arg instance methods by simple return type
    struct Cand {
        size_t idx;
        uint64_t rva;
    };
    std::unordered_map<std::string, std::vector<Cand>> by_ret;
    for (size_t i = 0; i < t.methods.size(); ++i) {
        auto& m = t.methods[i];
        if (!is_hashed_name(m.name) || m.is_static || !m.params.empty())
            continue;
        auto st = simple_type(m.return_type);
        if (st.empty() || st == "Void" || st == "void")
            continue;
        by_ret[st].push_back({i, m.rva});
    }

    size_t n = 0;
    for (auto& [ret, cands] : by_ret) {
        std::sort(cands.begin(), cands.end(),
                  [](const Cand& a, const Cand& b) { return a.rva < b.rva; });
        // First → get_X / X property style; rest get_X_rva
        for (size_t k = 0; k < cands.size(); ++k) {
            auto& m = t.methods[cands[k].idx];
            std::string base;
            if (k == 0)
                base = "get_" + ret;
            else {
                char rv[32]{};
                sprintf_s(rv, "%llx", static_cast<unsigned long long>(m.rva));
                base = "get_" + ret + "_" + rv;
            }
            const std::string orig = m.name;
            m.name = unique_name(base, used);
            note_orig(m.comment, orig, "getter");
            ++n;
        }
    }
    return n;
}

// ── Pass: structural methods remaining ────────────────────────────────────
size_t pass_structural_methods(SdkType& t) {
    std::unordered_map<std::string, int> used;
    for (auto& m : t.methods)
        if (!is_hashed_name(m.name))
            used[m.name] = 1;

    size_t n = 0;
    for (auto& m : t.methods) {
        if (!is_hashed_name(m.name))
            continue;
        auto ret = simple_type(m.return_type);
        std::string base;
        const bool is_void = ret.empty() || ret == "Void" || ret == "void";
        if (is_void && m.params.empty())
            base = "Do";
        else if (is_void)
            base = "Set";
        else
            base = "Get" + ret;

        char rv[32]{};
        if (m.rva)
            sprintf_s(rv, "%llx", static_cast<unsigned long long>(m.rva));
        else
            sprintf_s(rv, "%.8s", m.name.c_str() + (m.name[0] == '%' ? 1 : 0));
        base = base + "_" + rv;
        if (m.is_static)
            base = "Static_" + base;

        const std::string orig = m.name;
        m.name = unique_name(sanitize_ident(base), used);
        note_orig(m.comment, orig, "structural");
        ++n;
    }
    return n;
}

// ── Pass: semantic Rust rules (type-driven, high confidence) ───────────────
struct SemHit {
    std::string field_name;
    std::string via;
};

bool rename_field_by_type(SdkType& t, const char* type_hint, const char* new_name,
                          DeobfStats& st, bool require_unique = true) {
    std::vector<SdkField*> hits;
    for (auto& f : t.fields) {
        if (f.is_static)
            continue;
        if (type_is(f.type_name, type_hint) || type_contains(f.type_name, type_hint))
            hits.push_back(&f);
    }
    if (hits.empty())
        return false;
    if (require_unique && hits.size() != 1) {
        // pick first by offset
        std::sort(hits.begin(), hits.end(),
                  [](SdkField* a, SdkField* b) { return a->offset < b->offset; });
    }
    SdkField* f = hits.front();
    // Only rename if still hashed or already structural-but we prefer semantic
    if (!is_hashed_name(f->name) && f->comment.find("via=semantic") != std::string::npos)
        return false;
    // If already clear semantic name matching, skip
    if (f->name == new_name)
        return false;

    // Avoid stealing a name already used by a different clear field
    for (auto& o : t.fields) {
        if (&o != f && o.name == new_name)
            return false;
    }

    const std::string orig = f->name;
    f->name = new_name;
    note_orig(f->comment, orig, "semantic");
    ++st.semantic_hits;
    ++st.renamed_fields;
    return true;
}

void rename_method_getter(SdkType& t, const char* ret_hint, const char* new_name, DeobfStats& st) {
    SdkMethod* best = nullptr;
    for (auto& m : t.methods) {
        if (m.is_static || !m.params.empty() || !m.rva)
            continue;
        if (!type_is(m.return_type, ret_hint) && !type_contains(m.return_type, ret_hint))
            continue;
        if (!best || m.rva < best->rva)
            best = &m;
    }
    if (!best)
        return;
    for (auto& o : t.methods)
        if (&o != best && o.name == new_name)
            return;
    const std::string orig = best->name;
    best->name = new_name;
    note_orig(best->comment, orig, "semantic_getter");
    ++st.semantic_hits;
    ++st.renamed_methods;
}

void pass_semantic_rust(SdkModel& model, DeobfStats& st) {
    for (auto& t : model.types) {
        const std::string& n = t.name;
        if (n == "BasePlayer") {
            rename_field_by_type(t, "PlayerInput", "playerInput", st);
            rename_field_by_type(t, "PlayerEyes", "playerEyes", st);
            rename_field_by_type(t, "PlayerInventory", "playerInventory", st);
            rename_field_by_type(t, "PlayerModel", "playerModel", st);
            rename_field_by_type(t, "PlayerModifiers", "playerModifiers", st);
            rename_field_by_type(t, "PlayerMetabolism", "metabolism", st);
            rename_field_by_type(t, "PlayerBlueprints", "blueprints", st);
            rename_field_by_type(t, "PlayerVoiceRecorder", "voiceRecorder", st);
            rename_field_by_type(t, "PlayerVoiceSpeaker", "voiceSpeaker", st);
            rename_field_by_type(t, "BaseMovement", "movement", st);
            rename_field_by_type(t, "PlayerWalkMovement", "movement", st);
            rename_field_by_type(t, "ViewModel", "gestureViewModel", st);
            rename_field_by_type(t, "BaseCollision", "playerCollision", st);
            rename_field_by_type(t, "PhoneController", "phoneController", st);
            rename_field_by_type(t, "HeldEntity", "heldEntity", st);
            rename_field_by_type(t, "ProtectionProperties", "baseProtection", st, false);

            // playerFlags by type name
            for (auto& f : t.fields) {
                if (type_contains(f.type_name, "PlayerFlags") ||
                    (f.name == "playerFlags")) {
                    if (is_hashed_name(f.name)) {
                        const std::string orig = f.name;
                        f.name = "playerFlags";
                        note_orig(f.comment, orig, "semantic");
                        ++st.semantic_hits;
                    }
                    break;
                }
            }
            // currentTeam already clear often
            rename_method_getter(t, "PlayerInventory", "get_playerInventory", st);
            rename_method_getter(t, "PlayerEyes", "get_playerEyes", st);
            rename_method_getter(t, "PlayerModel", "get_playerModel", st);
            rename_method_getter(t, "PlayerInput", "get_playerInput", st);
            rename_method_getter(t, "PlayerMetabolism", "get_metabolism", st);
        } else if (n == "BaseCombatEntity") {
            // health pair: consecutive floats
            std::vector<SdkField*> floats;
            for (auto& f : t.fields)
                if (!f.is_static &&
                    (type_is(f.type_name, "Single") || type_contains(f.type_name, "System.Single")))
                    floats.push_back(&f);
            std::sort(floats.begin(), floats.end(),
                      [](SdkField* a, SdkField* b) { return a->offset < b->offset; });
            for (size_t i = 0; i + 1 < floats.size(); ++i) {
                if (floats[i + 1]->offset == floats[i]->offset + 4) {
                    auto rename = [&](SdkField* f, const char* nm) {
                        if (!is_hashed_name(f->name) && f->name == nm)
                            return;
                        for (auto& o : t.fields)
                            if (&o != f && o.name == nm)
                                return;
                        const std::string orig = f->name;
                        f->name = nm;
                        note_orig(f->comment, orig, "semantic");
                        ++st.semantic_hits;
                    };
                    rename(floats[i], "health");
                    rename(floats[i + 1], "maxHealth");
                    break;
                }
            }
            rename_field_by_type(t, "ProtectionProperties", "baseProtection", st);
            rename_field_by_type(t, "SkeletonProperties", "skeletonProperties", st);
        } else if (n == "BaseEntity") {
            rename_field_by_type(t, "Model", "model", st);
            rename_field_by_type(t, "BuildingPrivlidge", "buildingPrivilege", st);
        } else if (n == "BaseNetworkable") {
            // prefabID often already clear
            for (auto& f : t.fields) {
                if (f.name == "prefabID" ||
                    (type_contains(f.type_name, "UInt32") && f.offset >= 0x50 && f.offset <= 0x60 &&
                     is_hashed_name(f.name))) {
                    // only rename hashed UInt32 near known prefabID slot if unique
                }
            }
        } else if (n == "PlayerEyes") {
            std::vector<SdkField*> vecs;
            for (auto& f : t.fields)
                if (!f.is_static && type_contains(f.type_name, "Vector3"))
                    vecs.push_back(&f);
            std::sort(vecs.begin(), vecs.end(),
                      [](SdkField* a, SdkField* b) { return a->offset < b->offset; });
            if (!vecs.empty() && is_hashed_name(vecs[0]->name)) {
                const std::string orig = vecs[0]->name;
                vecs[0]->name = "viewOffset";
                note_orig(vecs[0]->comment, orig, "semantic");
                ++st.semantic_hits;
            }
            int qn = 0;
            for (auto& f : t.fields) {
                if (f.is_static || !type_contains(f.type_name, "Quaternion"))
                    continue;
                if (!is_hashed_name(f.name)) {
                    ++qn;
                    continue;
                }
                const char* nm = (qn == 0) ? "bodyRotation" : "eyeRotation";
                if (qn >= 2)
                    break;
                // check free
                bool taken = false;
                for (auto& o : t.fields)
                    if (o.name == nm) {
                        taken = true;
                        break;
                    }
                if (!taken) {
                    const std::string orig = f.name;
                    f.name = nm;
                    note_orig(f.comment, orig, "semantic");
                    ++st.semantic_hits;
                }
                ++qn;
            }
        } else if (n == "PlayerInventory") {
            rename_field_by_type(t, "ItemCrafter", "crafting", st);
            rename_field_by_type(t, "PlayerLoot", "loot", st);
            // containers
            std::vector<SdkField*> containers;
            for (auto& f : t.fields)
                if (!f.is_static && type_contains(f.type_name, "ItemContainer"))
                    containers.push_back(&f);
            std::sort(containers.begin(), containers.end(),
                      [](SdkField* a, SdkField* b) { return a->offset < b->offset; });
            static const char* cnames[] = {"containerMain", "containerBelt", "containerWear"};
            for (size_t i = 0; i < containers.size() && i < 3; ++i) {
                if (!is_hashed_name(containers[i]->name))
                    continue;
                bool taken = false;
                for (auto& o : t.fields)
                    if (o.name == cnames[i]) {
                        taken = true;
                        break;
                    }
                if (taken)
                    continue;
                const std::string orig = containers[i]->name;
                containers[i]->name = cnames[i];
                note_orig(containers[i]->comment, orig, "semantic");
                ++st.semantic_hits;
            }
        } else if (n == "BaseProjectile") {
            // many clear names already (damageScale, automatic, …)
            rename_field_by_type(t, "RecoilProperties", "recoil", st);
            rename_field_by_type(t, "Magazine", "primaryMagazine", st, false);
        } else if (n == "MainCamera" || n == "CameraMan") {
            rename_field_by_type(t, "Camera", "camera", st, false);
            rename_field_by_type(t, "Transform", "transform", st, false);
        } else if (n == "PlayerModel") {
            rename_field_by_type(t, "SkinnedMultiMesh", "multiMesh", st);
            rename_field_by_type(t, "Animator", "animator", st, false);
        }
    }
}

// ── Pass: string xref recovery (LEA rip-relative → C# name strings) ────────
bool looks_like_csharp_name(const char* s, size_t n) {
    if (n < 2 || n > 128)
        return false;
    if (!(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_'))
        return false;
    size_t alnum = 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (std::isalnum(c) || c == '_' || c == '`' || c == '.')
            ++alnum;
        else
            return false;
    }
    // skip pure noise / short
    if (alnum < 3)
        return false;
    // skip common non-name strings
    static const char* kSkip[] = {
        "System", "UnityEngine", "mscorlib", "Assembly", "Object", "String",
        "Boolean", "Int32", "Single", "Void", "get_", "set_", "add_", "remove_",
        nullptr};
    std::string full(s, n);
    for (int i = 0; kSkip[i]; ++i)
        if (full == kSkip[i])
            return false;
    return true;
}

// SEH-safe read of up to `len` bytes
size_t safe_read(const uint8_t* src, uint8_t* dst, size_t len) {
    size_t got = 0;
    __try {
        for (size_t i = 0; i < len; ++i) {
            dst[i] = src[i];
            ++got;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return got;
    }
    return got;
}

// Collect LEA [rip+disp32] targets from a code blob; return absolute VAs.
void collect_lea_targets(const uint8_t* code, size_t code_len, uint64_t code_va,
                         std::vector<uint64_t>& out) {
    for (size_t i = 0; i + 7 <= code_len; ++i) {
        // REX.W LEA r64, [rip+disp32] : 48 8D xx disp32  or 4C 8D
        const uint8_t b0 = code[i];
        if ((b0 != 0x48 && b0 != 0x4C) || code[i + 1] != 0x8D)
            continue;
        const uint8_t modrm = code[i + 2];
        // mod=00, r/m=101 → rip-relative
        if ((modrm & 0xC7) != 0x05)
            continue;
        int32_t disp = 0;
        std::memcpy(&disp, code + i + 3, 4);
        const uint64_t next = code_va + i + 7;
        out.push_back(next + static_cast<int64_t>(disp));
        i += 6;
    }
}

bool read_c_string_at(uint64_t va, uint64_t mod_base, size_t mod_size, std::string& out) {
    if (va < mod_base || va >= mod_base + mod_size)
        return false;
    char buf[160]{};
    const size_t max_read = 128;
    const size_t got =
        safe_read(reinterpret_cast<const uint8_t*>(va), reinterpret_cast<uint8_t*>(buf), max_read);
    if (got < 3)
        return false;
    size_t n = 0;
    while (n < got && buf[n])
        ++n;
    if (n < 2 || n >= got) // must be null-terminated within buffer
        return false;
    if (!looks_like_csharp_name(buf, n))
        return false;
    out.assign(buf, n);
    return true;
}

size_t pass_string_xref(SdkModel& model, uint64_t mod_base, size_t mod_size,
                        const DeobfOptions& opt) {
    if (!mod_base || !mod_size)
        return 0;

    size_t hits = 0;
    size_t scanned = 0;
    const size_t scan_n = opt.method_scan_bytes ? opt.method_scan_bytes : 0x180;

    for (auto& t : model.types) {
        std::unordered_map<std::string, int> used;
        for (auto& m : t.methods)
            if (!is_hashed_name(m.name))
                used[m.name] = 1;

        for (auto& m : t.methods) {
            if (!is_hashed_name(m.name) || !m.rva)
                continue;
            if (opt.max_string_xref_methods && scanned >= opt.max_string_xref_methods)
                return hits;
            ++scanned;
            if ((scanned % 50000) == 0)
                ILOG_I("Deobf string_xref progress: %zu methods scanned, %zu hits", scanned, hits);

            const uint64_t mva = mod_base + m.rva;
            if (mva < mod_base || mva + 16 >= mod_base + mod_size)
                continue;

            std::vector<uint8_t> code(scan_n);
            const size_t got =
                safe_read(reinterpret_cast<const uint8_t*>(mva), code.data(), scan_n);
            if (got < 16)
                continue;

            std::vector<uint64_t> targets;
            collect_lea_targets(code.data(), got, mva, targets);

            std::string best;
            for (uint64_t tgt : targets) {
                std::string s;
                if (!read_c_string_at(tgt, mod_base, mod_size, s))
                    continue;
                // Prefer names that look method-like and not type-only
                if (s == t.name)
                    continue;
                if (best.empty() || s.size() > best.size())
                    best = s;
            }
            if (best.empty())
                continue;

            // sanitize and assign
            std::string name = sanitize_ident(best);
            if (name.size() < 2)
                continue;
            // Don't use pure type names as method names if too generic
            if (name == "Object" || name == "Component" || name == "Behaviour")
                continue;

            const std::string orig = m.name;
            m.name = unique_name(name, used);
            note_orig(m.comment, orig, "string_xref");
            ++hits;
        }
    }
    return hits;
}

// Rename hashed types that appear only as nested junk — light touch
size_t pass_type_names(SdkModel& model) {
    size_t n = 0;
    for (auto& t : model.types) {
        if (!is_hashed_name(t.name))
            continue;
        const std::string short_h =
            (t.name[0] == '%') ? t.name.substr(1, 8) : t.name.substr(0, 8);
        const std::string orig = t.name;
        t.name = "Type_" + short_h;
        if (t.full_name.find('%') != std::string::npos || t.full_name == orig)
            t.full_name = (t.ns.empty() ? t.name : t.ns + "." + t.name);
        note_orig(t.comment, orig, "type_hash");
        ++n;
    }
    return n;
}

} // namespace

bool is_hashed_name(const std::string& name) {
    return is_facepunch_hash(name);
}

DeobfStats deobfuscate_sdk(SdkModel& model, uint64_t module_base, size_t module_size,
                           const DeobfOptions& opt) {
    DeobfStats st{};

    for (auto& t : model.types) {
        for (auto& f : t.fields)
            if (is_hashed_name(f.name))
                ++st.hashed_fields_seen;
        for (auto& m : t.methods)
            if (is_hashed_name(m.name))
                ++st.hashed_methods_seen;
    }

    ILOG_I("Deobf: %zu hashed fields, %zu hashed methods — recovering names…",
           st.hashed_fields_seen, st.hashed_methods_seen);

    // 1) Semantic first (highest quality) — uses types while names still hashed or partial
    //    Actually unique_type first then semantic can override structural later.
    //    Order: unique_type → semantic (overrides) → structural remaining → getters → methods → xref

    if (opt.structural) {
        for (auto& t : model.types)
            st.unique_type_hits += pass_unique_types(t);
        ILOG_I("Deobf pass unique_type: %zu fields", st.unique_type_hits);
        st.renamed_fields += st.unique_type_hits;
    }

    if (opt.semantic_rust) {
        const size_t before_f = st.semantic_hits;
        pass_semantic_rust(model, st);
        ILOG_I("Deobf pass semantic: %zu hits", st.semantic_hits - before_f);
    }

    if (opt.structural) {
        size_t n = 0;
        for (auto& t : model.types)
            n += pass_structural_fields(t);
        st.renamed_fields += n;
        ILOG_I("Deobf pass structural fields: %zu", n);
    }

    if (opt.getter_heuristic) {
        size_t n = 0;
        for (auto& t : model.types)
            n += pass_getters(t);
        st.getter_hits = n;
        st.renamed_methods += n;
        ILOG_I("Deobf pass getters: %zu", n);
    }

    // String xref before generic structural methods (better names)
    if (opt.string_xref && module_base) {
        ILOG_I("Deobf pass string_xref: scanning method prologues (this can take a while)…");
        st.string_xref_hits = pass_string_xref(model, module_base, module_size, opt);
        st.renamed_methods += st.string_xref_hits;
        ILOG_I("Deobf pass string_xref: %zu methods named from code strings", st.string_xref_hits);
    }

    if (opt.structural) {
        size_t n = 0;
        for (auto& t : model.types)
            n += pass_structural_methods(t);
        st.renamed_methods += n;
        ILOG_I("Deobf pass structural methods: %zu", n);
    }

    st.renamed_types = pass_type_names(model);

    model.metadata["deobf"] = "1";
    model.metadata["deobf_fields"] = std::to_string(st.renamed_fields);
    model.metadata["deobf_methods"] = std::to_string(st.renamed_methods);
    model.metadata["deobf_semantic"] = std::to_string(st.semantic_hits);
    model.metadata["deobf_string_xref"] = std::to_string(st.string_xref_hits);
    model.engine_detail += " + deobf";

    ILOG_I("Deobf done: fields=%zu methods=%zu types=%zu semantic=%zu xref=%zu",
           st.renamed_fields, st.renamed_methods, st.renamed_types, st.semantic_hits,
           st.string_xref_hits);
    return st;
}

bool write_name_map(const SdkModel& model, const std::string& path, const DeobfStats& stats) {
    std::ofstream os(path, std::ios::trunc);
    if (!os)
        return false;
    os << "{\n";
    os << "  \"tool\": \"Icky\",\n";
    os << "  \"schema\": \"icky.namemap/v1\",\n";
    os << "  \"stats\": {\n";
    os << "    \"hashed_fields\": " << stats.hashed_fields_seen << ",\n";
    os << "    \"hashed_methods\": " << stats.hashed_methods_seen << ",\n";
    os << "    \"renamed_fields\": " << stats.renamed_fields << ",\n";
    os << "    \"renamed_methods\": " << stats.renamed_methods << ",\n";
    os << "    \"semantic\": " << stats.semantic_hits << ",\n";
    os << "    \"string_xref\": " << stats.string_xref_hits << "\n";
    os << "  },\n";
    os << "  \"entries\": [\n";

    bool first = true;
    auto emit = [&](const std::string& cls, const char* kind, const std::string& name,
                    const std::string& comment, int32_t offset, uint64_t rva) {
        // parse orig= from comment
        std::string orig;
        auto p = comment.find("orig=");
        if (p == std::string::npos)
            return;
        p += 5;
        auto end = comment.find(';', p);
        orig = (end == std::string::npos) ? comment.substr(p) : comment.substr(p, end - p);
        if (orig.empty())
            return;
        std::string via;
        auto v = comment.find("via=");
        if (v != std::string::npos) {
            v += 4;
            auto ve = comment.find(';', v);
            via = (ve == std::string::npos) ? comment.substr(v) : comment.substr(v, ve - v);
        }
        if (!first)
            os << ",\n";
        first = false;
        os << "    {\"class\":\"" << cls << "\",\"kind\":\"" << kind << "\",\"from\":\"" << orig
           << "\",\"to\":\"" << name << "\",\"via\":\"" << via << "\"";
        if (offset)
            os << ",\"offset\":" << offset;
        if (rva)
            os << ",\"rva\":\"0x" << std::hex << rva << std::dec << "\"";
        os << "}";
    };

    for (const auto& t : model.types) {
        for (const auto& f : t.fields)
            emit(t.name, "field", f.name, f.comment, f.offset, 0);
        for (const auto& m : t.methods)
            emit(t.name, "method", m.name, m.comment, 0, m.rva);
    }
    os << "\n  ]\n}\n";
    return true;
}

bool write_semantic_offsets(const SdkModel& model, const std::string& path) {
    std::ofstream os(path, std::ios::trunc);
    if (!os)
        return false;
    os << "// Icky semantic offsets (type-driven + deobf)\n";
    os << "// High-confidence names: via=semantic or unique clear types\n";
    os << "#pragma once\n#include <cstdint>\n\nnamespace Icky::Rust {\n";

    auto dump_class = [&](const char* want) {
        const SdkType* t = nullptr;
        for (const auto& x : model.types)
            if (x.name == want) {
                t = &x;
                break;
            }
        if (!t)
            return;
        os << "  namespace " << want << " {\n";
        for (const auto& f : t->fields) {
            if (f.is_static)
                continue;
            // Prefer semantic / unique / clear
            const bool good =
                f.comment.find("via=semantic") != std::string::npos ||
                f.comment.find("via=unique_type") != std::string::npos ||
                !is_hashed_name(f.name);
            if (!good)
                continue;
            if (is_hashed_name(f.name))
                continue;
            os << "    constexpr std::uintptr_t " << f.name << " = 0x" << std::hex << f.offset
               << std::dec << "; // " << f.type_name << "\n";
        }
        // getters
        for (const auto& m : t->methods) {
            if (m.comment.find("via=semantic") == std::string::npos &&
                m.comment.find("via=semantic_getter") == std::string::npos &&
                m.comment.find("via=getter") == std::string::npos)
                continue;
            if (!m.rva || is_hashed_name(m.name))
                continue;
            os << "    constexpr std::uintptr_t " << m.name << "_rva = 0x" << std::hex << m.rva
               << std::dec << "; // " << m.return_type << "\n";
        }
        os << "  }\n";
    };

    static const char* kClasses[] = {
        "BasePlayer", "BaseCombatEntity", "BaseEntity", "BaseNetworkable",
        "PlayerEyes", "PlayerInventory", "PlayerModel", "PlayerInput",
        "BaseProjectile", "HeldEntity", "MainCamera", nullptr};
    for (int i = 0; kClasses[i]; ++i)
        dump_class(kClasses[i]);

    os << "} // namespace Icky::Rust\n";
    return true;
}

} // namespace icky::il2cpp
