#include "uobject.h"
#include "core/logger.h"

#include <unordered_set>

namespace ue {

void UObjectReader::configure(const Memory* mem, const ue_offsets* offs, FNameSystem* names) {
    mem_   = mem;
    offs_  = offs;
    names_ = names;
}

bool UObjectReader::valid_object(uint64_t obj) const {
    if (!mem_ || !offs_ || obj < 0x10000 || obj > 0x00007FFFFFFFFFFFULL)
        return false;
    // vtable pointer sanity
    uint64_t vt = 0;
    if (!mem_->read(obj + static_cast<uint64_t>(offs_->uobject_vtable), vt))
        return false;
    if (vt < 0x10000)
        return false;
    uint64_t cls = get_class(obj);
    return cls != 0;
}

uint64_t UObjectReader::get_class(uint64_t obj) const {
    uint64_t c = 0;
    mem_->read(obj + static_cast<uint64_t>(offs_->uobject_class), c);
    if (c < 0x10000 || c > 0x00007FFFFFFFFFFFULL)
        return 0;
    return c;
}

uint64_t UObjectReader::get_outer(uint64_t obj) const {
    uint64_t o = 0;
    mem_->read(obj + static_cast<uint64_t>(offs_->uobject_outer), o);
    if (o && (o < 0x10000 || o > 0x00007FFFFFFFFFFFULL))
        return 0;
    return o;
}

FNameRaw UObjectReader::get_fname(uint64_t obj) const {
    FNameRaw n{};
    const uint64_t addr = obj + static_cast<uint64_t>(offs_->uobject_name);
    mem_->read(addr + static_cast<uint64_t>(offs_->fname_comparison_index), n.comparison_index);
    mem_->read(addr + static_cast<uint64_t>(offs_->fname_number), n.number);
    return n;
}

std::string UObjectReader::get_name(uint64_t obj) const {
    if (!names_)
        return {};
    const FNameRaw n = get_fname(obj);
    return names_->get(n.comparison_index, n.number);
}

std::string UObjectReader::get_full_name(uint64_t obj) const {
    if (!obj)
        return {};
    std::string cls = get_class_name(obj);
    std::string path;
    uint64_t cur = obj;
    std::unordered_set<uint64_t> seen;
    // Outer chain: Package.Sub.Object
    std::vector<std::string> parts;
    while (cur && seen.insert(cur).second) {
        parts.push_back(get_name(cur));
        cur = get_outer(cur);
        if (parts.size() > 64)
            break;
    }
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        if (!path.empty())
            path += '.';
        path += *it;
    }
    if (cls.empty())
        return path;
    return cls + " " + path;
}

std::string UObjectReader::get_class_name(uint64_t obj) const {
    const uint64_t cls = get_class(obj);
    if (!cls)
        return {};
    return get_name(cls);
}

bool UObjectReader::is_a(uint64_t obj, const char* class_name) const {
    if (!obj || !class_name)
        return false;
    return class_is_child_of(get_class(obj), class_name);
}

bool UObjectReader::class_is_child_of(uint64_t uclass, const char* parent_name) const {
    if (!uclass || !parent_name)
        return false;
    std::unordered_set<uint64_t> seen;
    uint64_t cur = uclass;
    while (cur && seen.insert(cur).second) {
        if (get_name(cur) == parent_name)
            return true;
        cur = get_super(cur);
        if (seen.size() > 256)
            break;
    }
    return false;
}

uint64_t UObjectReader::get_super(uint64_t ustruct) const {
    uint64_t s = 0;
    mem_->read(ustruct + static_cast<uint64_t>(offs_->ustruct_super), s);
    if (s && (s < 0x10000 || s > 0x00007FFFFFFFFFFFULL))
        return 0;
    return s;
}

int32_t UObjectReader::get_struct_size(uint64_t ustruct) const {
    int32_t sz = 0;
    mem_->read(ustruct + static_cast<uint64_t>(offs_->ustruct_size), sz);
    if (sz < 0 || sz > 0x1000000)
        return 0;
    return sz;
}

std::string UObjectReader::property_type_name(uint64_t prop_or_field, bool is_ffield) const {
    if (is_ffield) {
        // FField::ClassPrivate -> FFieldClass::Name
        uint64_t fclass = 0;
        mem_->read(prop_or_field + static_cast<uint64_t>(offs_->ffield_class), fclass);
        if (!fclass)
            return "FProperty";
        // FFieldClass name often at +0x00 as FName
        int32_t idx = 0, num = 0;
        mem_->read(fclass + 0x00, idx);
        mem_->read(fclass + 0x04, num);
        if (names_) {
            auto n = names_->get(idx, num);
            if (!n.empty())
                return n;
        }
        return "FProperty";
    }
    // UObject property: Class name
    return get_class_name(prop_or_field);
}

PropertyInfo UObjectReader::read_property_ffield(uint64_t field) const {
    PropertyInfo p;
    p.address = field;
    int32_t idx = 0, num = 0;
    mem_->read(field + static_cast<uint64_t>(offs_->ffield_name) + 0, idx);
    mem_->read(field + static_cast<uint64_t>(offs_->ffield_name) + 4, num);
    if (names_)
        p.name = names_->get(idx, num);
    p.type_name = property_type_name(field, true);
    mem_->read(field + static_cast<uint64_t>(offs_->property_offset), p.offset);
    mem_->read(field + static_cast<uint64_t>(offs_->property_element_size), p.size);
    mem_->read(field + static_cast<uint64_t>(offs_->property_array_dim), p.array_dim);
    mem_->read(field + static_cast<uint64_t>(offs_->property_property_flags), p.property_flags);
    mem_->read(field + static_cast<uint64_t>(offs_->ffield_class), p.class_ptr);
    return p;
}

