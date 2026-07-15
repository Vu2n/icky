// Offline GameAssembly decrypt hunter (Zydis).
// Finds Facepunch-style encrypted field getters and their decrypt stubs.
//
// Usage:
//   decrypt_scan.exe [path\to\GameAssembly.dll] [--rva 0x29D04C0] [--scan]
//
// Default: analyzes known BasePlayer getters from Icky dump, then pattern-scans
// .text for more getter→decrypt edges.

#include <Zydis/Zydis.h>

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct PeImage {
    std::vector<uint8_t> file;
    uint64_t image_base = 0;
    uint32_t size_of_image = 0;
    std::vector<uint8_t> mapped; // section-mapped image (no full PE loader)

    const uint8_t* rva_ptr(uint32_t rva) const {
        if (rva >= mapped.size())
            return nullptr;
        return mapped.data() + rva;
    }

    bool contains_rva(uint32_t rva, size_t n = 1) const {
        return rva < mapped.size() && n <= mapped.size() - rva;
    }
};

bool load_pe(const std::string& path, PeImage& out, std::string& err) {
    std::ifstream is(path, std::ios::binary);
    if (!is) {
        err = "cannot open " + path;
        return false;
    }
    is.seekg(0, std::ios::end);
    const auto sz = is.tellg();
    if (sz <= 0 || sz > 0x40000000) {
        err = "bad file size";
        return false;
    }
    is.seekg(0, std::ios::beg);
    out.file.resize(static_cast<size_t>(sz));
    is.read(reinterpret_cast<char*>(out.file.data()), sz);
    if (!is) {
        err = "read failed";
        return false;
    }

    if (out.file.size() < sizeof(IMAGE_DOS_HEADER)) {
        err = "too small";
        return false;
    }
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(out.file.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        err = "not PE (MZ)";
        return false;
    }
    if (static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > out.file.size()) {
        err = "bad e_lfanew";
        return false;
    }
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(out.file.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        err = "bad PE sig";
        return false;
    }
    if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) {
        err = "not x64";
        return false;
    }

    out.image_base = nt->OptionalHeader.ImageBase;
    out.size_of_image = nt->OptionalHeader.SizeOfImage;
    if (out.size_of_image < 0x1000 || out.size_of_image > 0x40000000) {
        err = "bad SizeOfImage";
        return false;
    }
    out.mapped.assign(out.size_of_image, 0);

    // headers
    const size_t hdr = (std::min)(static_cast<size_t>(nt->OptionalHeader.SizeOfHeaders), out.file.size());
    std::memcpy(out.mapped.data(), out.file.data(), hdr);

    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& s = sec[i];
        if (!s.SizeOfRawData || !s.PointerToRawData)
            continue;
        if (s.PointerToRawData + s.SizeOfRawData > out.file.size())
            continue;
        if (s.VirtualAddress >= out.size_of_image)
            continue;
        const size_t copy = (std::min)(
            static_cast<size_t>(s.SizeOfRawData),
            static_cast<size_t>(out.size_of_image - s.VirtualAddress));
        std::memcpy(out.mapped.data() + s.VirtualAddress,
                    out.file.data() + s.PointerToRawData, copy);
    }
    return true;
}

struct TextRange {
    uint32_t rva = 0;
    uint32_t size = 0;
};

std::vector<TextRange> text_sections(const PeImage& pe) {
    std::vector<TextRange> out;
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(pe.file.data());
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(pe.file.data() + dos->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        char name[9]{};
        std::memcpy(name, sec[i].Name, 8);
        const bool exec = (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        if (exec || std::strcmp(name, ".text") == 0) {
            TextRange t;
            t.rva = sec[i].VirtualAddress;
            t.size = sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
            if (t.rva + t.size > pe.size_of_image)
                t.size = pe.size_of_image - t.rva;
            out.push_back(t);
        }
    }
    return out;
}

struct Decoded {
    ZydisDecodedInstruction ins{};
    ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT]{};
    uint32_t rva = 0;
    uint8_t length = 0;
};

bool decode_at(const PeImage& pe, uint32_t rva, ZydisDecoder& decoder, Decoded& out) {
    if (!pe.contains_rva(rva, 15))
        return false;
    out.rva = rva;
    if (ZYAN_FAILED(ZydisDecoderDecodeFull(
            &decoder, pe.rva_ptr(rva), pe.mapped.size() - rva, &out.ins, out.ops)))
        return false;
    out.length = out.ins.length;
    return true;
}

