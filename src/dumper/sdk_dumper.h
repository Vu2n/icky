#pragma once

#include "core/memory.h"
#include "engine/fname.h"
#include "engine/gobjects.h"
#include "engine/uobject.h"
#include "engine/version_db.h"
#include <ue_sdk/types.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace ue {

struct DumpResult {
    std::vector<PackageInfo> packages;
    ue_dump_stats            stats{};
};

class SdkDumper {
public:
    SdkDumper(Memory mem, ue_offsets offs, ue_globals globals);

    void set_profile(const EngineProfile* profile) { profile_ = profile; }

    ue_status find_globals(const char* module_name);
    ue_status detect_version(ue_version* out);

    const ue_globals& globals() const { return globals_; }
    const ue_offsets& offsets() const { return offsets_; }
    void set_globals(const ue_globals& g);
    void set_offsets(const ue_offsets& o);

    // Full reflection walk
    DumpResult dump(const ue_dump_options& options);

    FNameSystem& names() { return names_; }
    UObjectReader& objects() { return reader_; }
    Memory& memory() { return mem_; }

private:
    PackageInfo& package_for(uint64_t package_obj, std::unordered_map<uint64_t, size_t>& index,
                             DumpResult& result);

    Memory        mem_;
    ue_offsets    offsets_{};
    ue_globals    globals_{};
    const EngineProfile* profile_ = nullptr;

    FNameSystem   names_;
    GObjects      gobjects_;
    UObjectReader reader_;
};

} // namespace ue
