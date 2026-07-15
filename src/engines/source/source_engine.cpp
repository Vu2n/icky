#include "engine/engine_iface.h"
#include "core/logger.h"
#include "core/modules.h"
#include "core/pattern.h"
#include "core/memory.h"

#include <Windows.h>
#include <unordered_map>
#include <cstring>
#include <string>

namespace icky {
namespace {

// Source1 CreateInterface
using CreateInterfaceFn = void* (*)(const char* name, int* return_code);

CreateInterfaceFn get_create_interface(uint64_t module_base) {
    return reinterpret_cast<CreateInterfaceFn>(get_export(module_base, "CreateInterface"));
}

void* capture_interface(CreateInterfaceFn fn, const char* name) {
    if (!fn || !name) return nullptr;
    {
        int rc = 0;
        return fn(name, &rc);
    } 
}

// RecvTable / ClientClass walk for Source1 netvars
struct RecvProp;
struct RecvTable {
    RecvProp* props;
    int       num;
    void*     decoder;
    char*     net_table_name;
    bool      initialized;
    bool      in_main_list;
};

struct RecvProp {
    char*         name;
    int           type;
    int           flags;
    int           string_buffer_size;
    bool          inside_array;
    const void*   extra;
    RecvProp*     array_prop;
    void*         array_length_proxy;
    void*         proxy_fn;
    void*         data_table_proxy_fn;
    RecvTable*    data_table;
    int           offset;
    int           element_stride;
    int           num_elements;
    const char*   parent_array_prop_name;
};

struct ClientClass {
    void*        create_fn;
    void*        create_event_fn;
    char*        network_name;
    RecvTable*   rt;
    ClientClass* next;
    int          class_id;
};

void dump_recv_table(RecvTable* table, const std::string& prefix, int depth,
                     SdkType& type_out, std::unordered_map<std::string, bool>& visited) {
    if (!table || depth > 12) return;
    char* tname = nullptr;
    {
        tname = table->net_table_name;
    } 
    std::string tn = tname ? Mem::cstr(reinterpret_cast<uint64_t>(tname), 128) : "";
    if (!tn.empty() && visited[tn]) return;
    if (!tn.empty()) visited[tn] = true;

    int num = 0;
    RecvProp* props = nullptr;
    {
        num = table->num;
        props = table->props;
    } 
    if (num <= 0 || num > 1024 || !props) return;

    for (int i = 0; i < num; ++i) {
        RecvProp prop{};
        if (!Mem::read(reinterpret_cast<uint64_t>(props) + static_cast<uint64_t>(i) * sizeof(RecvProp), prop))
            continue;
        std::string pname = prop.name ? Mem::cstr(reinterpret_cast<uint64_t>(prop.name), 128) : "";
        if (pname.empty() || pname == "baseclass") continue;

        const std::string full = prefix.empty() ? pname : (prefix + "." + pname);
        SdkField f;
        f.name = full;
        f.offset = prop.offset;
        f.type_name = "netvar";
        f.comment = tn;
        type_out.fields.push_back(std::move(f));

        if (prop.data_table && prop.type == 6 /* DPT_DataTable */) {
            dump_recv_table(prop.data_table, full, depth + 1, type_out, visited);
        }
    }
}

class SourceEngine final : public IEngine {
    mutable icky_engine detected_id_ = ICKY_ENGINE_SOURCE1;

public:
    icky_engine id() const override { return detected_id_; }
    const char* name() const override { return "Source"; }

    DetectResult detect() const override {
        DetectResult d;

        // Source 2 / Deadlock / CS2
        auto client_s2 = find_module("client.dll");
        auto engine2 = find_module("engine2.dll");
        auto schemasys = find_module("schemasystem.dll");
        if (engine2 || schemasys) {
            detected_id_ = ICKY_ENGINE_SOURCE2;
            d.matched = true;
            d.confidence = 0.95f;
            if (client_s2) d.primary = *client_s2;
            else if (engine2) d.primary = *engine2;
            else d.primary = *schemasys;
            d.detail = "Source 2 (engine2/schemasystem)";
            // Deadlock hint
            if (find_module("citadel") || process_name().find("deadlock") != std::string::npos ||
                process_name().find("project8") != std::string::npos)
                d.detail += " [Deadlock-like]";
            if (process_name().find("cs2") != std::string::npos)
                d.detail += " [CS2]";
            return d;
        }

        // Source 1 — CS:GO
        auto client = find_module("client.dll");
        auto engine = find_module("engine.dll");
        if (client && engine) {
            detected_id_ = ICKY_ENGINE_SOURCE1;
            d.matched = true;
            d.primary = *client;
            d.confidence = 0.9f;
            d.detail = "Source 1 client.dll+engine.dll";
            if (get_export(client->base, "CreateInterface"))
                d.confidence = 0.98f;
            if (process_name().find("csgo") != std::string::npos)
                d.detail += " [CS:GO]";
            return d;
        }
        if (client && get_export(client->base, "CreateInterface")) {
            detected_id_ = ICKY_ENGINE_SOURCE1;
            d.matched = true;
            d.primary = *client;
            d.confidence = 0.7f;
            d.detail = "client.dll CreateInterface";
            return d;
        }

        d.detail = "no Source modules";
        return d;
    }

