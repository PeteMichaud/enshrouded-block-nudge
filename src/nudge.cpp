#include "nudge.h"

namespace nudge {

static int sign_of(float v) { return v < 0 ? -1 : 1; }
static float abs_of(float v) { return v < 0 ? -v : v; }

Step step_for(Dir dir, const float q[4], bool flipLeftRight) {
    if (dir == Dir::Up) return {1, 1};
    if (dir == Dir::Down) return {1, -1};

    const float x = q[0], y = q[1], z = q[2], w = q[3];
    // Camera forward is the quaternion applied to +Z, camera right the same applied to +X
    // (as Enshrouded Building Tools computes them). Only the horizontal parts matter.
    const float fx = 2 * (x * z + w * y), fz = 1 - 2 * (x * x + y * y);
    const float rx = 1 - 2 * (y * y + z * z), rz = 2 * (x * z - w * y);

    Step fwd, right;
    if (abs_of(fx) >= abs_of(fz)) {
        fwd = {0, sign_of(fx)};
        right = {2, sign_of(rz)};
    } else {
        fwd = {2, sign_of(fz)};
        right = {0, sign_of(rx)};
    }
    if (flipLeftRight) right.sign = -right.sign;

    switch (dir) {
        case Dir::Forward: return fwd;
        case Dir::Back: return {fwd.axis, -fwd.sign};
        case Dir::Right: return right;
        default: return {right.axis, -right.sign};
    }
}

void apply(const Lock& lock, int64_t pos[3], float rot[4], uint8_t* valid) {
    for (int a = 0; a < 3; a++) pos[a] = lock.pose.pos[a] + lock.cells[a] * kCell;
    for (int k = 0; k < 4; k++) rot[k] = lock.pose.rot[k];
    *valid = 1;
}

Toggle toggle(Lock& lock, const Cursor& cur, uint64_t nowMs, bool snap) {
    if (lock.locked) {
        lock = Lock{};
        return Toggle::Unlocked;
    }
    if (!cur.haveValid || elapsed_ms(nowMs, cur.validMs) > kLockFreshMs) return Toggle::NoRecentCursor;
    lock.pose = cur.lastValid;
    if (snap)
        for (int a = 0; a < 3; a++) lock.pose.pos[a] = round_to_cell(lock.pose.pos[a]);
    for (int a = 0; a < 3; a++) lock.cells[a] = 0;
    lock.locked = true;
    return Toggle::Locked;
}

void move(Lock& lock, Step step, int cells) {
    if (!lock.locked) return;
    lock.cells[step.axis] += step.sign * cells;
}

bool should_auto_unlock(const Lock& lock, const Cursor& cur, uint64_t nowMs) {
    return lock.locked && elapsed_ms(nowMs, cur.seenMs) > kAutoUnlockMs;
}

uint64_t elapsed_ms(uint64_t nowMs, uint64_t thenMs) {
    return nowMs > thenMs ? nowMs - thenMs : 0;
}

int64_t round_to_cell(int64_t v) {
    return (v + kCell / 2) & ~(kCell - 1);
}

double metres(int64_t fixed) {
    return (double)fixed / 4294967296.0;
}

bool Repeater::poll(bool down, uint64_t nowMs, uint32_t delayMs, uint32_t rateMs) {
    if (!down) {
        down_ = false;
        return false;
    }
    if (!down_) {
        down_ = true;
        nextMs_ = nowMs + delayMs;
        return true;
    }
    if (rateMs == 0 || nowMs < nextMs_) return false;
    // From now, not from the missed deadline: a late poll must not fire a burst.
    nextMs_ = nowMs + rateMs;
    return true;
}

}  // namespace nudge
