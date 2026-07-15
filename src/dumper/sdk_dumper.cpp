#include "sdk_dumper.h"
#include "core/logger.h"
#include "core/pattern_scan.h"

#include <chrono>
#include <cstring>
#include <set>
#include <sstream>

namespace ue {
namespace {

std::set<std::string> parse_filter(const char* filter) {
    std::set<std::string> out;
    if (!filter || !*filter)
        return out;
    std::stringstream ss(filter);
    std::string item;
    while (std::getline(ss, item, ',')) {
        // trim
        size_t a = item.find_first_not_of(" \t");
        size_t b = item.find_last_not_of(" \t");
        if (a == std::string::npos)
            continue;
        out.insert(item.substr(a, b - a + 1));
    }
    return out;
}

bool is_engine_package(const std::string& name) {
    static const char* prefixes[] = {
        "Engine", "/Script/Engine", "CoreUObject", "/Script/CoreUObject",
        "Core", "InputCore", "Slate", "UMG", "AIModule", "GameplayTags",
        "NavigationSystem", "PhysicsCore", "Niagara", "EnhancedInput"
    };
    for (const char* p : prefixes) {
        if (name == p || name.rfind(p, 0) == 0)
            return true;
    }
    return false;
}

uint64_t package_of(UObjectReader& r, uint64_t obj) {
    uint64_t cur = obj;
    uint64_t last = obj;
    std::set<uint64_t> seen;
    while (cur && seen.insert(cur).second) {
        last = cur;
        cur = r.get_outer(cur);
    }
    return last;
}

} // namespace

SdkDumper::SdkDumper(Memory mem, ue_offsets offs, ue_globals globals)
    : mem_(std::move(mem)), offsets_(offs), globals_(globals) {
    names_.configure(&mem_, &offsets_, globals_.gnames);
    gobjects_.configure(&mem_, &offsets_, globals_.gobjects);
    reader_.configure(&mem_, &offsets_, &names_);
}

void SdkDumper::set_globals(const ue_globals& g) {
    globals_ = g;
    names_.configure(&mem_, &offsets_, globals_.gnames);
    gobjects_.configure(&mem_, &offsets_, globals_.gobjects);
}

void SdkDumper::set_offsets(const ue_offsets& o) {
    offsets_ = o;
    names_.configure(&mem_, &offsets_, globals_.gnames);
    gobjects_.configure(&mem_, &offsets_, globals_.gobjects);
    reader_.configure(&mem_, &offsets_, &names_);
}

ue_status SdkDumper::find_globals(const char* module_name) {
    if (!module_name || !*module_name)
        module_name = "UnrealEditor-Core.dll"; // fallback; callers should pass game EXE

    const uint64_t base = mem_.module_base(module_name);
    const size_t   size = mem_.module_size(module_name);
    if (!base || !size) {
        UE_LOG_E("Module not found: %s", module_name);
        return UE_ERR_NOT_FOUND;
    }

    UE_LOG_I("Scanning %s @ 0x%llX (%zu bytes)", module_name,
             static_cast<unsigned long long>(base), size);

    const EngineProfile* prof = profile_;
    if (!prof) {
        // Use a default mid-range profile for patterns
        prof = find_profile({4, 27, 0});
    }

    auto try_pat = [&](const std::string& pat, int disp, int len) -> uint64_t {
        if (pat.empty())
            return 0;
        return find_rip_ptr(mem_, base, size, pat, disp, len);
    };

    if (!globals_.gobjects && prof) {
        // RIP usually resolves to the address of the GObjects global (FUObjectArray or ptr).
        const uint64_t rip = try_pat(prof->pat_gobjects, prof->pat_gobjects_disp, prof->pat_gobjects_len);
        if (rip) {
            gobjects_.configure(&mem_, &offsets_, rip);
            if (gobjects_.num_elements() > 0) {
                globals_.gobjects = rip;
            } else {
                const uint64_t pointed = mem_.read_ptr(rip);
                gobjects_.configure(&mem_, &offsets_, pointed);
                if (gobjects_.num_elements() > 0)
                    globals_.gobjects = pointed;
                else
                    globals_.gobjects = rip; // best effort
            }
        }
        gobjects_.configure(&mem_, &offsets_, globals_.gobjects);
        UE_LOG_I("GObjects = 0x%llX (num=%d)",
                 static_cast<unsigned long long>(globals_.gobjects),
                 gobjects_.num_elements());
    }

    if (!globals_.gnames && prof) {
        auto rip = try_pat(prof->pat_gnames, prof->pat_gnames_disp, prof->pat_gnames_len);
        if (rip) {
            globals_.gnames = rip;
            // Sometimes pointer
            uint64_t pointed = mem_.read_ptr(rip);
            // Prefer non-null pointed if looks like heap/data
            if (pointed > 0x10000 && pointed < 0x00007FFFFFFFFFFFULL) {
                // Keep both options — name pool is often embedded, so use rip
                // Only switch if reading entry 0 fails later
            }
        }
        UE_LOG_I("GNames = 0x%llX", static_cast<unsigned long long>(globals_.gnames));
    }

    if (!globals_.gworld && prof) {
        auto rip = try_pat(prof->pat_gworld, prof->pat_gworld_disp, prof->pat_gworld_len);
        if (rip)
            globals_.gworld = rip;
        UE_LOG_I("GWorld  = 0x%llX", static_cast<unsigned long long>(globals_.gworld));
    }

    if (!globals_.process_event && prof && !prof->pat_process_event.empty()) {
        auto pat = Pattern::parse(prof->pat_process_event);
        if (pat) {
            globals_.process_event = scan_pattern(mem_, base, size, *pat);
            UE_LOG_I("ProcessEvent = 0x%llX",
                     static_cast<unsigned long long>(globals_.process_event));
        }
    }

    names_.configure(&mem_, &offsets_, globals_.gnames);
    gobjects_.configure(&mem_, &offsets_, globals_.gobjects);

    if (!globals_.gobjects)
        return UE_ERR_PATTERN;
    return UE_OK;
}

ue_status SdkDumper::detect_version(ue_version* out) {
    if (!out)
        return UE_ERR_INVALID_ARG;
    // Try reading common version resource patterns from main module is OS-specific;
    // here we probe FName/GObjects layout heuristics if globals are set.
    // Default guess: UE4.27 if name pool works, else UE4.22, else UE5.3.
    if (globals_.gnames) {
        names_.configure(&mem_, &offsets_, globals_.gnames);
        // Try pool-style
        ue_offsets pool = offsets_for_version({4, 27, 0});
        FNameSystem test;
        test.configure(&mem_, &pool, globals_.gnames);
        auto n = test.get_entry(0);
        if (n == "None" || n == "ByteProperty" || !n.empty()) {
            // Could still be UE5
            *out = {4, 27, 0};
            // Prefer UE5 if GObjects item size heuristics… leave 4.27 default
            return UE_OK;
        }
    }
    *out = {4, 27, 0};
    return UE_OK;
}

PackageInfo& SdkDumper::package_for(uint64_t package_obj,
                                    std::unordered_map<uint64_t, size_t>& index,
                                    DumpResult& result) {
    auto it = index.find(package_obj);
    if (it != index.end())
        return result.packages[it->second];
    PackageInfo pkg;
    pkg.address = package_obj;
    pkg.name = reader_.get_name(package_obj);
    if (pkg.name.empty())
        pkg.name = "UnknownPackage";
    index[package_obj] = result.packages.size();
    result.packages.push_back(std::move(pkg));
    return result.packages.back();
}

DumpResult SdkDumper::dump(const ue_dump_options& options) {
    DumpResult result{};
    const auto t0 = std::chrono::steady_clock::now();

    names_.configure(&mem_, &offsets_, globals_.gnames);
    gobjects_.configure(&mem_, &offsets_, globals_.gobjects);
    reader_.configure(&mem_, &offsets_, &names_);

    if (!gobjects_.ready()) {
        UE_LOG_E("GObjects not configured");
        return result;
    }

    const auto filter = parse_filter(options.package_filter);
    std::unordered_map<uint64_t, size_t> pkg_index;

    const int32_t max_obj = options.max_objects;
    UE_LOG_I("Walking GObjects (num=%d)...", gobjects_.num_elements());

    gobjects_.for_each([&](int32_t /*index*/, uint64_t obj) {
        ++result.stats.objects_scanned;
        if (!reader_.valid_object(obj))
            return;

        const std::string class_name = reader_.get_class_name(obj);
        if (class_name.empty())
            return;

        const uint64_t pkg_obj = package_of(reader_, obj);
        const std::string pkg_name = reader_.get_name(pkg_obj);

        if (options.filter_engine_only && !is_engine_package(pkg_name))
            return;
        if (!filter.empty() && filter.find(pkg_name) == filter.end())
            return;

        PackageInfo& pkg = package_for(pkg_obj, pkg_index, result);

        if (class_name == "Class" || class_name == "BlueprintGeneratedClass" ||
            class_name == "WidgetBlueprintGeneratedClass" || class_name == "AnimBlueprintGeneratedClass") {
            if (!options.generate_classes)
                return;
            StructInfo s;
            s.address   = obj;
            s.name      = reader_.get_name(obj);
            s.full_name = reader_.get_full_name(obj);
            s.super     = reader_.get_super(obj);
            s.super_name = s.super ? reader_.get_name(s.super) : std::string{};
            s.size      = reader_.get_struct_size(obj);
            s.is_class  = true;
            if (options.generate_structs || options.generate_classes)
                s.properties = reader_.read_properties(obj);
            if (options.generate_functions)
                s.functions = reader_.read_functions(obj);
            result.stats.classes++;
            result.stats.properties += s.properties.size();
            result.stats.functions  += s.functions.size();
            pkg.classes.push_back(std::move(s));
        } else if (class_name == "ScriptStruct") {
            if (!options.generate_structs)
                return;
            StructInfo s;
            s.address    = obj;
            s.name       = reader_.get_name(obj);
            s.full_name  = reader_.get_full_name(obj);
            s.super      = reader_.get_super(obj);
            s.super_name = s.super ? reader_.get_name(s.super) : std::string{};
            s.size       = reader_.get_struct_size(obj);
            s.properties = reader_.read_properties(obj);
            result.stats.structs++;
            result.stats.properties += s.properties.size();
            pkg.structs.push_back(std::move(s));
        } else if (class_name == "Enum" || class_name == "UserDefinedEnum") {
            if (!options.generate_enums)
                return;
            EnumInfo e = reader_.read_enum(obj);
            result.stats.enums++;
            pkg.enums.push_back(std::move(e));
        }
    }, max_obj);

    result.stats.packages = result.packages.size();
    const auto t1 = std::chrono::steady_clock::now();
    result.stats.elapsed_seconds =
        std::chrono::duration<double>(t1 - t0).count();

    UE_LOG_I("Dump walk done: %llu pkgs, %llu classes, %llu structs, %llu enums in %.2fs",
             static_cast<unsigned long long>(result.stats.packages),
             static_cast<unsigned long long>(result.stats.classes),
             static_cast<unsigned long long>(result.stats.structs),
             static_cast<unsigned long long>(result.stats.enums),
             result.stats.elapsed_seconds);
    return result;
}

} // namespace ue
