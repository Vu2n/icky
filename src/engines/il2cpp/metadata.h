#pragma once

#include "model/sdk_model.h"
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace icky::il2cpp {

// Subset of global-metadata.dat structures (Unity IL2CPP)
#pragma pack(push, 1)
struct Il2CppGlobalMetadataHeader {
    int32_t sanity;          // 0xFAB11BAF when unencrypted
    int32_t version;
    int32_t stringLiteralOffset;
    int32_t stringLiteralSize;
    int32_t stringLiteralDataOffset;
    int32_t stringLiteralDataSize;
    int32_t stringOffset;
    int32_t stringSize;
    int32_t eventsOffset;
    int32_t eventsSize;
    int32_t propertiesOffset;
    int32_t propertiesSize;
    int32_t methodsOffset;
    int32_t methodsSize;
    int32_t parameterDefaultValuesOffset;
    int32_t parameterDefaultValuesSize;
    int32_t fieldDefaultValuesOffset;
    int32_t fieldDefaultValuesSize;
    int32_t fieldAndParameterDefaultValueDataOffset;
    int32_t fieldAndParameterDefaultValueDataSize;
    int32_t fieldMarshaledSizesOffset;
    int32_t fieldMarshaledSizesSize;
    int32_t parametersOffset;
    int32_t parametersSize;
    int32_t fieldsOffset;
    int32_t fieldsSize;
    int32_t genericParametersOffset;
    int32_t genericParametersSize;
    int32_t genericParameterConstraintsOffset;
    int32_t genericParameterConstraintsSize;
    int32_t genericContainersOffset;
    int32_t genericContainersSize;
    int32_t nestedTypesOffset;
    int32_t nestedTypesSize;
    int32_t interfacesOffset;
    int32_t interfacesSize;
    int32_t vtableMethodsOffset;
    int32_t vtableMethodsSize;
    int32_t interfaceOffsetsOffset;
    int32_t interfaceOffsetsSize;
    int32_t typeDefinitionsOffset;
    int32_t typeDefinitionsSize;
    // more fields omitted — version dependent
};
#pragma pack(pop)

struct MetadataBlob {
    std::vector<uint8_t> data;
    int32_t version = 0;
    bool    was_encrypted = false;
    std::string path;
};

// Load metadata from GameAssembly path sibling Data/il2cpp_data/Metadata/global-metadata.dat
// or search common Unity paths.
std::optional<MetadataBlob> load_global_metadata();

// Attempt automatic decryption of common XOR / rolling schemes
bool try_decrypt_metadata(MetadataBlob& blob);

// Extract string from metadata string heap (offset relative to string section)
std::string meta_string(const MetadataBlob& blob, int32_t offset);

// Build SdkModel types from metadata + optional GameAssembly exports
bool dump_from_metadata(const MetadataBlob& blob, uint64_t game_assembly_base,
                        size_t game_assembly_size, SdkModel& out);

} // namespace icky::il2cpp
