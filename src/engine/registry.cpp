#include "engine_iface.h"
#include "core/logger.h"

namespace icky {

// Factory decls
EnginePtr create_unreal_engine();
EnginePtr create_il2cpp_engine();
EnginePtr create_mono_engine();
EnginePtr create_source_engine();

std::vector<EnginePtr>& engine_registry() {
    static std::vector<EnginePtr> reg = [] {
        std::vector<EnginePtr> v;
        v.push_back(create_il2cpp_engine()); // high priority Unity
        v.push_back(create_mono_engine());
        v.push_back(create_unreal_engine());
        v.push_back(create_source_engine());
        return v;
    }();
    return reg;
}

icky_engine detect_best_engine(DetectResult* info) {
    float best = 0.f;
    icky_engine id = ICKY_ENGINE_UNKNOWN;
    DetectResult best_det{};

    for (auto& e : engine_registry()) {
        auto d = e->detect();
        ILOG_I("Detect %-10s  matched=%d  conf=%.2f  %s",
               e->name(), d.matched ? 1 : 0, d.confidence, d.detail.c_str());
        if (d.matched && d.confidence > best) {
            best = d.confidence;
            id = e->id();
            best_det = std::move(d);
        }
    }
    if (info) *info = best_det;
    return id;
}

IEngine* engine_by_id(icky_engine id) {
    for (auto& e : engine_registry()) {
        if (e->id() == id) return e.get();
    }
    // Source1 and Source2 share one engine class
    if (id == ICKY_ENGINE_SOURCE1 || id == ICKY_ENGINE_SOURCE2) {
        for (auto& e : engine_registry()) {
            if (e->id() == ICKY_ENGINE_SOURCE1 || e->id() == ICKY_ENGINE_SOURCE2)
                return e.get();
        }
    }
    return nullptr;
}

bool dump_engine(icky_engine id, SdkModel& out, DetectResult* det) {
    if (id == ICKY_ENGINE_UNKNOWN) {
        id = detect_best_engine(det);
        if (id == ICKY_ENGINE_UNKNOWN) {
            ILOG_E("No supported engine detected in this process");
            return false;
        }
    } else if (det) {
        if (auto* e = engine_by_id(id))
            *det = e->detect();
    }

    auto* eng = engine_by_id(id);
    if (!eng) {
        ILOG_E("Engine id %d not registered", static_cast<int>(id));
        return false;
    }

    ILOG_I("Dumping with engine: %s", eng->name());
    out = SdkModel{};
    out.engine = id;
    out.game_name = process_name();
    return eng->dump(out);
}

} // namespace icky
