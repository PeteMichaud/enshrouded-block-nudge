// State shared between the cursor hook (game threads) and the input thread.
// The hook never waits on either side.
#pragma once
#include <atomic>
#include <cstdint>

#include "nudge.h"

// One writer thread, any number of readers. A reader that collides with a write gives up
// instead of waiting.
template <class T>
class Published {
public:
    void write(const T& v) {
        seq_.fetch_add(1);
        value_ = v;
        seq_.fetch_add(1);
    }

    bool read(T& out) const {
        for (int tries = 0; tries < 64; tries++) {
            const uint32_t before = seq_.load();
            if (before & 1) continue;
            out = value_;
            if (seq_.load() == before) return true;
        }
        return false;
    }

private:
    std::atomic<uint32_t> seq_{0};
    T value_{};
};

// Any number of writers that skip their update when the box is busy, and readers that
// wait for it.
template <class T>
class TryBox {
public:
    template <class F>
    bool try_update(F&& f) {
        if (busy_.test_and_set(std::memory_order_acquire)) return false;
        f(value_);
        busy_.clear(std::memory_order_release);
        return true;
    }

    template <class Relax>
    T read(Relax&& relax) {
        while (busy_.test_and_set(std::memory_order_acquire)) relax();
        T copy = value_;
        busy_.clear(std::memory_order_release);
        return copy;
    }

private:
    std::atomic_flag busy_ = ATOMIC_FLAG_INIT;
    T value_{};
};

// The game calls the cursor function for several cursors a frame, told apart by the mode in
// query[3]. With a block in hand the installed game (build 26430625) runs mode 3 every frame
// and mode 4 only some of the time, so each mode is tracked and locked on its own.
constexpr int kModes = 8;

// What the hook saw on its last call in one mode.
struct ModeSeen {
    nudge::Pose raw{};  // the game's own output, before any override
    bool rawValid = false;
    nudge::Pose lastValid{};
    bool haveValid = false;
    uint64_t validMs = 0;
    uint64_t seenMs = 0;
    int64_t cam[3]{};
    uint8_t query[8]{};
    uint8_t rayType = 255;
    uint32_t callerRva = 0;  // where in the game the call came from
    uintptr_t sys = 0;
};

struct Seen {
    nudge::Cursor cur;  // the lockable modes together: newest valid pose, newest call
    ModeSeen modes[kModes];
};

// What the hook applies. The offset is shared; each mode is held at the pose it had itself
// when the lock was taken, and a mode with no pose then is left to the game.
struct HookLock {
    nudge::Lock lock;
    nudge::Pose pose[kModes]{};
    bool has[kModes]{};
};

struct Shared {
    Published<HookLock> lock;  // written by the input thread
    TryBox<Seen> seen;         // written by the hook
    std::atomic<uint32_t> calls{0};
    std::atomic<uint32_t> modeCalls[kModes]{};
    std::atomic<uint32_t> mode{255};             // query[3] of the last call
    std::atomic<uint32_t> lockModes{0x18};       // bit per lockable mode; 3 and 4
};

extern Shared g_shared;
