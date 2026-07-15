#pragma once

#include <icky/types.h>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace icky {

// Unified intermediate representation for all engines.
// Generators turn this into Internal or External SDKs.

enum class TypeKind {
    Class,
    Struct,
    Enum,
    Interface,
    Namespace
};

struct SdkField {
    std::string name;
    std::string type_name;
    int32_t     offset     = 0;
    int32_t     size       = 0;
    int32_t     array_dim  = 1;
    bool        is_static  = false;
    bool        is_pointer = false;
    uint64_t    flags      = 0;
    std::string comment;
};

struct SdkMethodParam {
    std::string name;
    std::string type_name;
};

struct SdkMethod {
    std::string name;
    std::string return_type;
    std::vector<SdkMethodParam> params;
    uint64_t    rva        = 0;   // relative to primary module
    uint64_t    address    = 0;   // absolute (internal)
    uint32_t    flags      = 0;
    bool        is_static  = false;
    std::string comment;
};

struct SdkEnumMember {
    std::string name;
    int64_t     value = 0;
};

struct SdkType {
    TypeKind    kind = TypeKind::Class;
    std::string name;
    std::string full_name;
    std::string parent;          // super class / base
    std::string ns;              // namespace / package / assembly
    int32_t     size = 0;
    uint64_t    address = 0;     // type object / Il2CppClass* / etc.
    uint64_t    rva = 0;
    std::vector<SdkField>       fields;
    std::vector<SdkMethod>      methods;
    std::vector<SdkEnumMember>  enum_members;
    std::string comment;
};

struct SdkGlobal {
    std::string name;
    uint64_t    address = 0;
    uint64_t    rva     = 0;
    std::string type_name;
    std::string comment;
};

struct SdkModule {
    std::string name;
    uint64_t    base = 0;
    size_t      size = 0;
};

struct SdkModel {
    icky_engine engine = ICKY_ENGINE_UNKNOWN;
    std::string engine_detail;   // e.g. "UE 4.27", "IL2CPP 29", "Source2"
    std::string game_name;
    SdkModule   primary_module;
    std::vector<SdkModule> modules;
    std::vector<SdkType>   types;
    std::vector<SdkGlobal> globals;
    std::unordered_map<std::string, std::string> metadata; // free-form notes
    // Decrypted string samples (name -> plaintext) for verification
    std::vector<std::pair<std::string, std::string>> decrypted_strings;
};

} // namespace icky