std::string format_ins(const PeImage& pe, const Decoded& d, ZydisFormatter& fmt) {
    char buf[256]{};
    const uint64_t runtime = pe.image_base + d.rva;
    ZydisFormatterFormatInstruction(&fmt, &d.ins, d.ops, d.ins.operand_count_visible,
                                    buf, sizeof(buf), runtime, ZYAN_NULL);
    return buf;
}

// Relative branch target RVA if present
bool branch_target_rva(const Decoded& d, const PeImage& pe, uint32_t& target_rva) {
    for (int i = 0; i < d.ins.operand_count; ++i) {
        if (d.ops[i].type != ZYDIS_OPERAND_TYPE_IMMEDIATE)
            continue;
        if (!d.ops[i].imm.is_relative)
            continue;
        uint64_t abs = 0;
        if (ZYAN_FAILED(ZydisCalcAbsoluteAddress(&d.ins, &d.ops[i], pe.image_base + d.rva, &abs)))
            continue;
        if (abs < pe.image_base)
            continue;
        target_rva = static_cast<uint32_t>(abs - pe.image_base);
        return pe.contains_rva(target_rva);
    }
    return false;
}

// Memory operand: [reg + disp] → base reg + disp
bool mem_reg_disp(const ZydisDecodedOperand& op, ZydisRegister& base, int64_t& disp) {
    if (op.type != ZYDIS_OPERAND_TYPE_MEMORY)
        return false;
    base = op.mem.base;
    disp = op.mem.disp.value;
    return base != ZYDIS_REGISTER_NONE;
}

bool is_gpr(ZydisRegister r) {
    const auto c = ZydisRegisterGetClass(r);
    return c == ZYDIS_REGCLASS_GPR64 || c == ZYDIS_REGCLASS_GPR32;
}

struct GetterHit {
    uint32_t getter_rva = 0;
    int32_t field_offset = 0;
    uint32_t decrypt_rva = 0;
    uint32_t key_rva = 0; // rip-relative global if found
    std::string note;
};

// Linear decode up to max_bytes looking for:
//   cmp/test [reg+off], 0
//   ...
//   mov rcx/rdx, [reg+off]
//   jmp/call decrypt
bool analyze_getter(const PeImage& pe, ZydisDecoder& decoder, ZydisFormatter& fmt,
                    uint32_t start_rva, GetterHit& hit, std::string* disasm_out,
                    size_t max_bytes = 0x80) {
    hit = {};
    hit.getter_rva = start_rva;

    int32_t field_off = -1;
    ZydisRegister this_reg = ZYDIS_REGISTER_NONE;
    uint32_t decrypt = 0;
    uint32_t key_rva = 0;

    std::string dis;
    uint32_t rva = start_rva;
    const uint32_t end = start_rva + static_cast<uint32_t>(max_bytes);

    while (rva < end) {
        Decoded d{};
        if (!decode_at(pe, rva, decoder, d))
            break;

        char line[320]{};
        std::snprintf(line, sizeof(line), "  %08X  %s\n", rva, format_ins(pe, d, fmt).c_str());
        dis += line;

        // CMP/TEST mem, imm0
        if (d.ins.mnemonic == ZYDIS_MNEMONIC_CMP || d.ins.mnemonic == ZYDIS_MNEMONIC_TEST) {
            ZydisRegister base{};
            int64_t disp = 0;
            if (d.ins.operand_count_visible >= 1 && mem_reg_disp(d.ops[0], base, disp)) {
                if (disp > 0 && disp < 0x4000 && is_gpr(base)) {
                    field_off = static_cast<int32_t>(disp);
                    this_reg = base;
                }
            }
        }

        // MOV reg, [reg+disp] — loading encrypted handle
        if (d.ins.mnemonic == ZYDIS_MNEMONIC_MOV || d.ins.mnemonic == ZYDIS_MNEMONIC_MOVZX) {
            if (d.ins.operand_count_visible >= 2 &&
                d.ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
                d.ops[1].type == ZYDIS_OPERAND_TYPE_MEMORY) {
                ZydisRegister base{};
                int64_t disp = 0;
                if (mem_reg_disp(d.ops[1], base, disp) && disp > 0 && disp < 0x4000) {
                    field_off = static_cast<int32_t>(disp);
                    this_reg = base;
                }
            }
            // MOV reg, [rip+disp] key global
            if (d.ops[1].type == ZYDIS_OPERAND_TYPE_MEMORY &&
                d.ops[1].mem.base == ZYDIS_REGISTER_RIP) {
                uint64_t abs = 0;
                if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&d.ins, &d.ops[1],
                                                          pe.image_base + rva, &abs)) &&
                    abs >= pe.image_base) {
                    key_rva = static_cast<uint32_t>(abs - pe.image_base);
                }
            }
        }

        // LEA reg, [rip+disp] — sometimes key TypeInfo
        if (d.ins.mnemonic == ZYDIS_MNEMONIC_LEA &&
            d.ops[1].type == ZYDIS_OPERAND_TYPE_MEMORY &&
            d.ops[1].mem.base == ZYDIS_REGISTER_RIP) {
            uint64_t abs = 0;
            if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&d.ins, &d.ops[1],
                                                      pe.image_base + rva, &abs)) &&
                abs >= pe.image_base) {
                key_rva = static_cast<uint32_t>(abs - pe.image_base);
            }
        }

        // JMP/CALL to decrypt
        if (d.ins.mnemonic == ZYDIS_MNEMONIC_JMP || d.ins.mnemonic == ZYDIS_MNEMONIC_CALL) {
            uint32_t t = 0;
            if (branch_target_rva(d, pe, t)) {
                // tail jmp is typical for decrypt
                if (d.ins.mnemonic == ZYDIS_MNEMONIC_JMP && field_off > 0) {
                    decrypt = t;
                    rva += d.length;
                    // record and stop after jmp
                    char more[128];
                    std::snprintf(more, sizeof(more), "  ; => decrypt RVA 0x%X\n", t);
                    dis += more;
                    break;
                }
                if (d.ins.mnemonic == ZYDIS_MNEMONIC_CALL && field_off > 0 && !decrypt)
                    decrypt = t;
            }
        }

        if (d.ins.mnemonic == ZYDIS_MNEMONIC_RET || d.ins.mnemonic == ZYDIS_MNEMONIC_RET)
            break;

        rva += d.length;
        if (d.length == 0)
            break;
    }

    if (disasm_out)
        *disasm_out = dis;

    if (field_off <= 0 || !decrypt)
        return false;

    hit.field_offset = field_off;
    hit.decrypt_rva = decrypt;
    hit.key_rva = key_rva;
    hit.note = "cmp/load [this+off] -> jmp decrypt";
    (void)this_reg;
    return true;
}

