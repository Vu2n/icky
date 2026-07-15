#include "string_decrypt.h"
#include "core/logger.h"
#include "core/memory.h"
#include "core/pattern.h"

#include <cctype>
#include <Windows.h>

namespace icky::mono_str {

bool MonoStringApi::init(uint64_t mono_module_base) {
    mono_base = mono_module_base;
    if (!mono_base) return false;

    auto r = [&](const char* n) { return get_export(mono_base, n); };

    uint64_t a = r("mono_string_to_utf8");
    if (!a) a = r("mono_string_to_utf8_checked");
    to_utf8 = reinterpret_cast<to_utf8_t>(a);

    uint64_t f = r("g_free");
    if (!f) f = r("mono_free");
    g_free = reinterpret_cast<free_t>(f);

    string_new = reinterpret_cast<string_new_t>(r("mono_string_new"));

    ILOG_I("Mono strings: to_utf8=%p free=%p new=%p", (void*)a, (void*)f, (void*)string_new);
    return to_utf8 != nullptr;
}

std::string MonoStringApi::read_string(uint64_t mono_string) const {
    if (!Mem::valid_user_ptr(mono_string)) return {};

    if (to_utf8) {
        char* u = to_utf8(reinterpret_cast<void*>(mono_string));
        if (u && is_readable(u, 1)) {
            std::string s(u);
            if (g_free) g_free(u);
            return s;
        }
    }

    int32_t len = 0;
    if (!Mem::read(mono_string + 0x10, len) || len <= 0 || len > 100000)
        return {};
    return Mem::wstr(mono_string + 0x14, static_cast<size_t>(len));
}

std::vector<std::pair<std::string, std::string>>
MonoStringApi::sample_decrypted(const uint8_t* image, size_t size, size_t max_samples) const {
    std::vector<std::pair<std::string, std::string>> out;
    if (!image || size < 64) return out;

    auto printable = [](const std::string& s) {
        if (s.size() < 4) return false;
        size_t g = 0;
        for (unsigned char c : s)
            if (std::isprint(c)) ++g;
        return g >= s.size() * 8 / 10;
    };

    std::string cur;
    for (size_t i = 0; i < size && out.size() < max_samples / 2; ++i) {
        if (std::isprint(image[i])) cur.push_back(static_cast<char>(image[i]));
        else {
            if (cur.size() >= 8) out.emplace_back("plain", cur);
            cur.clear();
        }
    }

    for (int key = 1; key < 256 && out.size() < max_samples; ++key) {
        for (size_t off = 0x1000; off + 32 < size && out.size() < max_samples; off += 0x5000) {
            std::string s(32, '\0');
            for (int i = 0; i < 32; ++i)
                s[i] = static_cast<char>(image[off + i] ^ static_cast<uint8_t>(key));
            size_t n = 0;
            while (n < s.size() && std::isprint(static_cast<unsigned char>(s[n]))) ++n;
            s.resize(n);
            if (printable(s))
                out.emplace_back("xor_" + std::to_string(key), s);
        }
    }

    ILOG_I("Mono string samples: %zu", out.size());
    return out;
}

} // namespace icky::mono_str
