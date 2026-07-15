// Recovered from GameAssembly.dll via Zydis decrypt_scan (Rust client build).
// ImageBase (IDA/PE preferred) = 0x180000000 — add to RVA for VA.
// Runtime base is ASLR; always use module_base + RVA.
//
// Pattern (encrypted Il2Cpp object field):
//   getter:
//     cctor init once
//     handle = *(EncryptedValue*)(this + field_offset)
//     jmp decrypt(handle, TypeInfo*)
//   decrypt:
//     uint64 handle_data at handle+0x18 if handle+0x14 != 0
//     transform 2x uint32 in-place (constants below)
//     resolve GC handle / typed ref → real object*
//
#pragma once
#include <cstdint>

namespace Icky::Rust::Decrypt {

// ── BasePlayer.playerEyes (+0x380) ─────────────────────────────────────────
namespace PlayerEyes {
    constexpr std::uintptr_t field_offset   = 0x380;
    constexpr std::uintptr_t getter_rva     = 0x29D04C0;
    constexpr std::uintptr_t decrypt_rva    = 0x1B28EE0;
    constexpr std::uintptr_t typeinfo_rva   = 0x0FC7D500; // static used as rdx key

    // Per-dword (lo, hi of uint64 at encrypted+0x18), 2 iterations:
    //   x = x + 0xBCFC6DA8
    //   x = ROL(x, 19)          // shl 0x13 | shr 0x0D
    //   x = x ^ 0x73437527
    //   x = ROL(x, 10)          // shl 0x0A | shr 0x16
    constexpr std::uint32_t add_const = 0xBCFC6DA8u;
    constexpr std::uint32_t xor_const = 0x73437527u;
    constexpr int rol1 = 19;
    constexpr int rol2 = 10;

    // Re-encrypt path (when cache counter >= 1000) uses:
    //   ROR mix + xor 0x73437527 + add 0x43039258
    constexpr std::uint32_t reenc_add = 0x43039258u;
}

// ── BasePlayer.playerInventory (+0x548) ────────────────────────────────────
namespace PlayerInventory {
    constexpr std::uintptr_t field_offset   = 0x548;
    constexpr std::uintptr_t getter_rva     = 0x29B0320;
    constexpr std::uintptr_t decrypt_rva    = 0x1162810;
    constexpr std::uintptr_t typeinfo_rva   = 0x0FB6B400;

    // Per-dword, 2 iterations:
    //   x = x ^ 0x06562778
    //   x = ROL(x, 6)           // shl 0x06 | shr 0x1A
    //   x = x ^ 0x7EC38BFC
    constexpr std::uint32_t xor1 = 0x06562778u;
    constexpr std::uint32_t xor2 = 0x7EC38BFCu;
    constexpr int rol1 = 6;
}

// Shared IL2CPP helpers seen as callees from both decrypt stubs:
namespace Il2CppHelpers {
    constexpr std::uintptr_t class_init_rva      = 0x07DDA20; // cctor / runtime init
    constexpr std::uintptr_t helper_7EB270       = 0x07EB270;
    constexpr std::uintptr_t gchandle_like_81BE30 = 0x081BE30; // if (handle & 1)
    constexpr std::uintptr_t helper_7EB1F0       = 0x07EB1F0;
    constexpr std::uintptr_t isinst_like_822400  = 0x0822400; // type check
    constexpr std::uintptr_t helper_81BFB0       = 0x081BFB0;
}

// Pseudocode (external):
//   EncryptedValue* ev = rpm<EncryptedValue*>(player + field_offset);
//   if (!ev) return nullptr;
//   uint64_t h = 0;
//   if (rpm<uint8_t>(ev + 0x14)) h = decrypt_u64(rpm<uint64_t>(ev + 0x18));
//   void* obj = resolve_handle(h); // bit0 => gchandle path
//   return isinst(obj, TypeInfo);

inline std::uint32_t rol32(std::uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

inline std::uint64_t decrypt_player_eyes_handle(std::uint64_t raw) {
    std::uint32_t w[2] = {
        static_cast<std::uint32_t>(raw),
        static_cast<std::uint32_t>(raw >> 32)};
    for (int i = 0; i < 2; ++i) {
        std::uint32_t x = w[i] + PlayerEyes::add_const;
        x = rol32(x, PlayerEyes::rol1);
        x ^= PlayerEyes::xor_const;
        x = rol32(x, PlayerEyes::rol2);
        w[i] = x;
    }
    return (static_cast<std::uint64_t>(w[1]) << 32) | w[0];
}

inline std::uint64_t decrypt_player_inventory_handle(std::uint64_t raw) {
    std::uint32_t w[2] = {
        static_cast<std::uint32_t>(raw),
        static_cast<std::uint32_t>(raw >> 32)};
    for (int i = 0; i < 2; ++i) {
        std::uint32_t x = w[i] ^ PlayerInventory::xor1;
        x = rol32(x, PlayerInventory::rol1);
        x ^= PlayerInventory::xor2;
        w[i] = x;
    }
    return (static_cast<std::uint64_t>(w[1]) << 32) | w[0];
}

} // namespace Icky::Rust::Decrypt