struct DecryptProfile {
    uint32_t rva = 0;
    size_t size_est = 0;
    int xor_count = 0;
    int and_count = 0;
    int shr_count = 0;
    int shl_count = 0;
    int rol_count = 0;
    int ror_count = 0;
    int bswap_count = 0;
    int mul_count = 0;
    int call_count = 0;
    int mov_mem_count = 0;
    std::vector<uint32_t> callees;
    std::string disasm;
    std::string classification;
};

DecryptProfile profile_decrypt(const PeImage& pe, ZydisDecoder& decoder, ZydisFormatter& fmt,
                               uint32_t start_rva, size_t max_bytes = 0x400) {
    DecryptProfile p;
    p.rva = start_rva;
    uint32_t rva = start_rva;
    const uint32_t end = start_rva + static_cast<uint32_t>(max_bytes);
    std::unordered_set<uint32_t> seen_call;

    while (rva < end) {
        Decoded d{};
        if (!decode_at(pe, rva, decoder, d))
            break;

        char line[320]{};
        std::snprintf(line, sizeof(line), "  %08X  %s\n", rva, format_ins(pe, d, fmt).c_str());
        p.disasm += line;

        switch (d.ins.mnemonic) {
        case ZYDIS_MNEMONIC_XOR:
        case ZYDIS_MNEMONIC_XORPS:
        case ZYDIS_MNEMONIC_PXOR:
            ++p.xor_count;
            break;
        case ZYDIS_MNEMONIC_AND:
            ++p.and_count;
            break;
        case ZYDIS_MNEMONIC_SHR:
        case ZYDIS_MNEMONIC_SAR:
            ++p.shr_count;
            break;
        case ZYDIS_MNEMONIC_SHL:
            ++p.shl_count;
            break;
        case ZYDIS_MNEMONIC_ROL:
            ++p.rol_count;
            break;
        case ZYDIS_MNEMONIC_ROR:
            ++p.ror_count;
            break;
        case ZYDIS_MNEMONIC_BSWAP:
            ++p.bswap_count;
            break;
        case ZYDIS_MNEMONIC_IMUL:
        case ZYDIS_MNEMONIC_MUL:
            ++p.mul_count;
            break;
        case ZYDIS_MNEMONIC_CALL: {
            ++p.call_count;
            uint32_t t = 0;
            if (branch_target_rva(d, pe, t) && seen_call.insert(t).second)
                p.callees.push_back(t);
            break;
        }
        case ZYDIS_MNEMONIC_MOV:
            if (d.ins.operand_count_visible >= 2 &&
                (d.ops[0].type == ZYDIS_OPERAND_TYPE_MEMORY ||
                 d.ops[1].type == ZYDIS_OPERAND_TYPE_MEMORY))
                ++p.mov_mem_count;
            break;
        default:
            break;
        }

        if (d.ins.mnemonic == ZYDIS_MNEMONIC_RET) {
            rva += d.length;
            break;
        }
        // stop at tail jmp far from function (likely only if size wrong)
        if (d.ins.mnemonic == ZYDIS_MNEMONIC_JMP) {
            uint32_t t = 0;
            if (branch_target_rva(d, pe, t)) {
                // if jump outside our window, count as end
                if (t < start_rva || t >= end) {
                    rva += d.length;
                    break;
                }
            }
        }

        rva += d.length;
        if (d.length == 0)
            break;
    }
    p.size_est = rva - start_rva;

    // classify
    if (p.xor_count >= 2 && (p.shr_count + p.shl_count + p.rol_count + p.ror_count) >= 1)
        p.classification = "bitwise_mix (xor+shift) — classic handle decrypt";
    else if (p.xor_count >= 1 && p.and_count >= 1)
        p.classification = "xor+mask decrypt";
    else if (p.call_count >= 1 && p.xor_count == 0 && p.size_est < 0x40)
        p.classification = "thin wrapper (calls helper)";
    else if (p.call_count >= 2)
        p.classification = "multi-call decrypt (GC handle / typed ref?)";
    else if (p.xor_count >= 1)
        p.classification = "xor-based";
    else
        p.classification = "unknown / needs manual read";

    return p;
}

