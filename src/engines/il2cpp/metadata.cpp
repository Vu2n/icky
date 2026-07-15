#include "metadata.h"
#include "string_decrypt.h"
#include "core/logger.h"
#include "core/modules.h"
#include "core/fsutil.h"
#include "core/memory.h"
#include "core/pattern.h"

#include <Windows.h>
#include <fstream>
#include <filesystem>
#include <cstring>

namespace fs = std::filesystem;

namespace icky::il2cpp {
namespace {

std::vector<std::string> candidate_metadata_paths() {
    std::vector<std::string> paths;
    char exe[MAX_PATH]{};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    fs::path exe_path(exe);
    fs::path dir = exe_path.parent_path();
    std::string stem = exe_path.stem().string();

    // Unity players: <game>_Data/il2cpp_data/Metadata/global-metadata.dat
    // or GameName_Data/...
    auto add = [&](const fs::path& p) {
        paths.push_back(p.string());
    };

    add(dir / (stem + "_Data") / "il2cpp_data" / "Metadata" / "global-metadata.dat");
    add(dir / "Game_Data" / "il2cpp_data" / "Metadata" / "global-metadata.dat");
    // Some layouts
    std::error_code ec;
    for (auto& ent : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!ent.is_directory()) continue;
        auto name = ent.path().filename().string();
        if (name.size() > 5 && name.find("_Data") != std::string::npos) {
            add(ent.path() / "il2cpp_data" / "Metadata" / "global-metadata.dat");
            add(ent.path() / "Metadata" / "global-metadata.dat");
        }
    }
    return paths;
}

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream is(path, std::ios::binary);
    if (!is) return false;
    is.seekg(0, std::ios::end);
    const auto sz = is.tellg();
    if (sz <= 0 || sz > 0x20000000) return false;
    is.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(sz));
    is.read(reinterpret_cast<char*>(out.data()), sz);
    return static_cast<size_t>(is.gcount()) == out.size();
}

} // namespace

std::optional<MetadataBlob> load_global_metadata() {
    for (auto& p : candidate_metadata_paths()) {
        MetadataBlob b;
        if (!read_file(p, b.data)) continue;
        b.path = p;
        ILOG_I("Loaded metadata: %s (%zu bytes)", p.c_str(), b.data.size());
        if (b.data.size() >= 8) {
            int32_t sanity = 0, ver = 0;
            std::memcpy(&sanity, b.data.data(), 4);
            std::memcpy(&ver, b.data.data() + 4, 4);
            b.version = ver;
            if (sanity != static_cast<int32_t>(0xFAB11BAF)) {
                ILOG_W("Metadata sanity mismatch (0x%08X) — attempting decrypt", sanity);
                b.was_encrypted = true;
                try_decrypt_metadata(b);
            } else {
                ILOG_I("Metadata OK version=%d", ver);
            }
        }
        return b;
    }
    ILOG_W("global-metadata.dat not found near executable");
    return std::nullopt;
}

bool try_decrypt_metadata(MetadataBlob& blob) {
    if (blob.data.size() < 256) return false;
    const auto original = blob.data;

    auto check = [&](std::vector<uint8_t>& d) -> bool {
        if (d.size() < 8) return false;
        int32_t sanity = 0;
        std::memcpy(&sanity, d.data(), 4);
        return sanity == static_cast<int32_t>(0xFAB11BAF);
    };

    // 1) Single-byte XOR entire file
    for (int k = 1; k < 256; ++k) {
        blob.data = original;
        for (auto& b : blob.data) b = static_cast<uint8_t>(b ^ static_cast<uint8_t>(k));
        if (check(blob.data)) {
            int32_t ver = 0;
            std::memcpy(&ver, blob.data.data() + 4, 4);
            blob.version = ver;
            ILOG_I("Metadata decrypted with XOR key 0x%02X (version %d)", k, ver);
            return true;
        }
    }

    // 2) Rolling XOR
    for (int k = 1; k < 256; k += 1) {
        blob.data = original;
        uint8_t key = static_cast<uint8_t>(k);
        for (auto& b : blob.data) {
            b = static_cast<uint8_t>(b ^ key);
            key = static_cast<uint8_t>(key + 1);
        }
        if (check(blob.data)) {
            int32_t ver = 0;
            std::memcpy(&ver, blob.data.data() + 4, 4);
            blob.version = ver;
            ILOG_I("Metadata decrypted with rolling XOR start=0x%02X", k);
            return true;
        }
    }

    // 3) Header-only encryption (first 0x100 / 0x400 bytes)
    for (size_t hdr : {size_t(0x100), size_t(0x400), size_t(0x1000)}) {
        if (original.size() < hdr) continue;
        for (int k = 1; k < 256; ++k) {
            blob.data = original;
            for (size_t i = 0; i < hdr; ++i)
                blob.data[i] = static_cast<uint8_t>(blob.data[i] ^ static_cast<uint8_t>(k));
            if (check(blob.data)) {
                std::memcpy(&blob.version, blob.data.data() + 4, 4);
                ILOG_I("Metadata header XOR 0x%02X len=0x%zX", k, hdr);
                return true;
            }
        }
    }

    blob.data = original;
    ILOG_W("Failed to auto-decrypt metadata (custom scheme?)");
    return false;
}

