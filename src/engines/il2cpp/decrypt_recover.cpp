#include "decrypt_recover.h"
#include "core/logger.h"

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

#if defined(ICKY_HAS_ZYDIS)
#include <Zydis/Zydis.h>
#endif

namespace icky::il2cpp {
namespace {

size_t safe_copy(const void* src, void* dst, size_t n) {
    size_t got = 0;
    __try {
        std::memcpy(dst, src, n);
        got = n;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        got = 0;
    }
    return got;
}

bool in_module(uint64_t addr, uint64_t base, size_t size) {
    return addr >= base && addr < base + size;
}

std::string lower_copy(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string inner_clear_type(const std::string& type_name) {
    auto lt = type_name.find('<');
    auto gt = type_name.rfind('>');
    if (lt == std::string::npos || gt == std::string::npos || gt <= lt + 1)
        return {};
    std::string inner = type_name.substr(lt + 1, gt - lt - 1);
    auto comma = inner.find(',');
    if (comma != std::string::npos)
        inner = inner.substr(0, comma);
    auto nested = inner.find('<');
    if (nested != std::string::npos)
        inner = inner.substr(0, nested);
    auto dot = inner.find_last_of('.');
    if (dot != std::string::npos)
        inner = inner.substr(dot + 1);
    if (inner.empty() || inner[0] == '%')
        return {};
    bool hex = true;
    for (char c : inner) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') ||
              c == '_')) {
            hex = false;
            break;
        }
    }
    if (hex && inner.size() >= 16)
        return {};
    return inner;
}

bool looks_encrypted_type(const std::string& t) {
    if (t.rfind("Encrypted<", 0) == 0)
        return true;
    if (t.find('%') != std::string::npos && t.find('<') != std::string::npos)
        return true;
    if (t.rfind("enc_", 0) == 0)
        return true;
    return false;
}

bool is_primitive_or_value_junk(const std::string& t) {
    const std::string L = lower_copy(t);
    static const char* k[] = {
        "system.boolean", "boolean", "bool",
        "system.single", "single", "float",
        "system.double", "double",
        "system.int32", "int32", "int",
        "system.uint32", "uint32", "uint",
        "system.int16", "int16", "short",
        "system.byte", "byte",
        "system.char", "char",
        "system.int64", "int64", "long",
        "system.uint64", "uint64", "ulong",
        "unityengine.vector2", "vector2",
        "unityengine.vector3", "vector3",
        "unityengine.vector4", "vector4",
        "unityengine.quaternion", "quaternion",
        "unityengine.color", "color",
        "system.void", "void",
        nullptr,
    };
    for (int i = 0; k[i]; ++i) {
        const size_t n = std::strlen(k[i]);
        if (L == k[i])
            return true;
        if (L.size() >= n && L.compare(L.size() - n, n, k[i]) == 0)
            return true;
    }
    // System.Action / multicast junk
    if (L.find("system.action") != std::string::npos)
        return true;
    if (L.find("system.func") != std::string::npos)
        return true;
    return false;
}

bool is_semantic_encrypted_name(const std::string& name) {
    static const char* k[] = {
        "playereyes", "playerinventory", "playermodel", "playerinput",
        "metabolism", "movement", "heldentity", "blueprints",
        "voicerecorder", "voicespeaker", "playercollision",
        "gestureviewmodel", "clactiveitem", "username",
        "capsulecollider", "enc_capsulecollider",
        nullptr,
    };
    const std::string L = lower_copy(name);
    for (int i = 0; k[i]; ++i)
        if (L == k[i] || L.find(k[i]) != std::string::npos)
            return true;
    return false;
}

// Large mixed-bit XOR/ADD used by Facepunch handle crypto (not stack adjusts).
bool is_crypto_imm(uint32_t v) {
    if (v < 0x10000)
        return false;
    const int32_t s = static_cast<int32_t>(v);
    if (s >= -512 && s <= 512)
        return false; // 0xFFFFFFC8 etc.
    // Reject nearly-all-ones masks
    if (v >= 0xFFFF0000u)
        return false;
    return true;
}

bool is_good_xor(uint32_t v) { return is_crypto_imm(v); }

bool is_good_add(uint32_t v) { return is_crypto_imm(v); }

struct GetterEdge {
    int32_t  field_offset = 0;
    uint64_t getter_rva = 0;
    uint64_t decrypt_rva = 0;
    uint64_t typeinfo_rva = 0;
    bool     used_jmp = false; // tail-jmp decrypt (stronger than call)
    bool     had_null_check = false;
};

struct Algo {
    std::vector<uint32_t> xor_imms;
    std::vector<uint32_t> add_imms;
    std::vector<int>      rols;
    std::string summary;
    bool strong = false; // has real XOR (+ ideally ROL)
};