// Scan executable sections for getter pattern:
// within 0x60 bytes: load [reg+disp8/32] then jmp rel32
// Heuristic: look for 48 8B ?? ?? (mov r64, [r64+disp]) then later E9 xx xx xx xx
void scan_getters(const PeImage& pe, ZydisDecoder& decoder, ZydisFormatter& fmt,
                  std::vector<GetterHit>& hits, size_t max_hits = 5000) {
    auto texts = text_sections(pe);
    std::unordered_set<uint32_t> seen_decrypt;

    for (const auto& tr : texts) {
        const uint32_t end = tr.rva + tr.size;
        for (uint32_t rva = tr.rva; rva + 16 < end;) {
            // Fast prefilter: look for mov rax/rcx/rdx/rbx/rsi/rdi/r8-r15, [reg+disp]
            // and jmp near within next 0x50 bytes
            const uint8_t* p = pe.rva_ptr(rva);
            if (!p) {
                ++rva;
                continue;
            }

            // cheap: 48/4C 8B modrm with disp, or 48 8B 4x/5x etc.
            bool maybe = false;
            if ((p[0] == 0x48 || p[0] == 0x4C) && p[1] == 0x8B)
                maybe = true;
            if (!maybe) {
                ++rva;
                continue;
            }

            GetterHit hit{};
            std::string dis;
            if (analyze_getter(pe, decoder, fmt, rva, hit, &dis, 0x60)) {
                // Prefer decrypt targets that look like real code (prologue push/sub rsp)
                if (pe.contains_rva(hit.decrypt_rva, 4)) {
                    const uint8_t* dp = pe.rva_ptr(hit.decrypt_rva);
                    // accept most, but skip if target is ret-only
                    if (hit.field_offset >= 0x20) { // skip tiny struct fields
                        hits.push_back(hit);
                        seen_decrypt.insert(hit.decrypt_rva);
                        if (hits.size() >= max_hits)
                            return;
                    }
                }
                rva += 4;
                continue;
            }
            ++rva;
        }
    }
}