PropertyInfo UObjectReader::read_property_uobject(uint64_t prop) const {
    PropertyInfo p;
    p.address = prop;
    p.name = get_name(prop);
    p.type_name = get_class_name(prop);
    mem_->read(prop + static_cast<uint64_t>(offs_->property_offset), p.offset);
    mem_->read(prop + static_cast<uint64_t>(offs_->property_element_size), p.size);
    mem_->read(prop + static_cast<uint64_t>(offs_->property_array_dim), p.array_dim);
    mem_->read(prop + static_cast<uint64_t>(offs_->property_property_flags), p.property_flags);
    p.class_ptr = get_class(prop);
    return p;
}

std::vector<PropertyInfo> UObjectReader::read_properties(uint64_t ustruct) const {
    std::vector<PropertyInfo> out;
    if (!ustruct || !mem_ || !offs_)
        return out;

    if (offs_->uses_ffield && offs_->ustruct_child_properties) {
        uint64_t field = 0;
        mem_->read(ustruct + static_cast<uint64_t>(offs_->ustruct_child_properties), field);
        std::unordered_set<uint64_t> seen;
        while (field && seen.insert(field).second) {
            auto prop = read_property_ffield(field);
            if (!prop.name.empty() && prop.name != "None")
                out.push_back(std::move(prop));
            uint64_t next = 0;
            mem_->read(field + static_cast<uint64_t>(offs_->ffield_next), next);
            field = next;
            if (out.size() > 4096)
                break;
        }
        return out;
    }

    // Pre-FField: UField children linked list (UProperty)
    uint64_t child = 0;
    mem_->read(ustruct + static_cast<uint64_t>(offs_->ustruct_children), child);
    std::unordered_set<uint64_t> seen;
    while (child && seen.insert(child).second) {
        // Only properties (skip UFunction etc. by class name)
        const std::string cn = get_class_name(child);
        if (cn.find("Property") != std::string::npos) {
            auto prop = read_property_uobject(child);
            if (!prop.name.empty() && prop.name != "None")
                out.push_back(std::move(prop));
        }
        // UField::Next typically at 0x28
        uint64_t next = 0;
        mem_->read(child + 0x28, next);
        child = next;
        if (out.size() > 4096)
            break;
    }
    return out;
}

std::vector<FunctionInfo> UObjectReader::read_functions(uint64_t uclass) const {
    std::vector<FunctionInfo> out;
    if (!uclass)
        return out;

    // Walk Children for UFunction (both FField-era and older keep UField children for funcs)
    uint64_t child = 0;
    mem_->read(uclass + static_cast<uint64_t>(offs_->ustruct_children), child);
    std::unordered_set<uint64_t> seen;
    while (child && seen.insert(child).second) {
        const std::string cn = get_class_name(child);
        if (cn == "Function" || cn == "DelegateFunction" || cn == "SparseDelegateFunction") {
            FunctionInfo f;
            f.address = child;
            f.name = get_name(child);
            mem_->read(child + static_cast<uint64_t>(offs_->ufunction_function_flags), f.function_flags);
            mem_->read(child + static_cast<uint64_t>(offs_->ufunction_num_parms), f.num_parms);
            mem_->read(child + static_cast<uint64_t>(offs_->ufunction_parms_size), f.parms_size);
            mem_->read(child + static_cast<uint64_t>(offs_->ufunction_return_value_offset), f.return_value_offset);
            mem_->read(child + static_cast<uint64_t>(offs_->ufunction_func), f.func);
            f.params = read_properties(child);
            if (!f.name.empty())
                out.push_back(std::move(f));
        }
        uint64_t next = 0;
        mem_->read(child + 0x28, next);
        child = next;
        if (out.size() > 8192)
            break;
    }
    return out;
}

EnumInfo UObjectReader::read_enum(uint64_t uenum) const {
    EnumInfo e;
    e.address = uenum;
    e.name = get_name(uenum);
    e.full_name = get_full_name(uenum);

    // UEnum::Names is typically a TArray at a version-dependent offset.
    // Common: 0x40 (UE4) / 0x40-0x50. Try a small set of candidates.
    const int32_t candidates[] = {0x40, 0x48, 0x50, 0x30};
    for (int32_t off : candidates) {
        uint64_t data = 0;
        int32_t num = 0, max = 0;
        if (!mem_->read(uenum + static_cast<uint64_t>(off), data))
            continue;
        if (!mem_->read(uenum + static_cast<uint64_t>(off) + 8, num))
            continue;
        if (!mem_->read(uenum + static_cast<uint64_t>(off) + 12, max))
            continue;
        if (!data || num <= 0 || num > 4096 || max < num)
            continue;

        // TPair<FName, int64> size often 0x10
        bool ok = true;
        std::vector<std::pair<std::string, int64_t>> members;
        for (int32_t i = 0; i < num; ++i) {
            const uint64_t pair = data + static_cast<uint64_t>(i) * 0x10;
            int32_t idx = 0, number = 0;
            int64_t value = 0;
            if (!mem_->read(pair + 0, idx) || !mem_->read(pair + 4, number) ||
                !mem_->read(pair + 8, value)) {
                ok = false;
                break;
            }
            std::string n = names_ ? names_->get(idx, number) : std::string{};
            if (n.empty()) {
                ok = false;
                break;
            }
            members.emplace_back(std::move(n), value);
        }
        if (ok && !members.empty()) {
            e.members = std::move(members);
            break;
        }
    }
    return e;
}

} // namespace ue