enum class Confidence : int { Reject = 0, Low = 1, High = 2 };

#if defined(ICKY_HAS_ZYDIS)

bool analyze_getter_zydis(uint64_t fn_va, uint64_t mod_base, size_t mod_size, GetterEdge& out) {
    out = {};
    if (!in_module(fn_va, mod_base, mod_size))
        return false;

    uint8_t code[0xC0]{};
    if (safe_copy(reinterpret_cast<const void*>(fn_va), code, sizeof(code)) < 16)
        return false;

    ZydisDecoder decoder;
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

    int32_t field_off = -1;
    uint64_t decrypt = 0;
    uint64_t typeinfo = 0;
    bool null_check = false;
    bool used_jmp = false;
    size_t offset = 0;

    while (offset + 1 < sizeof(code)) {
        ZydisDecodedInstruction ins;
        ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];
        if (ZYAN_FAILED(ZydisDecoderDecodeFull(&decoder, code + offset, sizeof(code) - offset,
                                               &ins, ops)))
            break;
        const uint64_t ip = fn_va + offset;

        // Null-check: cmp/test [reg+disp], 0
        if (ins.mnemonic == ZYDIS_MNEMONIC_CMP || ins.mnemonic == ZYDIS_MNEMONIC_TEST) {
            for (int i = 0; i < ins.operand_count_visible; ++i) {
                if (ops[i].type != ZYDIS_OPERAND_TYPE_MEMORY)
                    continue;
                if (ops[i].mem.base == ZYDIS_REGISTER_NONE || ops[i].mem.base == ZYDIS_REGISTER_RIP)
                    continue;
                const int64_t disp = ops[i].mem.disp.value;
                if (disp >= 0x20 && disp < 0x4000) {
                    field_off = static_cast<int32_t>(disp);
                    null_check = true;
                }
            }
        }

        if (ins.mnemonic == ZYDIS_MNEMONIC_MOV) {
            for (int i = 0; i < ins.operand_count_visible; ++i) {
                if (ops[i].type != ZYDIS_OPERAND_TYPE_MEMORY)
                    continue;
                if (ops[i].mem.base == ZYDIS_REGISTER_RIP) {
                    uint64_t abs = 0;
                    if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&ins, &ops[i], ip, &abs)) &&
                        in_module(abs, mod_base, mod_size))
                        typeinfo = abs - mod_base;
                    continue;
                }
                if (ops[i].mem.base == ZYDIS_REGISTER_NONE)
                    continue;
                const int64_t disp = ops[i].mem.disp.value;
                // Encrypted handle is pointer-sized load from this+off
                if (disp >= 0x20 && disp < 0x4000 && ops[i].size == 64)
                    field_off = static_cast<int32_t>(disp);
            }
        }

        if (ins.mnemonic == ZYDIS_MNEMONIC_LEA && ops[1].type == ZYDIS_OPERAND_TYPE_MEMORY &&
            ops[1].mem.base == ZYDIS_REGISTER_RIP) {
            uint64_t abs = 0;
            if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&ins, &ops[1], ip, &abs)) &&
                in_module(abs, mod_base, mod_size))
                typeinfo = abs - mod_base;
        }

        if (ins.mnemonic == ZYDIS_MNEMONIC_JMP || ins.mnemonic == ZYDIS_MNEMONIC_CALL) {
            for (int i = 0; i < ins.operand_count; ++i) {
                if (ops[i].type != ZYDIS_OPERAND_TYPE_IMMEDIATE || !ops[i].imm.is_relative)
                    continue;
                uint64_t abs = 0;
                if (ZYAN_FAILED(ZydisCalcAbsoluteAddress(&ins, &ops[i], ip, &abs)))
                    continue;
                if (!in_module(abs, mod_base, mod_size))
                    continue;
                if (field_off <= 0)
                    continue;
                // Tail JMP is the classic encrypted getter shape
                if (ins.mnemonic == ZYDIS_MNEMONIC_JMP) {
                    decrypt = abs - mod_base;
                    used_jmp = true;
                    offset += ins.length;
                    goto done;
                }
                // CALL only if we already saw null-check (weaker)
                if (ins.mnemonic == ZYDIS_MNEMONIC_CALL && null_check && !decrypt)
                    decrypt = abs - mod_base;
            }
        }

        if (ins.mnemonic == ZYDIS_MNEMONIC_RET)
            break;
        offset += ins.length;
        if (!ins.length)
            break;
    }
done:
    if (field_off <= 0 || !decrypt)
        return false;
    // Require null-check OR tail-jmp (reject random mid-function call sites)
    if (!null_check && !used_jmp)
        return false;

    out.field_offset = field_off;
    out.decrypt_rva = decrypt;
    out.typeinfo_rva = typeinfo;
    out.getter_rva = fn_va - mod_base;
    out.used_jmp = used_jmp;
    out.had_null_check = null_check;
    return true;
}

