// BlockNudge's entry point. Installed as POWRPROF.dll (exports forwarded in
// powrprof_exports.cpp) or as dinput8.dll, where DirectInput8Create below is passed to the real
// one. Also works under any other name when another loader loads it, since everything starts
// from DllMain.

#include <windows.h>

#include <string>

#include "config.h"
#include "hook.h"
#include "input.h"
#include "keys.h"
#include "log.h"
#include "shared.h"

typedef HRESULT(WINAPI* DirectInput8CreateFn)(HINSTANCE, DWORD, const void*, void**, void*);
static DirectInput8CreateFn g_realCreate = nullptr;

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version, const void* riid, void** out,
                                             void* outer) {
    if (!g_realCreate) {
        // The full System32 path: a bare name would find this proxy again.
        wchar_t path[MAX_PATH];
        const UINT n = GetSystemDirectoryW(path, MAX_PATH);
        if (n == 0 || n > MAX_PATH - 16) return E_FAIL;
        lstrcatW(path, L"\\dinput8.dll");
        const HMODULE real = LoadLibraryW(path);
        if (real) g_realCreate = (DirectInput8CreateFn)(void*)GetProcAddress(real, "DirectInput8Create");
        if (!g_realCreate) return E_FAIL;
    }
    return g_realCreate(instance, version, riid, out, outer);
}

static HMODULE g_self = nullptr;

static std::string utf8(const wchar_t* wide) {
    char narrow[MAX_PATH * 3];
    if (!WideCharToMultiByte(CP_UTF8, 0, wide, -1, narrow, sizeof(narrow), nullptr, nullptr)) return "?";
    return narrow;
}

static void log_keys(const Config& c) {
    const struct {
        const char* name;
        int vk;
    } all[] = {
        {"Lock", c.keyLock}, {"Forward", c.keyForward}, {"Back", c.keyBack}, {"Left", c.keyLeft},
        {"Right", c.keyRight}, {"Up", c.keyUp}, {"Down", c.keyDown}, {"LargeStep", c.keyLargeStep},
    };
    std::string line = "keys:";
    for (const auto& k : all) {
        char name[16];
        keys::name_of(k.vk, name, sizeof(name));
        line += std::string(" ") + k.name + "=" + name;
    }
    logf("%s", line.c_str());
}

static DWORD WINAPI init_thread(void*) {
    wchar_t exe[MAX_PATH], self[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return 0;
    if (!GetModuleFileNameW(g_self, self, MAX_PATH)) self[0] = 0;

    // The ini and the log live next to the game's exe, wherever this DLL was loaded from.
    std::wstring dir = exe;
    const size_t slash = dir.find_last_of(L'\\');
    const std::wstring exeName = slash == std::wstring::npos ? dir : dir.substr(slash + 1);
    dir = slash == std::wstring::npos ? L"" : dir.substr(0, slash + 1);

    const Config config = load_config((dir + L"BlockNudge.ini").c_str());
    log_open(dir.c_str(), config.log);

    logf("BlockNudge " BLOCKNUDGE_VERSION " loaded as %s", utf8(self).c_str());
    logf("game: %s, module base %p", utf8(exe).c_str(), (void*)GetModuleHandleW(nullptr));
    for (size_t start = 0; start < config.warnings.size();) {
        const size_t end = config.warnings.find('\n', start);
        logf("%s", config.warnings.substr(start, end - start).c_str());
        start = end + 1;
    }
    logf("settings: Enabled=%d StepCells=%d LargeStepCells=%d FlipLeftRight=%d SnapOnLock=%d "
         "Trace=%d KeyRepeat=%d/%d ms CursorModes=0x%x",
         config.enabled, config.stepCells, config.largeStepCells, config.flipLeftRight,
         config.snapOnLock, config.trace, config.repeatDelayMs, config.repeatRateMs, config.cursorModes);
    log_keys(config);

    if (!config.enabled) {
        logf("Enabled=0: nothing installed, game left vanilla");
        return 0;
    }
    if (lstrcmpiW(exeName.c_str(), L"enshrouded_server.exe") == 0) {
        logf("this is the dedicated server, which has no cursor; nothing installed");
        return 0;
    }

    // The Steam stub decrypts the game's code during start-up; scanning before that finds nothing.
    logf("waiting %d ms for game start-up before scanning", config.startDelayMs);
    Sleep((DWORD)config.startDelayMs);

    g_shared.lockModes.store(config.cursorModes);
    if (hook_install()) input_start(config);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;

    // Two loaders (the root dinput8.dll and a chain entry, say) must not hook twice. The
    // marker holds the process id because child processes inherit the environment.
    wchar_t pid[16], marker[16];
    int digits = 0;
    for (DWORD v = GetCurrentProcessId(); digits < 15; v /= 10) {
        pid[digits++] = (wchar_t)(L'0' + v % 10);
        if (v < 10) break;
    }
    pid[digits] = 0;
    const DWORD n = GetEnvironmentVariableW(L"BLOCKNUDGE_LOADED", marker, 16);
    if (n > 0 && n < 16 && lstrcmpW(marker, pid) == 0) return TRUE;
    SetEnvironmentVariableW(L"BLOCKNUDGE_LOADED", pid);

    g_self = self;
    // Nothing else here: the loader lock is held. The thread starts once it is released.
    const HANDLE thread = CreateThread(nullptr, 0, init_thread, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
    return TRUE;
}
