#ifndef GM_SETTINGS_LOG_H
#define GM_SETTINGS_LOG_H

#include <cstdarg>
#include <cstdio>

// Appends to a fixed commit-log buffer without ever writing past it. A bare
// `used += snprintf(...)` accumulates the would-be length, so once the
// buffer is full `log + used` points past the end and `sizeof(log) - used`
// wraps to a huge size_t; the next snprintf then writes into whatever
// follows the buffer on the stack. Clamping `used` at the capacity keeps
// every later call a no-op on a full buffer.
inline void settingsLogAppend(char *buf, size_t cap, int &used, const char *fmt, ...) {
    if (used < 0 || static_cast<size_t>(used) >= cap - 1) {
        used = static_cast<int>(cap - 1);
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(buf + used, cap - static_cast<size_t>(used), fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    used += n;
    if (static_cast<size_t>(used) >= cap - 1) {
        used = static_cast<int>(cap - 1);
    }
}

#endif // GM_SETTINGS_LOG_H
