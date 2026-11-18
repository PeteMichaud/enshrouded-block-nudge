#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <string>

static CRITICAL_SECTION g_lock;
static HANDLE g_file = INVALID_HANDLE_VALUE;

void log_open(const wchar_t* dir, bool enabled) {
    if (!enabled || g_file != INVALID_HANDLE_VALUE) return;
    InitializeCriticalSection(&g_lock);
    const std::wstring path = std::wstring(dir) + L"BlockNudge.log";
    const std::wstring prev = std::wstring(dir) + L"BlockNudge.prev.log";
    MoveFileExW(path.c_str(), prev.c_str(), MOVEFILE_REPLACE_EXISTING);
    g_file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void logf(const char* fmt, ...) {
    if (g_file == INVALID_HANDLE_VALUE) return;
    char line[1024];
    SYSTEMTIME t;
    GetLocalTime(&t);
    int n = snprintf(line, sizeof(line), "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    const int m = vsnprintf(line + n, sizeof(line) - n - 2, fmt, args);
    va_end(args);
    if (m > 0) n += m < (int)sizeof(line) - n - 2 ? m : (int)sizeof(line) - n - 3;
    line[n++] = '\r';
    line[n++] = '\n';
    EnterCriticalSection(&g_lock);
    DWORD written = 0;
    WriteFile(g_file, line, (DWORD)n, &written, nullptr);
    LeaveCriticalSection(&g_lock);
}
