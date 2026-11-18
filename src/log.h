#pragma once

// Opens BlockNudge.log in dir (which ends with a backslash), keeping the previous
// session as BlockNudge.prev.log. With enabled false, logf does nothing.
void log_open(const wchar_t* dir, bool enabled);

// One timestamped line. Does file I/O: never call it from the cursor hook.
void logf(const char* fmt, ...)
#if defined(__MINGW32__)
    __attribute__((format(gnu_printf, 1, 2)))
#elif defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