void print_hit(const PeImage& pe, ZydisDecoder& decoder, ZydisFormatter& fmt,
               const GetterHit& h, bool verbose) {
    std::printf("\n[getter] RVA 0x%08X  field=+0x%X  decrypt=0x%08X  key_rva=0x%08X\n",
                h.getter_rva, h.field_offset, h.decrypt_rva, h.key_rva);
    std::printf("  VA getter  0x%llX\n",
                static_cast<unsigned long long>(pe.image_base + h.getter_rva));
    std::printf("  VA decrypt 0x%llX\n",
                static_cast<unsigned long long>(pe.image_base + h.decrypt_rva));

    if (verbose) {
        std::string dis;
        GetterHit tmp;
        analyze_getter(pe, decoder, fmt, h.getter_rva, tmp, &dis, 0x80);
        std::printf("  --- getter disasm ---\n%s", dis.c_str());
    }

    auto prof = profile_decrypt(pe, decoder, fmt, h.decrypt_rva, 0x300);
    std::printf("  decrypt size~0x%zX  class: %s\n", prof.size_est, prof.classification.c_str());
    std::printf("  ops: xor=%d and=%d shr=%d shl=%d rol=%d ror=%d bswap=%d mul=%d call=%d\n",
                prof.xor_count, prof.and_count, prof.shr_count, prof.shl_count,
                prof.rol_count, prof.ror_count, prof.bswap_count, prof.mul_count,
                prof.call_count);
    if (!prof.callees.empty()) {
        std::printf("  callees:");
        for (size_t i = 0; i < prof.callees.size() && i < 12; ++i)
            std::printf(" 0x%X", prof.callees[i]);
        std::printf("\n");
    }
    if (verbose) {
        std::printf("  --- decrypt disasm ---\n%s", prof.disasm.c_str());
    }
}

std::string default_ga() {
    // Prefer path we saw from IDA
    const char* cands[] = {
        R"(D:\SteamLibrary\steamapps\common\Rust\GameAssembly.dll)",
        R"(C:\Program Files (x86)\Steam\steamapps\common\Rust\GameAssembly.dll)",
        R"(C:\Steam\steamapps\common\Rust\GameAssembly.dll)",
        nullptr,
    };
    for (int i = 0; cands[i]; ++i) {
        if (GetFileAttributesA(cands[i]) != INVALID_FILE_ATTRIBUTES)
            return cands[i];
    }
    return {};
}

} // namespace

