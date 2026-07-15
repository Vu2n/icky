#pragma once

#include "export.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum icky_status {
    ICKY_OK              = 0,
    ICKY_ERR_INVALID     = 1,
    ICKY_ERR_DETECT      = 2,
    ICKY_ERR_DUMP        = 3,
    ICKY_ERR_IO          = 4,
    ICKY_ERR_CANCELLED   = 5,
    ICKY_ERR_INTERNAL    = 99
} icky_status;

typedef enum icky_engine {
    ICKY_ENGINE_UNKNOWN  = 0,
    ICKY_ENGINE_UNREAL   = 1,
    ICKY_ENGINE_IL2CPP   = 2,
    ICKY_ENGINE_MONO     = 3,
    ICKY_ENGINE_SOURCE1  = 4,  // CS:GO / classic Source
    ICKY_ENGINE_SOURCE2  = 5   // Deadlock / CS2 / Source 2
} icky_engine;

typedef enum icky_sdk_mode {
    ICKY_SDK_INTERNAL = 1,  // in-process: absolute ptrs, call helpers
    ICKY_SDK_EXTERNAL = 2   // out-of-process: RVAs + RPM-friendly layouts
} icky_sdk_mode;

typedef enum icky_log_level {
    ICKY_LOG_TRACE = 0,
    ICKY_LOG_DEBUG = 1,
    ICKY_LOG_INFO  = 2,
    ICKY_LOG_WARN  = 3,
    ICKY_LOG_ERROR = 4,
    ICKY_LOG_OFF   = 5
} icky_log_level;

typedef void (ICKY_CALL *icky_log_fn)(icky_log_level level, const char* msg, void* user);

#ifdef __cplusplus
}
#endif
