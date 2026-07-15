#include "logger.h"
#include "modules.h"

#include <cstdio>
#include <cstdarg>
#include <Windows.h>

namespace icky {
namespace {

// No std::mutex — static mutex init / lock has crashed under inject hosts
volatile long g_level = ICKY_LOG_INFO;
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

void write_line(const char* line) {
    OutputDebugStringA(line);
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE) {
        DWORD mode = 0, w = 0;
        if (GetConsoleMode(h, &mode)) {
            WriteConsoleA(h, line, (DWORD)lstrlenA(line), &w, nullptr);
            return;
        }
    }
    fputs(line, stdout);
    fflush(stdout);
}

} // namespace

void set_log_level(icky_log_level level) {
    InterlockedExchange(&g_level, (long)level);
}

void set_log_callback(icky_log_fn fn, void* user) {
    // Intentionally no lock; callback rarely changed
    g_fn = fn;
    g_user = user;
}

void log(icky_log_level level, const char* fmt, ...) {
    if ((long)level < g_level || g_level == ICKY_LOG_OFF)
        return;

    char body[4000];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    body[sizeof(body) - 1] = 0;

    if (g_fn) {
        g_fn(level, body, g_user);
        return;
    }

    char line[4200];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[icky][%s] %s\r\n", lvl(level), body);
    write_line(line);
}

} // namespace icky
