// Pure lock-and-nudge logic. No Windows or game dependencies, so it is unit-testable.
#pragma once
#include <cstdint>

namespace nudge {

// Positions are int64 fixed point, 1 m = 2^32. One building grid cell is 0.5 m.
constexpr int64_t kCell = 0x80000000ll;

constexpr uint64_t kLockFreshMs = 300;    // a lock needs a valid cursor this recent
constexpr uint64_t kAutoUnlockMs = 1000;  // no building cursor for this long clears the lock

struct Pose {
    int64_t pos[3];
    float rot[4];
};

enum class Dir { Forward, Back, Left, Right, Up, Down };

struct Step {
    int axis;  // 0 = X, 1 = Y (up), 2 = Z
    int sign;  // +1 or -1
};

// The world axis a nudge key moves along, from the camera rotation quaternion (x, y, z, w).
// Forward is the dominant horizontal axis of the view direction; right is the axis across
// it that the camera's right vector points to. Up and down are always world Y.
Step step_for(Dir dir, const float camRot[4], bool flipLeftRight);

struct Lock {
    bool locked = false;
    Pose pose{};
    int32_t cells[3]{};
};

// What the hook does to the game's cursor while locked.
void apply(const Lock& lock, int64_t pos[3], float rot[4], uint8_t* valid);

// What the hook last saw from the game, before any override.
struct Cursor {
    bool haveValid = false;
    Pose lastValid{};
    uint64_t validMs = 0;  // when lastValid was recorded
    uint64_t seenMs = 0;   // last call with a building shape in hand
    float camRot[4]{0, 0, 0, 1};
};

enum class Toggle { Locked, Unlocked, NoRecentCursor };

// Lock at the last valid cursor, or release the lock. With snap, the locked position is
// rounded to the grid instead of replaying the game's raw value.
Toggle toggle(Lock& lock, const Cursor& cur, uint64_t nowMs, bool snap);

void move(Lock& lock, Step step, int cells);

bool should_auto_unlock(const Lock& lock, const Cursor& cur, uint64_t nowMs);

// Milliseconds from thenMs to nowMs; 0 if thenMs is the later of the two.
uint64_t elapsed_ms(uint64_t nowMs, uint64_t thenMs);

int64_t round_to_cell(int64_t v);

double metres(int64_t fixed);

// Turns a held key into steps: one on press, then repeats after a delay.
class Repeater {
public:
    // True when a step is due. rateMs == 0 disables repeating.
    bool poll(bool down, uint64_t nowMs, uint32_t delayMs, uint32_t rateMs);

private:
    bool down_ = false;
    uint64_t nextMs_ = 0;
};

}  // namespace nudge