// Extract ONLY the first handle-transform loop (before first CALL after crypto starts).
// Drops stack ADDs, re-encrypt constants, and extra reverse ROLs.
Algo extract_algo_zydis(uint64_t dec_va, uint64_t mod_base, size_t mod_size) {
    Algo a{};
    if (!in_module(dec_va, mod_base, mod_size))
        return a;

    uint8_t code[0x280]{};
    if (safe_copy(reinterpret_cast<const void*>(dec_va), code, sizeof(code)) < 32)
        return a;

    ZydisDecoder decoder;
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

    int last_shl = -1, last_shr = -1;
    size_t offset = 0;
    int ins_count = 0;
    bool saw_crypto = false;
    bool stop = false;

    std::vector<uint32_t> raw_xor, raw_add;
    std::vector<int> raw_rol;

    while (offset + 1 < sizeof(code) && ins_count < 160 && !stop) {
        ZydisDecodedInstruction ins;
        ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];
        if (ZYAN_FAILED(ZydisDecoderDecodeFull(&decoder, code + offset, sizeof(code) - offset,
                                               &ins, ops)))
            break;
        ++ins_count;

        auto imm32 = [&](int op_i) -> bool {
            return op_i < ins.operand_count_visible &&
                   ops[op_i].type == ZYDIS_OPERAND_TYPE_IMMEDIATE && !ops[op_i].imm.is_relative;
        };

        // Once we have crypto, first CALL is GC/type resolve — stop (skip re-encrypt).
        if (saw_crypto && ins.mnemonic == ZYDIS_MNEMONIC_CALL) {
            stop = true;
            break;
        }

        if (ins.mnemonic == ZYDIS_MNEMONIC_XOR && imm32(1)) {
            const uint32_t v = static_cast<uint32_t>(ops[1].imm.value.u);
            if (is_good_xor(v)) {
                raw_xor.push_back(v);
                saw_crypto = true;
            }
        }
        if ((ins.mnemonic == ZYDIS_MNEMONIC_ADD || ins.mnemonic == ZYDIS_MNEMONIC_SUB) &&
            imm32(1)) {
            uint32_t v = static_cast<uint32_t>(ops[1].imm.value.u);
            if (ins.mnemonic == ZYDIS_MNEMONIC_SUB)
                v = static_cast<uint32_t>(-static_cast<int32_t>(v));
            if (is_good_add(v)) {
                raw_add.push_back(v);
                saw_crypto = true;
            }
        }
        if (ins.mnemonic == ZYDIS_MNEMONIC_SHL && imm32(1))
            last_shl = static_cast<int>(ops[1].imm.value.u & 31);
        if ((ins.mnemonic == ZYDIS_MNEMONIC_SHR || ins.mnemonic == ZYDIS_MNEMONIC_SAR) &&
            imm32(1))
            last_shr = static_cast<int>(ops[1].imm.value.u & 31);
        if (ins.mnemonic == ZYDIS_MNEMONIC_OR && last_shl >= 0 && last_shr >= 0 &&
            last_shl + last_shr == 32) {
            raw_rol.push_back(last_shl);
            saw_crypto = true;
            last_shl = last_shr = -1;
        }
        if (ins.mnemonic == ZYDIS_MNEMONIC_ROL && imm32(1)) {
            raw_rol.push_back(static_cast<int>(ops[1].imm.value.u & 31));
            saw_crypto = true;
        }

        if (ins.mnemonic == ZYDIS_MNEMONIC_RET)
            break;

        offset += ins.length;
        if (!ins.length)
            break;
    }

    // Keep first unique crypto ops only (one loop of 2 dwords often repeats).
    auto take_unique = [](const std::vector<uint32_t>& in, size_t maxn) {
        std::vector<uint32_t> o;
        std::unordered_set<uint32_t> s;
        for (auto x : in) {
            if (s.insert(x).second)
                o.push_back(x);
            if (o.size() >= maxn)
                break;
        }
        return o;
    };
    auto take_unique_rol = [](const std::vector<int>& in, size_t maxn) {
        std::vector<int> o;
        std::unordered_set<int> s;
        for (int x : in) {
            if (x <= 0 || x >= 32)
                continue;
            if (s.insert(x).second)
                o.push_back(x);
            if (o.size() >= maxn)
                break;
        }
        return o;
    };

    // Eyes: 1 ADD + 2 ROL + 1 XOR. Inventory: 2 XOR + 1 ROL.
    a.add_imms = take_unique(raw_add, 1);
    a.xor_imms = take_unique(raw_xor, 2);
    a.rols = take_unique_rol(raw_rol, 2);

    a.strong = !a.xor_imms.empty() && (!a.rols.empty() || !a.add_imms.empty());
    // XOR-only with 2 distinct keys is also strong (inventory style)
    if (a.xor_imms.size() >= 2)
        a.strong = true;

    std::ostringstream os;
    for (auto x : a.add_imms)
        os << "ADD_0x" << std::hex << x << std::dec << ";";
    for (int r : a.rols)
        os << "ROL" << r << ";";
    for (auto x : a.xor_imms)
        os << "XOR_0x" << std::hex << x << std::dec << ";";
    a.summary = os.str();
    if (a.summary.empty())
        a.summary = "stub_only";
    return a;
}

