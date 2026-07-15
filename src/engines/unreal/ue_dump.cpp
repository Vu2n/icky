#include "ue_internals.h"
#include "core/logger.h"
#include "core/memory.h"

#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <string>

namespace icky::ue {
namespace {

uint64_t get_object(const Globals& g, const Layout& L, int32_t index) {
    uint64_t arr = g.gobjects;
    if (L.gobjects_is_outer)
        arr = g.gobjects + L.objobjects_offset;

    uint64_t objects = 0;
    if (!Mem::read(arr + L.objects_ptr, objects) || !objects)
        return 0;

    const int32_t item = L.item_size;
    uint64_t item_addr = 0;
    if (L.chunked) {
        const int32_t pc = L.per_chunk;
        const int32_t ci = index / pc;
        const int32_t wi = index % pc;
        uint64_t chunk = 0;
        if (!Mem::read(objects + static_cast<uint64_t>(ci) * 8, chunk) || !chunk)
            return 0;
        item_addr = chunk + static_cast<uint64_t>(wi) * item;
    } else {
        item_addr = objects + static_cast<uint64_t>(index) * item;
    }
    return Mem::ptr(item_addr + L.item_object);
}

int32_t num_objects(const Globals& g, const Layout& L) {
    uint64_t arr = g.gobjects;
    if (L.gobjects_is_outer)
        arr = g.gobjects + L.objobjects_offset;
    int32_t n = 0;
    Mem::read(arr + L.num_elements, n);
    if (n < 0 || n > 20'000'000) return 0;
    return n;
}

struct FNameRaw { int32_t cmp = 0; int32_t num = 0; };

FNameRaw read_fname(uint64_t addr) {
    FNameRaw n;
    Mem::read(addr, n.cmp);
    Mem::read(addr + 4, n.num);
    return n;
}

// Probe UStruct child property offsets by finding a known UClass with properties
struct StructOffsets {
    int32_t super = 0x40;
    int32_t children = 0x48;
    int32_t child_props = 0x50;
    int32_t size = 0x58;
    int32_t ffield_next = 0x20;
    int32_t ffield_name = 0x28;
    int32_t ffield_class = 0x08;
    int32_t prop_offset = 0x4C;
    int32_t prop_size = 0x3C;
    bool uses_ffield = true;
};

bool is_ident(const std::string& s) {
    if (s.empty() || s.size() > 200) return false;
    if (!(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_'))
        return false;
    for (unsigned char c : s)
        if (!(std::isalnum(c) || c == '_' || c == '/')) return false;
    return true;
}

StructOffsets find_struct_offsets(const Globals& g, const Layout& L, NamePool& names,
                                  uint64_t class_uclass, uint64_t struct_uclass) {
    StructOffsets so;
    const int32_t supers[] = {0x40, 0x48, 0x38, 0x30, 0x50}; // prefer classic 0x40 first
    const int32_t childprops[] = {0x50, 0x48, 0x58, 0x60, 0x68, 0x70};
    const int32_t sizes[] = {0x58, 0x50, 0x5C, 0x60, 0x68};
    const int32_t fname_offs[] = {0x28, 0x20, 0x18, 0x30};
    const int32_t fnext_offs[] = {0x20, 0x18, 0x10, 0x28};
    const int32_t poffs[] = {0x4C, 0x44, 0x3C, 0x50, 0x54};

    // Find a non-trivial class as field probe (Actor etc.)
    uint64_t probe = class_uclass;
    std::string probe_name = "Class";
    int32_t n = num_objects(g, L);
    for (int32_t i = 0; i < n && i < 80000; ++i) {
        uint64_t obj = get_object(g, L, i);
        if (!obj) continue;
        if (Mem::ptr(obj + L.uobject_class) != class_uclass) continue;
        auto nm = names.get(read_fname(obj + L.uobject_name).cmp,
                            read_fname(obj + L.uobject_name).num);
        if (nm == "Actor" || nm == "Pawn" || nm == "Character" || nm == "PlayerController" ||
            nm == "GameModeBase" || nm == "ActorComponent" || nm == "Player") {
            probe = obj;
            probe_name = nm;
            ILOG_I("Struct offset probe class: %s @ 0x%llX", nm.c_str(), (unsigned long long)obj);
            break;
        }
    }

    auto is_ustruct_obj = [&](uint64_t o) -> bool {
        if (!Mem::valid_user_ptr(o)) return false;
        uint64_t c = Mem::ptr(o + L.uobject_class);
        return c == class_uclass || (struct_uclass && c == struct_uclass);
    };

    // --- Super offset: must point at a UClass/UScriptStruct with a sane name ---
    // For Actor, super is typically Object.
    int best_super_score = -1;
    int32_t best_super = 0x40;
    for (int32_t sup : supers) {
        uint64_t super = Mem::ptr(probe + sup);
        if (!is_ustruct_obj(super)) continue;
        auto sn = names.get(read_fname(super + L.uobject_name).cmp,
                            read_fname(super + L.uobject_name).num);
        int sc = 0;
        if (is_ident(sn)) sc += 10;
        if (probe_name == "Actor" && sn == "Object") sc += 50;
        if (probe_name == "Player" && (sn == "Object" || sn == "Actor")) sc += 30;
        if (sn == "Object" || sn == "Actor" || sn == "ActorComponent") sc += 20;
        if (sc > best_super_score) {
            best_super_score = sc;
            best_super = sup;
        }
    }
    so.super = best_super;
    ILOG_I("Super offset =+0x%X (score=%d, probe=%s super='%s')",
           so.super, best_super_score, probe_name.c_str(),
           names.get(read_fname(Mem::ptr(probe + so.super) + L.uobject_name).cmp,
                     read_fname(Mem::ptr(probe + so.super) + L.uobject_name).num)
               .c_str());

    // --- ChildProperties / Size / FField layout ---
    int best = -1;
    StructOffsets best_so = so;

    for (int32_t cp : childprops) {
        for (int32_t sz : sizes) {
            if (cp == sz || cp == so.super) continue;
            int32_t size_val = 0;
            Mem::read(probe + sz, size_val);
            if (size_val < 0x10 || size_val > 0x100000) continue;

            uint64_t field = Mem::ptr(probe + cp);
            if (!Mem::valid_user_ptr(field)) continue;

            for (int32_t fn : fname_offs) {
                for (int32_t nx : fnext_offs) {
                    if (fn == nx) continue;
                    for (int32_t po : poffs) {
                        int good = 0;
                        uint64_t f = field;
                        std::unordered_set<uint64_t> seen;
                        for (int step = 0; step < 48 && f && seen.insert(f).second; ++step) {
                            auto raw = read_fname(f + fn);
                            auto nm = names.get(raw.cmp, raw.num);
                            int32_t off = 0;
                            Mem::read(f + po, off);
                            if (is_ident(nm) && nm != "None" && off >= 0 && off < size_val + 0x1000)
                                ++good;
                            f = Mem::ptr(f + nx);
                            if (!Mem::valid_user_ptr(f)) break;
                        }
                        if (good > best) {
                            best = good;
                            best_so = so;
                            best_so.child_props = cp;
                            best_so.size = sz;
                            best_so.ffield_name = fn;
                            best_so.ffield_next = nx;
                            best_so.prop_offset = po;
                            best_so.uses_ffield = true;
                        }
                    }
                }
            }
        }
    }

    if (best >= 2) {
        so = best_so;
        ILOG_I("UStruct offsets: Super=+0x%X ChildProps=+0x%X Size=+0x%X FFieldName=+0x%X Next=+0x%X PropOff=+0x%X (fields_hit=%d)",
               so.super, so.child_props, so.size, so.ffield_name, so.ffield_next, so.prop_offset, best);
    } else {
        ILOG_W("UStruct offset probe weak (hit=%d) — using UE5 defaults", best);
        so.super = 0x40;
        so.child_props = 0x50;
        so.size = 0x58;
        so.ffield_name = 0x28;
        so.ffield_next = 0x20;
        so.prop_offset = 0x4C;
    }
    return so;
}

// "/Script/Engine.Actor" or "Class /Script/Engine.Actor" → "/Script/Engine"
std::string package_from_full_name(const std::string& full) {
    if (full.empty()) return {};
    std::string path = full;
    const auto sp = path.find(' ');
    if (sp != std::string::npos)
        path = path.substr(sp + 1);
    // strip leading slashes for grouping key consistency but keep Script/
    const auto dot = path.rfind('.');
    if (dot != std::string::npos && dot > 0)
        return path.substr(0, dot);
    return path;
}

bool is_package_path(const std::string& s) {
    if (s.empty() || s.size() > 260) return false;
    for (unsigned char c : s) {
        if (!(std::isalnum(c) || c == '_' || c == '/' || c == '.' || c == '-'))
            return false;
    }
    return s.find_first_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") != std::string::npos;
}

std::string package_name(uint64_t obj, const Layout& L, NamePool& names) {
    // Prefer Outer chain to UPackage
    uint64_t cur = obj;
    uint64_t last = obj;
    std::unordered_set<uint64_t> seen;
    while (cur && seen.insert(cur).second) {
        last = cur;
        cur = Mem::ptr(cur + L.uobject_outer);
        if (!Mem::valid_user_ptr(cur)) break;
    }
    auto raw = read_fname(last + L.uobject_name);
    auto n = names.get(raw.cmp, raw.num);
    if (is_package_path(n) && n != "None")
        return n;
    return {};
}

std::string full_name(uint64_t obj, const Layout& L, NamePool& names) {
    std::vector<std::string> parts;
    uint64_t cur = obj;
    std::unordered_set<uint64_t> seen;
    while (cur && seen.insert(cur).second && parts.size() < 32) {
        auto raw = read_fname(cur + L.uobject_name);
        parts.push_back(names.get(raw.cmp, raw.num));
        cur = Mem::ptr(cur + L.uobject_outer);
        if (!Mem::valid_user_ptr(cur)) break;
    }
    std::string path;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        if (!path.empty()) path += '.';
        path += *it;
    }
    uint64_t cls = Mem::ptr(obj + L.uobject_class);
    std::string cn;
    if (cls) {
        auto raw = read_fname(cls + L.uobject_name);
        cn = names.get(raw.cmp, raw.num);
    }
    if (cn.empty()) return path;
    return cn + " " + path;
}

void read_fields(uint64_t ustruct, const StructOffsets& so, NamePool& names,
                 SdkType& type_out) {
    uint64_t field = Mem::ptr(ustruct + so.child_props);
    std::unordered_set<uint64_t> seen;
    while (field && Mem::valid_user_ptr(field) && seen.insert(field).second &&
           type_out.fields.size() < 1024) {
        SdkField f;
        auto raw = read_fname(field + so.ffield_name);
        f.name = names.get(raw.cmp, raw.num);
        Mem::read(field + so.prop_offset, f.offset);
        Mem::read(field + so.prop_size, f.size);
        uint64_t fc = Mem::ptr(field + so.ffield_class);
        if (fc) {
            auto fr = read_fname(fc);
            f.type_name = names.get(fr.cmp, fr.num);
            if (f.type_name.empty() || !is_ident(f.type_name)) {
                fr = read_fname(fc + 8);
                f.type_name = names.get(fr.cmp, fr.num);
            }
        }
        if (f.type_name.empty() || !is_ident(f.type_name)) f.type_name = "FProperty";
        if (f.name.empty() || f.name == "None" || !is_ident(f.name))
            f.name = "Field_0x" + std::to_string(static_cast<unsigned>(f.offset));
        if (f.offset >= 0 && f.offset < 0x100000)
            type_out.fields.push_back(std::move(f));
        field = Mem::ptr(field + so.ffield_next);
    }
}

void read_functions(uint64_t uclass, const Layout& L, const StructOffsets& so,
                    NamePool& names, SdkType& type_out) {
    // UField Children linked list (UFunction still lives here on modern UE)
    uint64_t child = Mem::ptr(uclass + so.children);
    // children offset may equal default 0x48 — also try L.ustruct_children
    if (!Mem::valid_user_ptr(child))
        child = Mem::ptr(uclass + L.ustruct_children);

    std::unordered_set<uint64_t> seen;
    // UField::Next typically 0x28
    const int32_t next_off = 0x28;
    while (child && Mem::valid_user_ptr(child) && seen.insert(child).second &&
           type_out.methods.size() < 2048) {
        uint64_t ccls = Mem::ptr(child + L.uobject_class);
        std::string cn;
        if (ccls) {
            auto raw = read_fname(ccls + L.uobject_name);
            cn = names.get(raw.cmp, raw.num);
        }
        if (cn == "Function" || cn == "DelegateFunction" || cn == "SparseDelegateFunction") {
            SdkMethod m;
            auto raw = read_fname(child + L.uobject_name);
            m.name = names.get(raw.cmp, raw.num);
            Mem::read(child + L.ufunction_flags, m.flags);
            uint64_t func = 0;
            Mem::read(child + L.ufunction_native, func);
            m.address = func;
            if (!m.name.empty())
                type_out.methods.push_back(std::move(m));
        }
        child = Mem::ptr(child + next_off);
    }
}

} // namespace

bool dump_sdk(const Globals& g, const Layout& layout, SdkModel& out) {
    out.engine = ICKY_ENGINE_UNREAL;
    out.engine_detail = layout.describe() + " | " + g.engine_guess;
    out.primary_module = {g.module_name, g.module_base, g.module_size};
    out.game_name = g.module_name;

    auto add_global = [&](const char* name, uint64_t addr, const char* ty) {
        if (!addr) return;
        out.globals.push_back({name, addr,
                               g.module_base ? addr - g.module_base : 0, ty, ""});
    };
    add_global("GObjects", g.gobjects, "FUObjectArray*");
    add_global("GNames", g.gnames, "FNamePool*");
    add_global("GWorld", g.gworld, "UWorld**");
    add_global("ProcessEvent", g.process_event, "function");
    add_global("FName_AppendString", g.append_string, "function");

    // Layout constants as globals for external users
    out.globals.push_back({"OFF_UObject_Class", static_cast<uint64_t>(layout.uobject_class),
                           static_cast<uint64_t>(layout.uobject_class), "offset", ""});
    out.globals.push_back({"OFF_UObject_Name", static_cast<uint64_t>(layout.uobject_name),
                           static_cast<uint64_t>(layout.uobject_name), "offset", ""});
    out.globals.push_back({"OFF_UObject_Outer", static_cast<uint64_t>(layout.uobject_outer),
                           static_cast<uint64_t>(layout.uobject_outer), "offset", ""});
    out.globals.push_back({"OFF_ObjObjects", static_cast<uint64_t>(layout.objobjects_offset),
                           static_cast<uint64_t>(layout.objobjects_offset), "offset", ""});
    out.globals.push_back({"OFF_NumElements", static_cast<uint64_t>(layout.num_elements),
                           static_cast<uint64_t>(layout.num_elements), "offset", ""});

    NamePool names;
    names.init(g.gnames, layout, g.append_string);
    {
        const auto n0 = names.get(0, 0);
        ILOG_I("FName[0]='%s' sane=%d gameFn=%d",
               n0.c_str(), names.looks_sane() ? 1 : 0, names.using_game_fn() ? 1 : 0);
        if (!names.looks_sane()) {
            ILOG_W("FName still not resolving to None — will use Class self-ref + best-effort names");
        }
    }

    const int32_t n = num_objects(g, layout);
    ILOG_I("Walking %d UObjects...", n);
    if (n <= 0) {
        ILOG_E("NumElements=0 — layout still wrong");
        return !out.globals.empty();
    }

    // Locate metaclasses
    // 1) Pointer identity: UClass Class has ClassPrivate == itself
    // 2) Name-based once FName works
    uint64_t meta_class = 0, meta_struct = 0, meta_enum = 0, meta_bp = 0, meta_func = 0;

    for (int32_t i = 0; i < n && i < 200000; ++i) {
        uint64_t obj = get_object(g, layout, i);
        if (!Mem::valid_user_ptr(obj)) continue;
        uint64_t cls = Mem::ptr(obj + layout.uobject_class);
        if (cls == obj) {
            meta_class = obj;
            ILOG_I("Class metaclass via self-ref @ 0x%llX (index %d)",
                   (unsigned long long)obj, i);
            break;
        }
    }

    // Name-based meta discovery (works once ToString is good)
    for (int32_t i = 0; i < n && i < 200000; ++i) {
        uint64_t obj = get_object(g, layout, i);
        if (!Mem::valid_user_ptr(obj)) continue;
        uint64_t cls = Mem::ptr(obj + layout.uobject_class);
        if (!Mem::valid_user_ptr(cls)) continue;

        // Only UClass instances once we know Class meta
        if (meta_class && cls != meta_class) continue;

        auto on = names.get(read_fname(obj + layout.uobject_name).cmp,
                            read_fname(obj + layout.uobject_name).num);
        if (on == "Class" && !meta_class) meta_class = obj;
        if (on == "ScriptStruct") meta_struct = obj;
        if (on == "Enum") meta_enum = obj;
        if (on == "Function") meta_func = obj;
        if (on == "BlueprintGeneratedClass") meta_bp = obj;
    }

    // If we have Class meta but not ScriptStruct: find by name among UClasses
    if (meta_class && !meta_struct) {
        for (int32_t i = 0; i < n && i < 200000; ++i) {
            uint64_t obj = get_object(g, layout, i);
            if (!obj) continue;
            if (Mem::ptr(obj + layout.uobject_class) != meta_class) continue;
            auto on = names.get(read_fname(obj + layout.uobject_name).cmp,
                                read_fname(obj + layout.uobject_name).num);
            if (on == "ScriptStruct") meta_struct = obj;
            else if (on == "Enum") meta_enum = obj;
            else if (on == "Function") meta_func = obj;
            else if (on == "BlueprintGeneratedClass") meta_bp = obj;
        }
    }

    ILOG_I("Meta: Class=0x%llX ScriptStruct=0x%llX Enum=0x%llX BPGC=0x%llX Func=0x%llX",
           (unsigned long long)meta_class, (unsigned long long)meta_struct,
           (unsigned long long)meta_enum, (unsigned long long)meta_bp,
           (unsigned long long)meta_func);

    // If Class meta found but names still broken — dump ALL UClasses by pointer
    // (ScriptStruct/Enum instances use different Class pointers)

    StructOffsets so = find_struct_offsets(g, layout, names,
                                           meta_class ? meta_class : 0,
                                           meta_struct ? meta_struct : 0);
    Layout L = layout;
    L.ustruct_super = so.super;
    L.ustruct_children = so.children;
    L.ustruct_child_properties = so.child_props;
    L.ustruct_size = so.size;
    L.ffield_name = so.ffield_name;
    L.ffield_next = so.ffield_next;
    L.prop_offset = so.prop_offset;
    L.prop_elem_size = so.prop_size;

    int classes = 0, structs = 0, enums = 0, funcs = 0;
    std::unordered_map<std::string, int> pkg_counts;

    // Helper: is object a UClass / ScriptStruct / Enum?
    auto classify = [&](uint64_t obj) -> const char* {
        uint64_t cls = Mem::ptr(obj + L.uobject_class);
        if (!cls) return nullptr;

        // Pointer identity (works without decrypted names)
        if (meta_struct && cls == meta_struct) return "ScriptStruct";
        if (meta_enum && cls == meta_enum) return "Enum";
        if (meta_func && cls == meta_func) return "Function";
        if (meta_bp && cls == meta_bp) return "Class"; // BPGC treated as class
        if (meta_class && cls == meta_class) return "Class";

        // Name of the *class* of this object
        auto cn = names.get(read_fname(cls + L.uobject_name).cmp,
                            read_fname(cls + L.uobject_name).num);
        if (cn == "Class" || cn == "BlueprintGeneratedClass" ||
            cn == "WidgetBlueprintGeneratedClass" || cn == "AnimBlueprintGeneratedClass")
            return "Class";
        if (cn == "ScriptStruct") return "ScriptStruct";
        if (cn == "Enum" || cn == "UserDefinedEnum") return "Enum";
        if (cn == "Function") return "Function";
        return nullptr;
    };

    const int32_t limit = n;
    for (int32_t i = 0; i < limit; ++i) {
        uint64_t obj = get_object(g, L, i);
        if (!Mem::valid_user_ptr(obj)) continue;

        const char* kind = classify(obj);
        if (!kind) continue;
        if (std::string(kind) == "Function") {
            ++funcs;
            continue; // collected via class children
        }

        auto raw = read_fname(obj + L.uobject_name);
        std::string oname = names.get(raw.cmp, raw.num);
        if (oname == "None") continue;
        if (oname.empty() || !is_ident(oname))
            oname = std::string(kind) + "_" + std::to_string(i);

        SdkType t;
        t.address = obj;
        t.rva = g.module_base && obj >= g.module_base && obj < g.module_base + g.module_size
                    ? obj - g.module_base
                    : 0;
        t.name = oname;
        t.full_name = full_name(obj, L, names);
        t.ns = package_name(obj, L, names);
        if (!is_package_path(t.ns))
            t.ns = package_from_full_name(t.full_name);
        if (!is_package_path(t.ns))
            t.ns = "Global";

        auto read_parent = [&](uint64_t ustruct) -> std::string {
            uint64_t super = Mem::ptr(ustruct + so.super);
            if (!Mem::valid_user_ptr(super)) return {};
            // Super must itself be a class or scriptstruct
            uint64_t sc = Mem::ptr(super + L.uobject_class);
            if (meta_class && sc != meta_class && !(meta_struct && sc == meta_struct))
                return {};
            auto sr = read_fname(super + L.uobject_name);
            auto pn = names.get(sr.cmp, sr.num);
            if (!is_ident(pn) || pn == "None") return {};
            return pn;
        };

        if (std::string(kind) == "Enum") {
            t.kind = TypeKind::Enum;
            ++enums;
            for (int32_t eoff : {0x40, 0x48, 0x50, 0x30}) {
                uint64_t data = Mem::ptr(obj + eoff);
                int32_t count = 0;
                Mem::read(obj + eoff + 8, count);
                if (!data || count <= 0 || count > 4096) continue;
                bool ok = true;
                for (int32_t ei = 0; ei < count; ++ei) {
                    auto er = read_fname(data + static_cast<uint64_t>(ei) * 0x10);
                    int64_t val = 0;
                    Mem::read(data + static_cast<uint64_t>(ei) * 0x10 + 8, val);
                    auto en = names.get(er.cmp, er.num);
                    if (!is_ident(en)) { ok = false; break; }
                    t.enum_members.push_back({en, val});
                }
                if (ok && !t.enum_members.empty()) break;
                t.enum_members.clear();
            }
        } else if (std::string(kind) == "ScriptStruct") {
            t.kind = TypeKind::Struct;
            ++structs;
            Mem::read(obj + so.size, t.size);
            t.parent = read_parent(obj);
            read_fields(obj, so, names, t);
        } else {
            t.kind = TypeKind::Class;
            ++classes;
            Mem::read(obj + so.size, t.size);
            t.parent = read_parent(obj);
            read_fields(obj, so, names, t);
            read_functions(obj, L, so, names, t);
            for (auto& m : t.methods) {
                if (m.address && g.module_base && m.address >= g.module_base &&
                    m.address < g.module_base + g.module_size)
                    m.rva = m.address - g.module_base;
            }
        }

        pkg_counts[t.ns]++;
        out.types.push_back(std::move(t));
    }

    ILOG_I("UE dump done: %d classes, %d structs, %d enums, ~%d functions seen, %zu types total, %zu packages",
           classes, structs, enums, funcs, out.types.size(), pkg_counts.size());

    out.metadata["num_objects"] = std::to_string(n);
    out.metadata["layout"] = layout.describe();
    out.metadata["fname0"] = names.get(0, 0);

    // Even if types empty, return true if we at least got globals + layout
    if (out.types.empty()) {
        ILOG_W("No types emitted — names may still be encrypted. Dumping raw object sample...");
        // Emit diagnostic type list with raw FName indices
        SdkType diag;
        diag.kind = TypeKind::Namespace;
        diag.name = "Diagnostics";
        diag.ns = "Icky";
        diag.comment = "FName0=" + names.get(0, 0) + " NumObjects=" + std::to_string(n);
        for (int32_t i = 0; i < std::min(n, 50); ++i) {
            uint64_t obj = get_object(g, L, i);
            if (!obj) continue;
            auto raw = read_fname(obj + L.uobject_name);
            SdkField f;
            f.name = "obj_" + std::to_string(i);
            f.offset = static_cast<int32_t>(raw.cmp);
            f.comment = names.get(raw.cmp, raw.num);
            f.type_name = "FNameIndex";
            diag.fields.push_back(std::move(f));
        }
        out.types.push_back(std::move(diag));
    }

    return true;
}

} // namespace icky::ue
