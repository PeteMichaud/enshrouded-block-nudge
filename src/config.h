#pragma once
#include <string>

struct Config {
    bool enabled = true;
    bool log = true;
    bool trace = false;  // once-a-second cursor lines while a building shape is in hand
    bool flipLeftRight = false;
    bool snapOnLock = false;
    int stepCells = 1;
    int largeStepCells = 4;
    int startDelayMs = 4000;
    int repeatDelayMs = 350;
    int repeatRateMs = 80;
    // One bit per cursor mode (query[3]) that the lock holds. 3 and 4: see shared.h.
    unsigned cursorModes = 0x18;

    int keyLock = 'H';
    int keyForward = 0x26;    // Up
    int keyBack = 0x28;       // Down
    int keyLeft = 0x25;       // Left
    int keyRight = 0x27;      // Right
    int keyUp = 0x24;         // Home (PageUp and PageDown are the game's camera zoom)
    int keyDown = 0x23;       // End
    int keyLargeStep = 0xA0;  // LShift

    std::string warnings;  // one line per problem, for the log
};

// Missing file or missing entries leave the defaults above.
Config load_config(const wchar_t* iniPath);
