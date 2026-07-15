#pragma once

#include "export.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Status codes ────────────────────────────────────────────────────────────

typedef enum ue_status {
    UE_OK                    = 0,
    UE_ERR_INVALID_ARG       = 1,
    UE_ERR_NOT_FOUND         = 2,
    UE_ERR_PROCESS           = 3,
    UE_ERR_MEMORY            = 4,
    UE_ERR_PATTERN           = 5,
    UE_ERR_VERSION           = 6,
    UE_ERR_DUMP              = 7,
    UE_ERR_IO                = 8,
    UE_ERR_ALREADY_INIT      = 9,
    UE_ERR_NOT_INIT          = 10,
    UE_ERR_INTERNAL          = 99
} ue_status;

// ── Engine family / version ─────────────────────────────────────────────────

typedef enum ue_engine_family {
    UE_FAMILY_UNKNOWN = 0,
    UE_FAMILY_UE4     = 4,
    UE_FAMILY_UE5     = 5
} ue_engine_family;

typedef struct ue_version {
    int major;   // 4 or 5
    int minor;   // e.g. 27, 3
    int patch;   // e.g. 2
} ue_version;

// ── Log levels ──────────────────────────────────────────────────────────────

typedef enum ue_log_level {
    UE_LOG_TRACE = 0,
    UE_LOG_DEBUG = 1,
    UE_LOG_INFO  = 2,
    UE_LOG_WARN  = 3,
    UE_LOG_ERROR = 4,
    UE_LOG_OFF   = 5
} ue_log_level;

typedef void (UE_SDK_CALL *ue_log_callback)(ue_log_level level, const char* message, void* user);

// ── Memory backend ──────────────────────────────────────────────────────────
// Allow callers to plug in their own read (inject, emulator, remote, etc.)

typedef int  (UE_SDK_CALL *ue_read_fn)(void* ctx, uint64_t address, void* buffer, size_t size);
typedef int  (UE_SDK_CALL *ue_write_fn)(void* ctx, uint64_t address, const void* buffer, size_t size);
typedef uint64_t (UE_SDK_CALL *ue_module_base_fn)(void* ctx, const char* module_name);
typedef size_t (UE_SDK_CALL *ue_module_size_fn)(void* ctx, const char* module_name);

typedef struct ue_memory_backend {
    void*              user;
    ue_read_fn         read;
    ue_write_fn        write;          // optional (may be NULL)
    ue_module_base_fn  module_base;
    ue_module_size_fn  module_size;
} ue_memory_backend;

// ── Known global addresses (fill what you know; 0 = auto-scan) ──────────────

typedef struct ue_globals {
    uint64_t gobjects;       // FUObjectArray* / TUObjectArray*
    uint64_t gnames;         // FNamePool* / TNameEntryArray*
    uint64_t gworld;         // UWorld** (optional)
    uint64_t process_event;  // UObject::ProcessEvent (optional)
    uint64_t append_string;  // FName::AppendString (optional)
} ue_globals;

// ── Offset profile for one engine build ─────────────────────────────────────

typedef struct ue_offsets {
    // UObject
    int32_t uobject_vtable;
    int32_t uobject_class;
    int32_t uobject_name;
    int32_t uobject_outer;
    int32_t uobject_internal_index;
    int32_t uobject_flags;

    // UStruct / UClass
    int32_t ustruct_super;
    int32_t ustruct_children;
    int32_t ustruct_child_properties;  // UE4.25+ / UE5 FField chain
    int32_t ustruct_size;
    int32_t ustruct_min_alignment;
    int32_t uclass_cast_flags;
    int32_t uclass_default_object;
    int32_t uclass_functions;          // linked list head (varies)

    // UFunction
    int32_t ufunction_function_flags;
    int32_t ufunction_num_parms;
    int32_t ufunction_parms_size;
    int32_t ufunction_return_value_offset;
    int32_t ufunction_func;            // native func ptr

    // UProperty / FProperty (layout differs pre/post FField)
    int32_t property_array_dim;
    int32_t property_element_size;
    int32_t property_property_flags;
    int32_t property_offset;
    int32_t property_size;             // sizeof(UProperty/FProperty) base

    // FField (UE4.25+)
    int32_t ffield_class;
    int32_t ffield_owner;
    int32_t ffield_next;
    int32_t ffield_name;
    int32_t ffield_flags;

    // FName
    int32_t fname_comparison_index;
    int32_t fname_number;
    int32_t fname_display_index;       // UE5 may use display index
    int32_t fname_size;

    // FUObjectArray / TUObjectArray
    int32_t fuobjectarray_objects;
    int32_t fuobjectarray_num_elements;
    int32_t fuobjectarray_max_elements;
    int32_t fuobjectarray_num_chunks;  // chunked layout
    int32_t fuobjectitem_size;
    int32_t fuobjectitem_object;

    // FNamePool (UE4.23+ pool) / TNameEntryArray
    int32_t fnamepool_blocks;
    int32_t fnamepool_current_block;
    int32_t fnamepool_current_byte_cursor;
    int32_t fnameentry_header_size;
    uint16_t fnameentry_len_bit_offset;
    uint16_t fnameentry_len_bit_count;
    int uses_name_pool;                // 1 = FNamePool, 0 = TNameEntryArray
    int uses_ffield;                   // 1 = FProperty, 0 = UProperty
    int objects_chunked;               // 1 = chunked TUObjectArray
    int32_t objects_elements_per_chunk;
} ue_offsets;

// ── Dump / generate options ─────────────────────────────────────────────────

typedef enum ue_output_format {
    UE_FMT_CPP_HEADERS   = 1 << 0,
    UE_FMT_JSON          = 1 << 1,
    UE_FMT_OFFSETS_H     = 1 << 2,
    UE_FMT_ALL           = 0xFFFF
} ue_output_format;

typedef struct ue_dump_options {
    const char*        output_dir;       // required
    uint32_t           formats;          // ue_output_format flags
    int                generate_packages;
    int                generate_structs;
    int                generate_classes;
    int                generate_enums;
    int                generate_functions;
    int                one_header_per_package;
    int                include_padding;
    int                filter_engine_only; // skip game packages if 1
    const char*        package_filter;   // optional comma list; NULL = all
    int                max_objects;      // 0 = unlimited
} ue_dump_options;

// ── Stats after a dump ──────────────────────────────────────────────────────

typedef struct ue_dump_stats {
    uint64_t objects_scanned;
    uint64_t packages;
    uint64_t classes;
    uint64_t structs;
    uint64_t enums;
    uint64_t functions;
    uint64_t properties;
    uint64_t files_written;
    double   elapsed_seconds;
} ue_dump_stats;

// Opaque session handle
typedef struct ue_session ue_session;

#ifdef __cplusplus
}
#endif