int main(int argc, char** argv) {
    std::string path;
    bool do_scan = true;
    bool verbose = true;
    std::vector<uint32_t> seed_rvas = {
        // From Icky dump (BasePlayer)
        0x29D04C0, // get_playerEyes
        0x29B0320, // get_playerInventory
        0x29B0380, // get_CameraMode-ish
    };

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--scan")
            do_scan = true;
        else if (a == "--no-scan")
            do_scan = false;
        else if (a == "--quiet")
            verbose = false;
        else if (a == "--rva" && i + 1 < argc) {
            seed_rvas.push_back(static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 0)));
        } else if (a[0] != '-')
            path = a;
    }
    if (path.empty())
        path = default_ga();
    if (path.empty()) {
        std::fprintf(stderr, "Usage: decrypt_scan <GameAssembly.dll> [--rva 0x...] [--scan]\n");
        return 1;
    }

    std::printf("decrypt_scan: %s\n", path.c_str());

    PeImage pe;
    std::string err;
    if (!load_pe(path, pe, err)) {
        std::fprintf(stderr, "PE load failed: %s\n", err.c_str());
        return 1;
    }
    std::printf("ImageBase=0x%llX SizeOfImage=0x%X mapped=%zu\n",
                static_cast<unsigned long long>(pe.image_base), pe.size_of_image,
                pe.mapped.size());

    ZydisDecoder decoder;
    ZydisFormatter formatter;
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    ZydisFormatterInit(&formatter, ZYDIS_FORMATTER_STYLE_INTEL);

    std::vector<GetterHit> hits;
    std::unordered_set<uint32_t> decrypt_set;

    std::printf("\n======== SEED GETTERS (from Icky dump) ========\n");
    for (uint32_t rva : seed_rvas) {
        if (!pe.contains_rva(rva, 16)) {
            std::printf("[skip] RVA 0x%X not in image\n", rva);
            continue;
        }
        GetterHit h{};
        std::string dis;
        if (analyze_getter(pe, decoder, formatter, rva, h, &dis, 0xA0)) {
            hits.push_back(h);
            decrypt_set.insert(h.decrypt_rva);
            print_hit(pe, decoder, formatter, h, verbose);
            if (verbose)
                std::printf("  --- seed disasm ---\n%s", dis.c_str());
        } else {
            std::printf("\n[seed] RVA 0x%08X — pattern not matched, raw disasm:\n%s", rva,
                        dis.c_str());
        }
    }

    // Also profile known decrypt RVAs from partial IDA session
    const uint32_t known_decrypts[] = {0x1B28EE0, 0x1162810, 0};
    std::printf("\n======== KNOWN DECRYPT STUBS ========\n");
    for (int i = 0; known_decrypts[i]; ++i) {
        uint32_t r = known_decrypts[i];
        if (!pe.contains_rva(r, 16))
            continue;
        auto prof = profile_decrypt(pe, decoder, formatter, r, 0x400);
        std::printf("\n[decrypt] RVA 0x%08X VA 0x%llX size~0x%zX\n", r,
                    static_cast<unsigned long long>(pe.image_base + r), prof.size_est);
        std::printf("  %s\n", prof.classification.c_str());
        std::printf("  ops: xor=%d and=%d shr=%d shl=%d rol=%d ror=%d bswap=%d mul=%d call=%d\n",
                    prof.xor_count, prof.and_count, prof.shr_count, prof.shl_count,
                    prof.rol_count, prof.ror_count, prof.bswap_count, prof.mul_count,
                    prof.call_count);
        std::printf("%s", prof.disasm.c_str());
        decrypt_set.insert(r);
    }

    if (do_scan) {
        std::printf("\n======== PATTERN SCAN (.text getters) ========\n");
        std::printf("Scanning (this may take a minute)...\n");
        std::vector<GetterHit> scanned;
        scan_getters(pe, decoder, formatter, scanned, 8000);
        std::printf("Found %zu getter-like sites\n", scanned.size());

        // Cluster by decrypt RVA
        std::unordered_map<uint32_t, std::vector<GetterHit>> by_dec;
        for (auto& h : scanned)
            by_dec[h.decrypt_rva].push_back(h);

        std::vector<std::pair<uint32_t, size_t>> ranks;
        ranks.reserve(by_dec.size());
        for (auto& [k, v] : by_dec)
            ranks.push_back({k, v.size()});
        std::sort(ranks.begin(), ranks.end(),
                  [](auto& a, auto& b) { return a.second > b.second; });

        std::printf("Unique decrypt stubs: %zu\n", ranks.size());
        std::printf("\nTop decrypt stubs by xref count:\n");
        for (size_t i = 0; i < ranks.size() && i < 40; ++i) {
            auto& [drva, cnt] = ranks[i];
            auto prof = profile_decrypt(pe, decoder, formatter, drva, 0x200);
            // sample field offsets
            std::printf("  decrypt=0x%08X  getters=%zu  class=%s  xor=%d call=%d\n",
                        drva, cnt, prof.classification.c_str(), prof.xor_count, prof.call_count);
            // show a few field offsets
            size_t shown = 0;
            for (auto& h : by_dec[drva]) {
                if (shown >= 6)
                    break;
                std::printf("      getter=0x%08X field=+0x%X\n", h.getter_rva, h.field_offset);
                ++shown;
            }
        }

        // Highlight decrypts used by BasePlayer hot offsets
        const int32_t hot[] = {0x380, 0x548, 0x2D0, 0x6D8, 0x4F8, 0x658, 0};
        std::printf("\nHot BasePlayer field offsets in scan:\n");
        for (int i = 0; hot[i]; ++i) {
            for (auto& h : scanned) {
                if (h.field_offset == hot[i]) {
                    std::printf("  +0x%X <- getter 0x%08X decrypt 0x%08X\n", hot[i],
                                h.getter_rva, h.decrypt_rva);
                }
            }
        }

        // Write report next to exe
        char out_path[MAX_PATH]{};
        GetModuleFileNameA(nullptr, out_path, MAX_PATH);
        std::string report = out_path;
        auto slash = report.find_last_of("\\/");
        if (slash != std::string::npos)
            report = report.substr(0, slash + 1);
        report += "decrypt_report.txt";

        std::ofstream os(report, std::ios::trunc);
        if (os) {
            os << "GameAssembly decrypt scan report\n";
            os << "File: " << path << "\n";
            os << "ImageBase: 0x" << std::hex << pe.image_base << std::dec << "\n\n";
            for (size_t i = 0; i < ranks.size() && i < 200; ++i) {
                auto drva = ranks[i].first;
                auto prof = profile_decrypt(pe, decoder, formatter, drva, 0x280);
                os << "DECRYPT RVA 0x" << std::hex << drva << " count=" << std::dec
                   << ranks[i].second << " " << prof.classification << "\n";
                os << prof.disasm << "\n";
            }
            std::printf("\nWrote %s\n", report.c_str());
        }
    }

    std::printf("\nDone.\n");
    return 0;
}