// Quick check: decrypt stub contains at least one good XOR imm (handle crypto).
bool decrypt_stub_looks_real(uint64_t dec_va, uint64_t mod_base, size_t mod_size) {
    Algo a = extract_algo_zydis(dec_va, mod_base, mod_size);
    return a.strong || !a.xor_imms.empty();
}

#else
bool analyze_getter_zydis(uint64_t, uint64_t, size_t, GetterEdge&) { return false; }
Algo extract_algo_zydis(uint64_t, uint64_t, size_t) { return {}; }
bool decrypt_stub_looks_real(uint64_t, uint64_t, size_t) { return false; }
#endif

Confidence score_match(const SdkField& f, const SdkMethod& m, const Algo& algo,
                       const GetterEdge& edge) {
    const bool enc_type =
        looks_encrypted_type(f.type_name) || looks_encrypted_type(m.return_type);
    const bool semantic = is_semantic_encrypted_name(f.name);
    const bool prim = is_primitive_or_value_junk(f.type_name);

    // Hard reject primitives/actions unless clearly encrypted wrapper type
    if (prim && !enc_type && !semantic)
        return Confidence::Reject;

    // Must have real crypto for non-wrapper plain reference types
    if (!enc_type && !semantic && !algo.strong)
        return Confidence::Reject;

    // Prefer tail-jmp + null check pattern
    if (!edge.used_jmp && !edge.had_null_check)
        return Confidence::Reject;

    // Weak stub-only on non-encrypted type → reject
    if (!algo.strong && !enc_type && algo.summary == "stub_only")
        return Confidence::Reject;

    // ADD-only junk without XOR on non-wrapper
    if (!enc_type && !semantic && algo.xor_imms.empty())
        return Confidence::Reject;

    if (enc_type || semantic || algo.strong)
        return Confidence::High;

    return Confidence::Low;
}

std::string sanitize_ident(const std::string& name) {
    std::string o;
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
            o.push_back(c);
        else
            o.push_back('_');
    }
    if (o.empty())
        o = "field";
    if (std::isdigit(static_cast<unsigned char>(o[0])))
        o.insert(o.begin(), '_');
    return o;
}

std::string simple_type_key(const std::string& t) {
    if (looks_encrypted_type(t)) {
        auto in = inner_clear_type(t);
        if (!in.empty())
            return lower_copy(in);
    }
    auto dot = t.find_last_of('.');
    std::string s = (dot == std::string::npos) ? t : t.substr(dot + 1);
    auto tick = s.find('`');
    if (tick != std::string::npos)
        s = s.substr(0, tick);
    auto lt = s.find('<');
    if (lt != std::string::npos)
        s = s.substr(0, lt);
    return lower_copy(s);
}

