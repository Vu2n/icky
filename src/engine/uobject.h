#pragma once

#include "core/memory.h"
#include "fname.h"
#include <ue_sdk/types.h>
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace ue {

// Lightweight views over remote UE reflection objects

struct PropertyInfo {
    uint64_t    address = 0;
    std::string name;
    std::string type_name;
    int32_t     offset = 0;
    int32_t     size = 0;
    int32_t     array_dim = 1;
    uint64_t    property_flags = 0;
    uint64_t    class_ptr = 0; // UClass/FFieldClass of property
};

struct FunctionInfo {
    uint64_t    address = 0;
    std::string name;
    uint32_t    function_flags = 0;
    uint8_t     num_parms = 0;
    uint16_t    parms_size = 0;
    uint16_t    return_value_offset = 0;
    uint64_t    func = 0;
    std::vector<PropertyInfo> params;
};

struct StructInfo {
    uint64_t    address = 0;
    std::string name;
    std::string full_name;
    std::string super_name;
    uint64_t    super = 0;
    int32_t     size = 0;
    int32_t     min_alignment = 0;
    bool        is_class = false;
    bool        is_function = false;
    std::vector<PropertyInfo> properties;
    std::vector<FunctionInfo> functions;
};

struct EnumInfo {
    uint64_t    address = 0;
    std::string name;
    std::string full_name;
    std::vector<std::pair<std::string, int64_t>> members;
};

struct PackageInfo {
    uint64_t    address = 0;
    std::string name;
    std::vector<StructInfo> structs;
    std::vector<StructInfo> classes;
    std::vector<EnumInfo>   enums;
};

class UObjectReader {
public:
    void configure(const Memory* mem, const ue_offsets* offs, FNameSystem* names);

    bool valid_object(uint64_t obj) const;

    uint64_t get_class(uint64_t obj) const;
    uint64_t get_outer(uint64_t obj) const;
    FNameRaw get_fname(uint64_t obj) const;
    std::string get_name(uint64_t obj) const;
    std::string get_full_name(uint64_t obj) const;

    // Class name of object's UClass
    std::string get_class_name(uint64_t obj) const;

    bool is_a(uint64_t obj, const char* class_name) const;
    bool class_is_child_of(uint64_t uclass, const char* parent_name) const;

    uint64_t get_super(uint64_t ustruct) const;
    int32_t  get_struct_size(uint64_t ustruct) const;

    std::vector<PropertyInfo> read_properties(uint64_t ustruct) const;
    std::vector<FunctionInfo> read_functions(uint64_t uclass) const;

    // Enum: UEnum::Names as TArray<TPair<FName, int64>>
    EnumInfo read_enum(uint64_t uenum) const;

private:
    PropertyInfo read_property_ffield(uint64_t field) const;
    PropertyInfo read_property_uobject(uint64_t prop) const;
    std::string  property_type_name(uint64_t prop_or_field, bool is_ffield) const;

    const Memory*     mem_  = nullptr;
    const ue_offsets* offs_ = nullptr;
    FNameSystem*      names_ = nullptr;
};

} // namespace ue