    bool dump_source1(SdkModel& out, const ModuleInfo& client, const ModuleInfo& engine) {
        out.engine = ICKY_ENGINE_SOURCE1;
        out.engine_detail = "Source 1 (CS:GO / classic)";
        out.primary_module = {client.name, client.base, client.size};
        out.modules.push_back(out.primary_module);
        out.modules.push_back({engine.name, engine.base, engine.size});

        auto ci_client = get_create_interface(client.base);
        auto ci_engine = get_create_interface(engine.base);

        const char* iface_client[] = {
            "VClient018", "VClient017", "VClient016", "VClientEntityList003",
            "GameMovement001", "VClientPrediction001", "VCLIENT"
        };
        const char* iface_engine[] = {
            "VEngineClient014", "VEngineClient013", "VEngineModel016",
            "VEngineCvar007", "ENGINETRACE"
        };

        void* chl = nullptr;
        for (auto* n : iface_client) {
            void* p = capture_interface(ci_client, n);
            if (p) {
                out.globals.push_back({n, reinterpret_cast<uint64_t>(p),
                                       reinterpret_cast<uint64_t>(p) - client.base, "interface", "client"});
                if (std::string(n).find("VClient0") == 0 && !chl) chl = p;
            }
        }
        for (auto* n : iface_engine) {
            void* p = capture_interface(ci_engine, n);
            if (p)
                out.globals.push_back({n, reinterpret_cast<uint64_t>(p),
                                       reinterpret_cast<uint64_t>(p) - engine.base, "interface", "engine"});
        }

        // IBaseClientDLL::GetAllClasses — vtable index typically 8
        if (chl) {
            {
                auto vtable = *reinterpret_cast<void***>(chl);
                // Scan a few slots for GetAllClasses (returns ClientClass*)
                ClientClass* head = nullptr;
                for (int idx = 0; idx < 20 && !head; ++idx) {
                    using fn_t = ClientClass* (*)(void*);
                    auto fn = reinterpret_cast<fn_t>(vtable[idx]);
                    ClientClass* cc = fn(chl);
                    if (!Mem::valid_user_ptr(reinterpret_cast<uint64_t>(cc))) continue;
                    // validate network_name looks like string
                    char* nn = cc->network_name;
                    if (!nn) continue;
                    auto s = Mem::cstr(reinterpret_cast<uint64_t>(nn), 64);
                    if (s.size() >= 3 && s.size() < 64) {
                        head = cc;
                        out.globals.push_back({"GetAllClasses_vtable_index", static_cast<uint64_t>(idx),
                                               static_cast<uint64_t>(idx), "int", ""});
                        out.globals.push_back({"g_pClientClassHead", reinterpret_cast<uint64_t>(head),
                                               reinterpret_cast<uint64_t>(head) - client.base, "ClientClass*", ""});
                        ILOG_I("Source1 ClientClass head via vtable[%d] name=%s", idx, s.c_str());
                    }
                }

                std::unordered_map<std::string, bool> visited;
                int classes = 0;
                for (ClientClass* cc = head; cc && classes < 500; cc = cc->next, ++classes) {
                    SdkType t;
                    t.kind = TypeKind::Class;
                    t.address = reinterpret_cast<uint64_t>(cc);
                    t.ns = "Source1";
                    if (cc->network_name)
                        t.name = Mem::cstr(reinterpret_cast<uint64_t>(cc->network_name), 128);
                    if (t.name.empty()) t.name = "Class_" + std::to_string(classes);
                    t.full_name = t.name;
                    if (cc->rt)
                        dump_recv_table(cc->rt, "", 0, t, visited);
                    out.types.push_back(std::move(t));
                }
                ILOG_I("Source1 netvar classes: %d", classes);
            } 
        }

        // Export CreateInterface RVAs
        if (auto a = get_export(client.base, "CreateInterface"))
            out.globals.push_back({"client.CreateInterface", a, a - client.base, "function", ""});
        if (auto a = get_export(engine.base, "CreateInterface"))
            out.globals.push_back({"engine.CreateInterface", a, a - engine.base, "function", ""});

        return !out.types.empty() || !out.globals.empty();
    }

