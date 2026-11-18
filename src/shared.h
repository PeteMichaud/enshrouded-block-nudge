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

// What the hook saw on its last call with a building shape in hand.
struct Seen {
    nudge::Cursor cur;
    nudge::Pose raw{};  // the game's own output, before any override
    bool rawValid = false;
    int64_t cam[3]{};
};

struct Shared {
    Published<nudge::Lock> lock;  // written by the input thread
    TryBox<Seen> seen;            // written by the hook
    std::atomic<uint32_t> calls{0};
    std::atomic<uint32_t> mode{255};  // query[3] of the last call
};

extern Shared g_shared;