void apply_decrypt_to_field(SdkField& f, const SdkMethod& m, const GetterEdge& edge,
                            const Algo& algo, DecryptRecoverStats& st) {
    f.is_encrypted = true;
    f.decrypt.valid = true;
    f.decrypt.getter_rva = edge.getter_rva;
    f.decrypt.decrypt_rva = edge.decrypt_rva;
    f.decrypt.typeinfo_rva = edge.typeinfo_rva;
    f.decrypt.inner_type = inner_clear_type(f.type_name);
    if (f.decrypt.inner_type.empty())
        f.decrypt.inner_type = inner_clear_type(m.return_type);
    if (f.decrypt.inner_type.empty()) {
        auto sk = simple_type_key(m.return_type);
        if (!sk.empty() && sk[0] != '%' && sk != "object" && sk != "boolean")
            f.decrypt.inner_type = m.return_type; // keep full if needed
        // prefer simple
        auto dot = m.return_type.find_last_of('.');
        if (dot != std::string::npos)
            f.decrypt.inner_type = m.return_type.substr(dot + 1);
    }

    f.decrypt.xor_imms = algo.xor_imms;
    f.decrypt.add_imms = algo.add_imms;
    f.decrypt.rol_amounts = algo.rols;
    f.decrypt.algo_summary = algo.summary;

    char buf[384]{};
    sprintf_s(buf, "encrypted; conf=high; getter=0x%llX decrypt=0x%llX typeinfo=0x%llX algo=%s",
              static_cast<unsigned long long>(f.decrypt.getter_rva),
              static_cast<unsigned long long>(f.decrypt.decrypt_rva),
              static_cast<unsigned long long>(f.decrypt.typeinfo_rva),
              f.decrypt.algo_summary.c_str());
    // strip old decrypt= noise
    if (f.comment.find("decrypt=") != std::string::npos) {
        // replace encrypted;... section if present
        auto p = f.comment.find("encrypted");
        if (p != std::string::npos)
            f.comment = f.comment.substr(0, p);
        while (!f.comment.empty() && (f.comment.back() == ' ' || f.comment.back() == ';'))
            f.comment.pop_back();
    }
    if (f.comment.empty())
        f.comment = buf;
    else {
        f.comment += "; ";
        f.comment += buf;
    }

    if (looks_encrypted_type(f.type_name) || !f.decrypt.inner_type.empty()) {
        if (!f.decrypt.inner_type.empty()) {
            // strip namespace from inner for display
            std::string in = f.decrypt.inner_type;
            auto d = in.find_last_of('.');
            if (d != std::string::npos)
                in = in.substr(d + 1);
            auto tick = in.find('`');
            if (tick != std::string::npos)
                in = in.substr(0, tick);
            if (!in.empty() && in[0] != '%') {
                f.type_name = std::string("Encrypted<") + in + ">";
                ++st.wrapper_types_cleared;
            }
        }
    }

    if (!f.decrypt.inner_type.empty()) {
        std::string want = f.decrypt.inner_type;
        auto d = want.find_last_of('.');
        if (d != std::string::npos)
            want = want.substr(d + 1);
        if (!want.empty())
            want[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(want[0])));
        if (f.name.rfind("field_0x", 0) == 0 || f.name.rfind("enc_", 0) == 0 ||
            f.name.find("0x") != std::string::npos) {
            f.name = want;
        }
    }

    ++st.fields_annotated;
}

} // namespace

