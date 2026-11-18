#pragma once
#include <string>

struct Config {
    bool enabled = true;
    bool log = true;
    bool trace = true;  // once-a-second cursor line while a building shape is in hand
    bool stayLockedAfterPlace = true;
    bool flipLeftRight = false;
    bool snapOnLock = false;
    int stepCells = 1;
    int largeStepCells = 4;
    int startDelayMs = 4000;
    int repeatDelayMs = 350;
    int repeatRateMs = 80;

    int keyLock = 'H';
    int keyForward = 0x26;    // Up
    int keyBack = 0x28;       // Down
    int keyLeft = 0x25;       // Left
    int keyRight = 0x27;      // Right
    int keyUp = 0x21;         // PageUp
    int keyDown = 0x22;       // PageDown
    int keyLargeStep = 0xA0;  // LShift

    std::string warnings;  // one line per problem, for the log
};

// Missing file or missing entries leave the defaults above.
Config load_config(const wchar_t* iniPath);
