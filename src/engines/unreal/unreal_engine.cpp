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
        DetectResult d;
        if (find_module("GameAssembly")) {
            d.detail = "GameAssembly present (prefer IL2CPP)";
            return d;
        }

        auto mod = ue::find_game_module();
        if (!mod.base) {
            d.detail = "no game module";
            return d;
        }
        d.primary = mod;

        const bool shipping = mod.name.find("Shipping") != std::string::npos ||
                              mod.name.find("Win64") != std::string::npos;
        // Light pattern probe (first GObjects pattern only — full scan in dump)
        bool pat = scan_ida(mod.base, mod.size,
                            "48 8B 05 ?? ?? ?? ?? 48 8B 0C C8") != 0 ||
                   scan_ida(mod.base, mod.size,
                            "48 8B 05 ?? ?? ?? ?? 48 8B 0C C8 48 8D 04 D1") != 0;

        if (shipping || pat || mod.size > 0x2000000) {
            d.matched = true;
            d.confidence = pat ? 0.9f : (shipping ? 0.75f : 0.5f);
            d.detail = mod.name + (pat ? " (GObjects pattern)" : " (shipping/size heuristic)");
            return d;
        }
        d.detail = "weak UE signals on " + mod.name;
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
