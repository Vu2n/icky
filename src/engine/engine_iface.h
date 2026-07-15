#pragma once

#include "core/modules.h"
#include "model/sdk_model.h"

#include <memory>
#include <string>
#include <vector>

namespace icky {

struct DetectResult {
    bool        matched = false;
    float       confidence = 0.f; // 0..1
    std::string detail;
    ModuleInfo  primary{};
};

class IEngine {
public:
    virtual ~IEngine() = default;
    virtual icky_engine id() const = 0;
    virtual const char* name() const = 0;
    virtual DetectResult detect() const = 0;
    virtual bool dump(SdkModel& out) = 0;
};

using EnginePtr = std::unique_ptr<IEngine>;

std::vector<EnginePtr>& engine_registry();
icky_engine detect_best_engine(DetectResult* info = nullptr);
IEngine* engine_by_id(icky_engine id);
bool dump_engine(icky_engine id, SdkModel& out, DetectResult* det = nullptr);

} // namespace icky