DecryptRecoverStats recover_field_decrypts(SdkModel& model, uint64_t module_base,
                                           size_t module_size) {
    DecryptRecoverStats st{};
#if !defined(ICKY_HAS_ZYDIS)
    ILOG_W("Decrypt recover: built without Zydis — skipping");
    model.metadata["decrypt_recover"] = "no_zydis";
    return st;
#else
    if (!module_base || !module_size) {
        ILOG_W("Decrypt recover: no module");
        return st;
    }

    ILOG_I("Decrypt recover: high-confidence encrypted getters only…");

    std::unordered_map<uint64_t, Algo> algo_cache;

    // type key → best edge+algo from high-confidence hits (for second pass)
    struct CachedDec {
        GetterEdge edge;
        Algo algo;
        std::string ret_key;
    };
    std::vector<CachedDec> high_hits;
    high_hits.reserve(4096);

    auto get_algo = [&](uint64_t decrypt_rva) -> Algo {
        auto it = algo_cache.find(decrypt_rva);
        if (it != algo_cache.end())
            return it->second;
        Algo a = extract_algo_zydis(module_base + decrypt_rva, module_base, module_size);
        algo_cache[decrypt_rva] = a;
        if (a.strong)
            ++st.algos_recovered;
        return a;
    };

    // ── Pass 1: 0-arg getters ────────────────────────────────────────────
    for (auto& t : model.types) {
        std::unordered_map<int32_t, size_t> by_off;
        for (size_t fi = 0; fi < t.fields.size(); ++fi)
            if (!t.fields[fi].is_static)
                by_off[t.fields[fi].offset] = fi;

        for (auto& m : t.methods) {
            if (!m.rva || m.is_static || !m.params.empty())
                continue;
            if (m.return_type == "void" || m.return_type == "Void" ||
                m.return_type == "System.Void")
                continue;

            const uint64_t fn = module_base + m.rva;
            if (!in_module(fn, module_base, module_size))
                continue;

            ++st.methods_scanned;
            if ((st.methods_scanned % 25000) == 0)
                ILOG_I("Decrypt recover: %zu methods, %zu high-conf fields, %zu rejected",
                       st.methods_scanned, st.fields_annotated, st.getters_rejected);

            GetterEdge edge{};
            if (!analyze_getter_zydis(fn, module_base, module_size, edge))
                continue;
            ++st.getters_found;

            auto it = by_off.find(edge.field_offset);
            if (it == by_off.end())
                continue;

            auto& f = t.fields[it->second];
            Algo algo = get_algo(edge.decrypt_rva);

            const Confidence conf = score_match(f, m, algo, edge);
            if (conf == Confidence::Reject) {
                ++st.getters_rejected;
                continue;
            }
            if (conf != Confidence::High) {
                ++st.getters_rejected;
                continue;
            }

            // Prefer stronger algo if already annotated
            if (f.decrypt.valid && f.decrypt.decrypt_rva) {
                const bool old_strong =
                    !f.decrypt.xor_imms.empty() &&
                    (!f.decrypt.rol_amounts.empty() || !f.decrypt.add_imms.empty() ||
                     f.decrypt.xor_imms.size() >= 2);
                if (old_strong)
                    continue;
                if (!algo.strong)
                    continue;
            }

            apply_decrypt_to_field(f, m, edge, algo, st);

            CachedDec cd;
            cd.edge = edge;
            cd.algo = algo;
            cd.ret_key = simple_type_key(m.return_type);
            if (cd.ret_key.empty())
                cd.ret_key = simple_type_key(f.type_name);
            high_hits.push_back(std::move(cd));

            if (m.comment.find("decrypt_field") == std::string::npos) {
                char mb[128];
                sprintf_s(mb, "decrypt_field=+0x%X decrypt_rva=0x%llX conf=high",
                          edge.field_offset,
                          static_cast<unsigned long long>(edge.decrypt_rva));
                if (m.comment.empty())
                    m.comment = mb;
                else {
                    m.comment += "; ";
                    m.comment += mb;
                }
            }
        }
    }

    // ── Pass 2: unmatched Encrypted<> wrappers — match by return type / name ─
    ILOG_I("Decrypt recover pass 2: matching remaining Encrypted<> wrappers…");

    // Index high hits by return type key
    std::unordered_map<std::string, std::vector<size_t>> by_ret;
    for (size_t i = 0; i < high_hits.size(); ++i)
        if (!high_hits[i].ret_key.empty())
            by_ret[high_hits[i].ret_key].push_back(i);

    for (auto& t : model.types) {
        // Re-scan methods for this class targeting unmatched encrypted fields
        std::vector<size_t> need;
        for (size_t fi = 0; fi < t.fields.size(); ++fi) {
            auto& f = t.fields[fi];
            if (f.decrypt.valid && f.decrypt.decrypt_rva)
                continue;
            if (!looks_encrypted_type(f.type_name) && !is_semantic_encrypted_name(f.name))
                continue;
            need.push_back(fi);
        }
        if (need.empty())
            continue;

        std::unordered_map<int32_t, size_t> by_off;
        for (size_t fi : need)
            by_off[t.fields[fi].offset] = fi;

        for (auto& m : t.methods) {
            if (!m.rva || m.is_static || !m.params.empty())
                continue;
            const uint64_t fn = module_base + m.rva;
            if (!in_module(fn, module_base, module_size))
                continue;

            GetterEdge edge{};
            if (!analyze_getter_zydis(fn, module_base, module_size, edge))
                continue;

            auto it = by_off.find(edge.field_offset);
            if (it == by_off.end())
                continue;

            auto& f = t.fields[it->second];
            if (f.decrypt.valid && f.decrypt.decrypt_rva)
                continue;

            Algo algo = get_algo(edge.decrypt_rva);
            // For known wrappers, accept if stub has XOR or strong, OR type encrypted
            if (!algo.strong && !looks_encrypted_type(f.type_name) &&
                !decrypt_stub_looks_real(module_base + edge.decrypt_rva, module_base,
                                         module_size)) {
                // Still accept encrypted type + jmp pattern even if algo weak
                if (!looks_encrypted_type(f.type_name) || !edge.used_jmp)
                    continue;
            }

            Confidence conf = score_match(f, m, algo, edge);
            if (conf == Confidence::Reject && looks_encrypted_type(f.type_name) &&
                edge.used_jmp && edge.had_null_check) {
                conf = Confidence::High; // wrapper + classic getter shape
            }
            if (conf != Confidence::High)
                continue;

            apply_decrypt_to_field(f, m, edge, algo, st);
            ++st.second_pass_hits;
        }

        // Type-key fallback: same class already has decrypt for Encrypted<T> elsewhere?
        // Cross-class: if field Encrypted<PlayerEyes> and we have any high hit with ret PlayerEyes
        for (size_t fi : need) {
            auto& f = t.fields[fi];
            if (f.decrypt.valid && f.decrypt.decrypt_rva)
                continue;
            std::string key = simple_type_key(f.type_name);
            if (key.empty())
                continue;
            auto bit = by_ret.find(key);
            if (bit == by_ret.end() || bit->second.empty())
                continue;
            // Use first strong algo hit for this type — but ONLY if field offset was observed
            // with that decrypt on some class. Don't assign wrong offset's decrypt blindly.
            // Instead: only reuse if some hit has the SAME field_offset.
            for (size_t hi : bit->second) {
                const auto& cd = high_hits[hi];
                if (cd.edge.field_offset != f.offset)
                    continue;
                if (!cd.algo.strong && cd.algo.xor_imms.empty())
                    continue;
                SdkMethod dummy{};
                dummy.return_type = key;
                apply_decrypt_to_field(f, dummy, cd.edge, cd.algo, st);
                // Fix getter_rva — unknown for this class; keep decrypt + typeinfo
                f.decrypt.getter_rva = 0;
                ++st.second_pass_hits;
                break;
            }
        }
    }

    // ── Pass 3: rewrite remaining wrapper types WITHOUT marking decrypt ─────
    // Do NOT set is_encrypted flood; only clean type names + light comment.
    for (auto& t : model.types) {
        for (auto& f : t.fields) {
            if (f.decrypt.valid && f.decrypt.decrypt_rva)
                continue;
            if (!looks_encrypted_type(f.type_name))
                continue;
            // Clear hash wrapper name
            auto inner = inner_clear_type(f.type_name);
            if (!inner.empty()) {
                f.type_name = std::string("Encrypted<") + inner + ">";
                f.decrypt.inner_type = inner;
            } else if (f.type_name.find('%') != std::string::npos) {
                f.type_name = "EncryptedObject";
            }
            // Soft flag: encrypted layout but no recovered decrypt
            f.is_encrypted = true;
            f.decrypt.valid = false;
            if (f.comment.find("no_decrypt") == std::string::npos &&
                f.comment.find("decrypt=") == std::string::npos) {
                if (!f.comment.empty())
                    f.comment += "; ";
                f.comment += "encrypted_wrapper; no_decrypt_matched";
            }
            ++st.fields_wrapper_only;
        }
    }

    // Strip false-positive decrypts that slipped through (primitives)
    size_t stripped = 0;
    for (auto& t : model.types) {
        for (auto& f : t.fields) {
            if (!f.decrypt.valid || !f.decrypt.decrypt_rva)
                continue;
            if (is_primitive_or_value_junk(f.type_name) && !looks_encrypted_type(f.type_name) &&
                !is_semantic_encrypted_name(f.name)) {
                f.decrypt = {};
                f.is_encrypted = false;
                // scrub comment
                auto p = f.comment.find("encrypted; conf=high");
                if (p != std::string::npos)
                    f.comment.erase(p);
                ++stripped;
                if (st.fields_annotated)
                    --st.fields_annotated;
            }
        }
    }

    model.metadata["decrypt_recover"] = "1";
    model.metadata["decrypt_fields"] = std::to_string(st.fields_annotated);
    model.metadata["decrypt_getters"] = std::to_string(st.getters_found);
    model.metadata["decrypt_rejected"] = std::to_string(st.getters_rejected);
    model.metadata["decrypt_algos"] = std::to_string(st.algos_recovered);
    model.metadata["decrypt_wrapper_only"] = std::to_string(st.fields_wrapper_only);
    model.metadata["decrypt_second_pass"] = std::to_string(st.second_pass_hits);
    model.engine_detail += " + decrypt";

    ILOG_I("Decrypt recover done: scanned=%zu getters=%zu high_conf=%zu rejected=%zu "
           "algos=%zu wrapper_only=%zu pass2=%zu stripped_fp=%zu",
           st.methods_scanned, st.getters_found, st.fields_annotated, st.getters_rejected,
           st.algos_recovered, st.fields_wrapper_only, st.second_pass_hits, stripped);
    return st;
#endif
}

