#include "engine/engine_iface.h"
#include "engines/unreal/ue_internals.h"
#include "core/logger.h"
#include "core/modules.h"
#include "core/pattern.h"

namespace icky {
namespace {

class UnrealEngine final : public IEngine {
public:
    icky_engine id() const override { return ICKY_ENGINE_UNREAL; }
    const char* name() const override { return "Unreal"; }

    DetectResult detect() const override {
        // Detect must stay cheap: no pattern scans (those crash protected titles on inject).
        DetectResult d;
        if (find_module_handle("GameAssembly.dll")) {
            d.detail = "GameAssembly present (prefer IL2CPP)";
            return d;
        }
        auto pn = process_name();
        const bool shipping = pn.find("Shipping") != std::string::npos ||
                              pn.find("Win64") != std::string::npos;
        if (shipping) {
            d.matched = true;
            d.confidence = 0.6f;
            d.detail = pn + " (shipping name heuristic)";
            if (auto m = find_module_handle(pn.c_str()))
                d.primary = *m;
            return d;
        }
        d.detail = "no UE module signals";
        return d;
    }

    bool dump(SdkModel& out) override {
        ILOG_I("=== Unreal Engine dump (hardened) ===");
        ue::Globals g{};
        if (!ue::find_globals(g)) {
            ILOG_E("UE: global scan failed");
            out.engine = ICKY_ENGINE_UNREAL;
            out.engine_detail = "scan failed";
            return false;
        }

        ue::Layout layout{};
        if (!ue::discover_layout(g, layout)) {
            ILOG_W("UE: layout discovery failed — exporting raw globals only");
            out.engine = ICKY_ENGINE_UNREAL;
            out.engine_detail = "layout discovery failed";
            out.primary_module = {g.module_name, g.module_base, g.module_size};
            if (g.gobjects)
                out.globals.push_back({"GObjects_raw", g.gobjects,
                                       g.gobjects - g.module_base, "unknown", "unvalidated"});
            if (g.gnames)
                out.globals.push_back({"GNames_raw", g.gnames,
                                       g.gnames - g.module_base, "unknown", "unvalidated"});
            if (g.process_event)
                out.globals.push_back({"ProcessEvent", g.process_event,
                                       g.process_event - g.module_base, "function", ""});
            SdkType t;
            t.kind = TypeKind::Namespace;
            t.name = "LayoutFailed";
            t.ns = "Icky";
            t.comment = "Could not validate FUObjectArray layout. Re-run or add manual offsets.";
            out.types.push_back(std::move(t));
            return true;
        }

        return ue::dump_sdk(g, layout, out);
    }
};

} // namespace

EnginePtr create_unreal_engine() {
    return std::make_unique<UnrealEngine>();
}

} // namespace icky
