#include "logger.h"
#include <cstdio>
#include <cstdarg>
#include <mutex>

namespace icky {
namespace {
std::mutex g_mu;
icky_log_level g_level = ICKY_LOG_INFO;
icky_log_fn g_fn = nullptr;
void* g_user = nullptr;

const char* lvl(icky_log_level l) {
    switch (l) {
    case ICKY_LOG_TRACE: return "TRACE";
    case ICKY_LOG_DEBUG: return "DEBUG";
    case ICKY_LOG_INFO:  return "INFO";
    case ICKY_LOG_WARN:  return "WARN";
    case ICKY_LOG_ERROR: return "ERROR";
    default: return "OFF";
    }
}
} // namespace

void set_log_level(icky_log_level level) {
    std::lock_guard lock(g_mu);
    g_level = level;
}

void set_log_callback(icky_log_fn fn, void* user) {
    std::lock_guard lock(g_mu);
    g_fn = fn;
    g_user = user;
}

void log(icky_log_level level, const char* fmt, ...) {
    icky_log_fn fn;
    void* user;
    {
        std::lock_guard lock(g_mu);
        if (level < g_level || g_level == ICKY_LOG_OFF) return;
        fn = g_fn;
        user = g_user;
    }
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    if (fn) {
        fn(level, buf, user);
        return;
    }
    std::fprintf(stdout, "[icky][%s] %s\n", lvl(level), buf);
    std::fflush(stdout);
}

} // namespace icky