    bool dump_source2(SdkModel& out, const DetectResult& det) {
        out.engine = ICKY_ENGINE_SOURCE2;
        out.engine_detail = "Source 2 (CS2 / Deadlock / schema)";
        out.primary_module = {det.primary.name, det.primary.base, det.primary.size};

        auto add_mod = [&](const char* n) {
            if (auto m = find_module(n)) {
                out.modules.push_back({m->name, m->base, m->size});
                if (auto ci = get_export(m->base, "CreateInterface"))
                    out.globals.push_back({std::string(n) + "!CreateInterface", ci,
                                           ci - m->base, "function", ""});
            }
        };
        add_mod("client.dll");
        add_mod("engine2.dll");
        add_mod("schemasystem.dll");
        add_mod("tier0.dll");

        // Schema system — dump type scopes via interface
        auto schema = find_module("schemasystem.dll");
        if (schema) {
            auto ci = get_create_interface(schema->base);
            // Common interface names
            const char* names[] = {
                "SchemaSystem_001", "SchemaSystem_002", "SchemaSystem_003",
            };
            void* schema_sys = nullptr;
            for (auto* n : names) {
                schema_sys = capture_interface(ci, n);
                if (schema_sys) {
                    out.globals.push_back({n, reinterpret_cast<uint64_t>(schema_sys),
                                           reinterpret_cast<uint64_t>(schema_sys) - schema->base,
                                           "interface", "schemasystem"});
                    break;
                }
            }

            // Schema dump via patterns is game-build specific.
            // Export module base + known string markers for offline follow-up.
            SdkType marker;
            marker.kind = TypeKind::Namespace;
            marker.name = "SchemaNotes";
            marker.ns = "Source2";
            marker.comment =
                "Full schema class dump is build-dependent. "
                "Icky exports CreateInterface + module RVAs. "
                "Use SchemaSystem interface or CSchemaSystem::GlobalTypeScope for live walks.";
            out.types.push_back(std::move(marker));

            // Heuristic: scan client.dll for "C_BaseEntity" / schema class names as strings
            auto client = find_module("client.dll");
            if (client) {
                const char* needles[] = {
                    "C_BaseEntity", "C_CSPlayerPawn", "CBasePlayerController",
                    "C_DOTA", "C_Citadel", "CBodyComponent",
                };
                const auto* data = reinterpret_cast<const char*>(static_cast<uintptr_t>(client->base));
                {
                    for (auto* needle : needles) {
                        const size_t nlen = std::strlen(needle);
                        for (size_t i = 0; i + nlen < client->size && i < 32 * 1024 * 1024; i += 1) {
                            if (std::memcmp(data + i, needle, nlen) == 0) {
                                SdkType t;
                                t.kind = TypeKind::Class;
                                t.name = needle;
                                t.ns = "SchemaString";
                                t.rva = i;
                                t.address = client->base + i;
                                t.comment = "string xref in client.dll — resolve schema class offline/live";
                                out.types.push_back(std::move(t));
                                break;
                            }
                        }
                    }
                } 
            }
        }

        ILOG_I("Source2 dump: %zu globals, %zu type markers", out.globals.size(), out.types.size());
        return !out.globals.empty() || !out.types.empty();
    }

    bool dump(SdkModel& out) override {
        auto det = detect();
        if (!det.matched) return false;

        if (detected_id_ == ICKY_ENGINE_SOURCE2)
            return dump_source2(out, det);

        auto client = find_module("client.dll");
        auto engine = find_module("engine.dll");
        if (!client) return false;
        ModuleInfo eng = engine ? *engine : ModuleInfo{};
        return dump_source1(out, *client, eng);
    }
};

} // namespace

EnginePtr create_source_engine() {
    return std::make_unique<SourceEngine>();
}

} // namespace icky