std::string meta_string(const MetadataBlob& blob, int32_t offset) {
    if (offset < 0 || blob.data.size() < 8) return {};
    int32_t stringOffset = 0, stringSize = 0;
    std::memcpy(&stringOffset, blob.data.data() + 24, 4); // may vary — see header
    // Actually from our struct: stringOffset at offset 24
    std::memcpy(&stringOffset, blob.data.data() + offsetof(Il2CppGlobalMetadataHeader, stringOffset), 4);
    std::memcpy(&stringSize, blob.data.data() + offsetof(Il2CppGlobalMetadataHeader, stringSize), 4);
    if (stringOffset < 0 || stringSize < 0) return {};
    const size_t abs = static_cast<size_t>(stringOffset + offset);
    if (abs >= blob.data.size()) return {};
    const char* s = reinterpret_cast<const char*>(blob.data.data() + abs);
    // bound
    size_t max = blob.data.size() - abs;
    size_t n = 0;
    while (n < max && s[n]) ++n;
    return std::string(s, n);
}

bool dump_from_metadata(const MetadataBlob& blob, uint64_t ga_base, size_t ga_size, SdkModel& out) {
    out.engine = ICKY_ENGINE_IL2CPP;
    out.engine_detail = "IL2CPP metadata v" + std::to_string(blob.version) +
                        (blob.was_encrypted ? " (decrypted)" : "");
    out.primary_module = {"GameAssembly.dll", ga_base, ga_size};
    out.metadata["metadata_path"] = blob.path;
    out.metadata["metadata_version"] = std::to_string(blob.version);

    if (blob.data.size() < sizeof(Il2CppGlobalMetadataHeader)) {
        ILOG_E("Metadata too small");
        return false;
    }

    Il2CppGlobalMetadataHeader hdr{};
    std::memcpy(&hdr, blob.data.data(), sizeof(hdr));

    // Type definitions: version-dependent size. Common sizes: 88, 92, 100+ bytes.
    // We use a flexible scan of the type definition table for name indices.
    struct TypeDefLite {
        int32_t nameIndex;
        int32_t namespaceIndex;
        // remaining ignored
    };

    const int32_t type_off = hdr.typeDefinitionsOffset;
    const int32_t type_size = hdr.typeDefinitionsSize;
    if (type_off < 0 || type_size <= 0 ||
        static_cast<size_t>(type_off + type_size) > blob.data.size()) {
        ILOG_W("Type definition table invalid — exporting metadata header only");
        SdkType t;
        t.kind = TypeKind::Struct;
        t.name = "Il2CppGlobalMetadataHeader";
        t.ns = "Il2Cpp";
        t.size = static_cast<int32_t>(sizeof(hdr));
        t.comment = "Raw header; type table unreadable for this version";
        out.types.push_back(std::move(t));
        return true;
    }

    // Guess stride by trying common sizes and checking name strings resolve
    const int candidates[] = {88, 92, 96, 100, 104, 108, 112, 116, 120};
    int stride = 0;
    for (int c : candidates) {
        if (type_size % c != 0) continue;
        int32_t nameIndex = 0;
        std::memcpy(&nameIndex, blob.data.data() + type_off, 4);
        auto s = meta_string(blob, nameIndex);
        if (!s.empty() && s.size() < 200) {
            stride = c;
            break;
        }
    }
    if (!stride) {
        // default
        stride = (type_size % 88 == 0) ? 88 : 92;
    }

    const int count = type_size / stride;
    ILOG_I("IL2CPP types: count≈%d stride=%d", count, stride);

    const int max_types = count > 50000 ? 50000 : count;
    for (int i = 0; i < max_types; ++i) {
        const uint8_t* row = blob.data.data() + type_off + static_cast<size_t>(i) * stride;
        int32_t nameIndex = 0, nsIndex = 0;
        std::memcpy(&nameIndex, row + 0, 4);
        std::memcpy(&nsIndex, row + 4, 4);
        // fieldStart / methodStart etc. vary; try common layout for field count @ +48ish

        SdkType t;
        t.kind = TypeKind::Class;
        t.name = meta_string(blob, nameIndex);
        t.ns = meta_string(blob, nsIndex);
        t.full_name = t.ns.empty() ? t.name : (t.ns + "." + t.name);
        if (t.name.empty()) continue;

        // Best-effort field indices (Unity 2019/2021 style)
        // fieldStart int32, methodStart int32 often after parent/elementing
        // We skip deep field parse when layout unknown — still list types.
        out.types.push_back(std::move(t));
    }

    // Methods table — export names when possible
    if (hdr.methodsOffset > 0 && hdr.methodsSize > 0 &&
        static_cast<size_t>(hdr.methodsOffset + hdr.methodsSize) <= blob.data.size()) {
        // Il2CppMethodDefinition often 32–48 bytes; nameIndex at +0
        const int mstride = 32;
        if (hdr.methodsSize % 32 == 0 || hdr.methodsSize % 40 == 0) {
            const int ms = (hdr.methodsSize % 40 == 0) ? 40 : 32;
            const int mc = hdr.methodsSize / ms;
            const int mmax = mc > 100000 ? 100000 : mc;
            // Attach method names as globals samples — also store in a synthetic type
            SdkType methods_root;
            methods_root.kind = TypeKind::Namespace;
            methods_root.name = "MethodList";
            methods_root.ns = "Il2Cpp";
            for (int i = 0; i < mmax && methods_root.methods.size() < 20000; ++i) {
                int32_t nameIndex = 0;
                std::memcpy(&nameIndex,
                            blob.data.data() + hdr.methodsOffset + static_cast<size_t>(i) * ms, 4);
                auto n = meta_string(blob, nameIndex);
                if (n.empty()) continue;
                SdkMethod m;
                m.name = n;
                methods_root.methods.push_back(std::move(m));
            }
            out.types.push_back(std::move(methods_root));
        }
    }

    // Exports from GameAssembly
    if (ga_base) {
        const char* exports[] = {
            "il2cpp_domain_get", "il2cpp_domain_get_assemblies", "il2cpp_class_from_name",
            "il2cpp_class_get_methods", "il2cpp_class_get_fields", "il2cpp_method_get_name",
            "il2cpp_field_get_name", "il2cpp_field_get_offset", "il2cpp_string_new",
            "il2cpp_runtime_invoke", "il2cpp_thread_attach", "il2cpp_resolve_icall",
            "il2cpp_image_get_class_count", "il2cpp_image_get_class",
        };
        for (auto* ex : exports) {
            uint64_t addr = get_export(ga_base, ex);
            if (!addr) continue;
            out.globals.push_back({ex, addr, addr - ga_base, "function", "GameAssembly export"});
        }
    }

    StringDecryptor dec;
    dec.init(ga_base, ga_size);
    out.decrypted_strings = dec.decrypt_literal_samples(blob.data, 48);

    ILOG_I("IL2CPP dump complete: %zu types, %zu globals, %zu string samples",
           out.types.size(), out.globals.size(), out.decrypted_strings.size());
    return !out.types.empty() || !out.globals.empty();
}

} // namespace icky::il2cpp