bool write_decrypt_header(const SdkModel& model, const std::string& path) {
    std::ofstream os(path, std::ios::trunc);
    if (!os)
        return false;

    os << "// ============================================================\n";
    os << "//  Icky — high-confidence encrypted field decrypts\n";
    os << "//  Only entries with real getter→decrypt + crypto / Encrypted<>\n";
    os << "//  Module: " << model.primary_module.name << "\n";
    os << "// ============================================================\n";
    os << "#pragma once\n#include <cstdint>\n\n";
    os << "namespace Icky::Decrypt {\n\n";
    os << "inline constexpr std::uint32_t rol32(std::uint32_t x, int n) {\n";
    os << "    return (x << n) | (x >> (32 - n));\n}\n\n";
    os << "// Encrypted handle layout (Facepunch-style):\n";
    os << "//   +0x10  int  use_counter\n";
    os << "//   +0x14  bool has_value\n";
    os << "//   +0x18  uint64 encrypted_handle\n";
    os << "// Apply *_decrypt_handle, then GC-handle / typed-ref resolve (game decrypt stub).\n\n";

    auto emit_worthy = [](const SdkField& f) -> bool {
        if (!f.decrypt.valid || !f.decrypt.decrypt_rva)
            return false;
        // High confidence only
        if (looks_encrypted_type(f.type_name))
            return true;
        if (is_semantic_encrypted_name(f.name))
            return true;
        if (!f.decrypt.xor_imms.empty() &&
            (!f.decrypt.rol_amounts.empty() || f.decrypt.xor_imms.size() >= 2 ||
             !f.decrypt.add_imms.empty()))
            return true;
        return false;
    };

    size_t count = 0;
    for (const auto& t : model.types) {
        bool any = false;
        for (const auto& f : t.fields)
            if (emit_worthy(f))
                any = true;
        if (!any)
            continue;

        os << "namespace " << sanitize_ident(t.name) << " {\n";
        for (const auto& f : t.fields) {
            if (!emit_worthy(f))
                continue;
            const std::string tag = sanitize_ident(f.name);
            os << "  // " << f.type_name << " @ +0x" << std::hex << f.offset << std::dec;
            if (!f.decrypt.algo_summary.empty())
                os << "  algo=" << f.decrypt.algo_summary;
            os << "\n";
            os << "  constexpr std::uintptr_t " << tag << "_offset = 0x" << std::hex << f.offset
               << std::dec << ";\n";
            if (f.decrypt.getter_rva)
                os << "  constexpr std::uintptr_t " << tag << "_getter_rva = 0x" << std::hex
                   << f.decrypt.getter_rva << std::dec << ";\n";
            os << "  constexpr std::uintptr_t " << tag << "_decrypt_rva = 0x" << std::hex
               << f.decrypt.decrypt_rva << std::dec << ";\n";
            if (f.decrypt.typeinfo_rva)
                os << "  constexpr std::uintptr_t " << tag << "_typeinfo_rva = 0x" << std::hex
                   << f.decrypt.typeinfo_rva << std::dec << ";\n";

            if (!f.decrypt.xor_imms.empty() || !f.decrypt.rol_amounts.empty() ||
                !f.decrypt.add_imms.empty()) {
                os << "  inline std::uint64_t " << tag << "_decrypt_handle(std::uint64_t raw) {\n";
                os << "    std::uint32_t w[2] = { (std::uint32_t)raw, (std::uint32_t)(raw >> 32) };\n";
                os << "    for (int i = 0; i < 2; ++i) {\n";
                os << "      std::uint32_t x = w[i];\n";
                if (!f.decrypt.add_imms.empty() && f.decrypt.rol_amounts.size() >= 2 &&
                    f.decrypt.xor_imms.size() >= 1) {
                    // Eyes style: ADD, ROL, XOR, ROL
                    os << "      x += 0x" << std::hex << f.decrypt.add_imms[0] << std::dec
                       << "u;\n";
                    os << "      x = rol32(x, " << f.decrypt.rol_amounts[0] << ");\n";
                    os << "      x ^= 0x" << std::hex << f.decrypt.xor_imms[0] << std::dec
                       << "u;\n";
                    os << "      x = rol32(x, " << f.decrypt.rol_amounts[1] << ");\n";
                } else if (f.decrypt.xor_imms.size() >= 2 && !f.decrypt.rol_amounts.empty()) {
                    // Inventory style: XOR, ROL, XOR
                    os << "      x ^= 0x" << std::hex << f.decrypt.xor_imms[0] << std::dec
                       << "u;\n";
                    os << "      x = rol32(x, " << f.decrypt.rol_amounts[0] << ");\n";
                    os << "      x ^= 0x" << std::hex << f.decrypt.xor_imms[1] << std::dec
                       << "u;\n";
                } else if (f.decrypt.xor_imms.size() >= 1 && !f.decrypt.rol_amounts.empty() &&
                           !f.decrypt.add_imms.empty()) {
                    os << "      x += 0x" << std::hex << f.decrypt.add_imms[0] << std::dec
                       << "u;\n";
                    os << "      x = rol32(x, " << f.decrypt.rol_amounts[0] << ");\n";
                    os << "      x ^= 0x" << std::hex << f.decrypt.xor_imms[0] << std::dec
                       << "u;\n";
                    if (f.decrypt.rol_amounts.size() > 1)
                        os << "      x = rol32(x, " << f.decrypt.rol_amounts[1] << ");\n";
                } else {
                    for (auto v : f.decrypt.add_imms)
                        os << "      x += 0x" << std::hex << v << std::dec << "u;\n";
                    for (int r : f.decrypt.rol_amounts)
                        os << "      x = rol32(x, " << r << ");\n";
                    for (auto v : f.decrypt.xor_imms)
                        os << "      x ^= 0x" << std::hex << v << std::dec << "u;\n";
                }
                os << "      w[i] = x;\n";
                os << "    }\n";
                os << "    return ((std::uint64_t)w[1] << 32) | w[0];\n";
                os << "  }\n";
            }
            os << "\n";
            ++count;
        }
        os << "} // " << sanitize_ident(t.name) << "\n\n";
    }

    os << "// High-confidence encrypted fields: " << count << "\n";
    os << "} // namespace Icky::Decrypt\n";
    return true;
}

} // namespace icky::il2cpp
