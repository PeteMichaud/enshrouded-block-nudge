#include "config.h"

#include <windows.h>

#include "keys.h"

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int ini_int(const wchar_t* path, const wchar_t* section, const wchar_t* key, int fallback) {
    return (int)GetPrivateProfileIntW(section, key, fallback, path);
}

static int ini_key(const wchar_t* path, const wchar_t* key, int fallback, std::string& warnings) {
    wchar_t wide[64];
    const DWORD n = GetPrivateProfileStringW(L"Keys", key, L"", wide, 64, path);
    if (!n) return fallback;
    char narrow[64];
    for (DWORD i = 0; i <= n; i++) narrow[i] = wide[i] < 128 ? (char)wide[i] : '?';
    const int vk = keys::parse(narrow);
    if (vk >= 0) return vk;

    char keyName[32], fallbackName[16];
    WideCharToMultiByte(CP_UTF8, 0, key, -1, keyName, sizeof(keyName), nullptr, nullptr);
    keys::name_of(fallback, fallbackName, sizeof(fallbackName));
    warnings += std::string("ini: [Keys] ") + keyName + "=" + narrow + " is not a key name; using " + fallbackName + "\n";
    return fallback;
}

Config load_config(const wchar_t* path) {
    Config c;
    c.enabled = ini_int(path, L"General", L"Enabled", c.enabled) != 0;
    c.log = ini_int(path, L"General", L"Log", c.log) != 0;
    c.trace = ini_int(path, L"General", L"Trace", c.trace) != 0;
    c.stayLockedAfterPlace = ini_int(path, L"General", L"StayLockedAfterPlace", c.stayLockedAfterPlace) != 0;
    c.flipLeftRight = ini_int(path, L"General", L"FlipLeftRight", c.flipLeftRight) != 0;
    c.snapOnLock = ini_int(path, L"General", L"SnapOnLock", c.snapOnLock) != 0;
    c.stepCells = clamp(ini_int(path, L"General", L"StepCells", c.stepCells), 1, 64);
    c.largeStepCells = clamp(ini_int(path, L"General", L"LargeStepCells", c.largeStepCells), 1, 64);
    c.startDelayMs = clamp(ini_int(path, L"General", L"StartDelayMs", c.startDelayMs), 0, 60000);
    c.repeatDelayMs = clamp(ini_int(path, L"KeyRepeat", L"DelayMs", c.repeatDelayMs), 50, 5000);
    c.repeatRateMs = clamp(ini_int(path, L"KeyRepeat", L"RateMs", c.repeatRateMs), 0, 5000);

    c.keyLock = ini_key(path, L"Lock", c.keyLock, c.warnings);
    c.keyForward = ini_key(path, L"Forward", c.keyForward, c.warnings);
    c.keyBack = ini_key(path, L"Back", c.keyBack, c.warnings);
    c.keyLeft = ini_key(path, L"Left", c.keyLeft, c.warnings);
    c.keyRight = ini_key(path, L"Right", c.keyRight, c.warnings);
    c.keyUp = ini_key(path, L"Up", c.keyUp, c.warnings);
    c.keyDown = ini_key(path, L"Down", c.keyDown, c.warnings);
    c.keyLargeStep = ini_key(path, L"LargeStep", c.keyLargeStep, c.warnings);
    return c;
}
