#include "string_decrypt.h"
#include "core/logger.h"
#include "core/memory.h"
#include "core/pattern.h"

#include <Windows.h>
#include <algorithm>
#include <cctype>

namespace icky::il2cpp {

bool StringDecryptor::init(uint64_t ga_base, size_t ga_size) {
    game_assembly = ga_base;
    this->ga_size = ga_size;
    if (!ga_base) return false;

    auto resolve = [&](const char* name) -> uint64_t {
        return get_export(ga_base, name);
    };

    string_new_addr = resolve("il2cpp_string_new");
    if (!string_new_addr)
        string_new_addr = resolve("il2cpp_string_new_wrapper");

    const uint64_t chars = resolve("il2cpp_string_chars");
    const uint64_t len   = resolve("il2cpp_string_length");

    string_new    = reinterpret_cast<il2cpp_string_new_t>(string_new_addr);
    string_chars  = reinterpret_cast<il2cpp_string_chars_t>(chars);
    string_length = reinterpret_cast<il2cpp_string_length_t>(len);

    ILOG_I("IL2CPP strings: new=%p chars=%p length=%p",
           (void*)string_new_addr, (void*)chars, (void*)len);
    return string_new_addr != 0 || chars != 0;
}

std::string StringDecryptor::read_il2cpp_string(uint64_t string_obj) const {
    if (!Mem::valid_user_ptr(string_obj)) return {};

    if (string_chars && string_length) {
        int32_t n = string_length(reinterpret_cast<void*>(string_obj));
        if (n > 0 && n <= 1'000'000) {
            uint16_t* ch = string_chars(reinterpret_cast<void*>(string_obj));
            if (ch && is_readable(ch, static_cast<size_t>(n) * 2)) {
                std::string out;
                out.reserve(static_cast<size_t>(n));
                for (int32_t i = 0; i < n; ++i) {
                    uint16_t c = ch[i];
                    out.push_back(c < 128 ? static_cast<char>(c) : '?');
                }
                return out;
            }
        }
    }

    int32_t length = 0;
    if (!Mem::read(string_obj + 0x10, length) || length <= 0 || length > 100000)
        return {};
    return Mem::wstr(string_obj + 0x14, static_cast<size_t>(length));
}

std::string StringDecryptor::xor_decrypt(const uint8_t* data, size_t len, uint8_t key) {
    std::string s;
    s.resize(len);
    for (size_t i = 0; i < len; ++i)
        s[i] = static_cast<char>(data[i] ^ key);
    return s;
}

std::string StringDecryptor::rolling_xor_decrypt(const uint8_t* data, size_t len, uint8_t key) {
    std::string s;
    s.resize(len);
    uint8_t k = key;
    for (size_t i = 0; i < len; ++i) {
        s[i] = static_cast<char>(data[i] ^ k);
        k = static_cast<uint8_t>(k + 1);
    }
    return s;
}

bool StringDecryptor::looks_printable(const std::string& s) {
    if (s.size() < 3) return false;
    size_t good = 0;
    for (unsigned char c : s) {
        if (c == 0) break;
        if (std::isprint(c) || c == '\n' || c == '\r' || c == '\t')
            ++good;
    }
    return good >= s.size() * 8 / 10 && good >= 3;
}

std::vector<std::pair<std::string, std::string>>
StringDecryptor::decrypt_literal_samples(const std::vector<uint8_t>& blob,
                                         size_t max_samples) const {
    std::vector<std::pair<std::string, std::string>> out;
    if (blob.size() < 64) return out;

    auto push_unique = [&](const std::string& label, const std::string& val) {
        if (!looks_printable(val)) return;
        if (out.size() >= max_samples) return;
        for (auto& e : out)
            if (e.second == val) return;
        out.emplace_back(label, val);
    };

    std::string cur;
    for (size_t i = 0; i < blob.size() && out.size() < max_samples / 2; ++i) {
        unsigned char c = blob[i];
        if (std::isprint(c)) cur.push_back(static_cast<char>(c));
        else {
            if (cur.size() >= 6) push_unique("plain", cur);
            cur.clear();
        }
    }

    for (uint8_t key = 1; key < 255 && out.size() < max_samples; key += 1) {
        for (size_t off = 0x100; off + 64 < blob.size() && out.size() < max_samples; off += 0x4000) {
            auto s = xor_decrypt(blob.data() + off, 48, key);
            size_t n = 0;
            while (n < s.size() && std::isprint(static_cast<unsigned char>(s[n]))) ++n;
            s.resize(n);
            if (looks_printable(s) && s.size() >= 6)
                push_unique("xor_" + std::to_string(key), s);
        }
    }

    for (uint8_t key : {uint8_t(0x11), uint8_t(0x55), uint8_t(0xAA), uint8_t(0x42)}) {
        if (out.size() >= max_samples) break;
        for (size_t off = 0x200; off + 64 < blob.size() && out.size() < max_samples; off += 0x8000) {
            auto s = rolling_xor_decrypt(blob.data() + off, 40, key);
            size_t n = 0;
            while (n < s.size() && std::isprint(static_cast<unsigned char>(s[n]))) ++n;
            s.resize(n);
            if (looks_printable(s))
                push_unique("rxor_" + std::to_string(key), s);
        }
    }

    ILOG_I("IL2CPP string decrypt samples: %zu", out.size());
    return out;
}

} // namespace icky::il2cpp
