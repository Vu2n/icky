#pragma once

#include <icky/types.h>

namespace icky {

void set_log_level(icky_log_level level);
void set_log_callback(icky_log_fn fn, void* user);
void log(icky_log_level level, const char* fmt, ...);

#define ILOG_T(...) ::icky::log(ICKY_LOG_TRACE, __VA_ARGS__)
#define ILOG_D(...) ::icky::log(ICKY_LOG_DEBUG, __VA_ARGS__)
#define ILOG_I(...) ::icky::log(ICKY_LOG_INFO,  __VA_ARGS__)
#define ILOG_W(...) ::icky::log(ICKY_LOG_WARN,  __VA_ARGS__)
#define ILOG_E(...) ::icky::log(ICKY_LOG_ERROR, __VA_ARGS__)

} // namespace icky
