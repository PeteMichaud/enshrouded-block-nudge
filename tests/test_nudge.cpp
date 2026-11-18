#include <cmath>
#include <initializer_list>

#include "check.h"
#include "nudge.h"
#include "shared.h"

using namespace nudge;

namespace {

struct Quat {
    float q[4];
};

// A camera turned yawDeg about world Y (0 = looking along +Z, 90 = along +X), then pitched
// about its own X axis.
Quat camera(double yawDeg, double pitchDeg = 0) {
    const double kPi = 3.14159265358979323846;
    const double hy = yawDeg * kPi / 360, hp = pitchDeg * kPi / 360;
    const double ys = sin(hy), yc = cos(hy), ps = sin(hp), pc = cos(hp);
    // (0, ys, 0, yc) * (ps, 0, 0, pc)
    return Quat{{(float)(yc * ps), (float)(ys * pc), (float)(-ys * ps), (float)(yc * pc)}};
}

bool is(Step s, int axis, int sign) { return s.axis == axis && s.sign == sign; }

const int X = 0, Y = 1, Z = 2;

Cursor valid_cursor(int64_t x, int64_t y, int64_t z, uint64_t atMs) {
    Cursor c;
    c.haveValid = true;
    c.lastValid = Pose{{x, y, z}, {0.1f, 0.2f, 0.3f, 0.9f}};
    c.validMs = atMs;
    c.seenMs = atMs;
    return c;
}

}  // namespace

TEST(axes_in_four_quadrants) {
    const struct {
        double yaw;
        int fAxis, fSign, rAxis, rSign;
    } cases[] = {
        {0, Z, 1, X, 1},
        {90, X, 1, Z, -1},
        {180, Z, -1, X, -1},
        {270, X, -1, Z, 1},
    };
    for (const auto& c : cases) {
        const Quat cam = camera(c.yaw);
        CHECK(is(step_for(Dir::Forward, cam.q, false), c.fAxis, c.fSign));
        CHECK(is(step_for(Dir::Back, cam.q, false), c.fAxis, -c.fSign));
        CHECK(is(step_for(Dir::Right, cam.q, false), c.rAxis, c.rSign));
        CHECK(is(step_for(Dir::Left, cam.q, false), c.rAxis, -c.rSign));
    }
}

TEST(axes_either_side_of_each_diagonal) {
    const struct {
        double yaw;
        int fAxis, fSign, rAxis, rSign;
    } cases[] = {
        {40, Z, 1, X, 1},    {50, X, 1, Z, -1},
        {130, X, 1, Z, -1},  {140, Z, -1, X, -1},
        {220, Z, -1, X, -1}, {230, X, -1, Z, 1},
        {310, X, -1, Z, 1},  {320, Z, 1, X, 1},
    };
    for (const auto& c : cases) {
        const Quat cam = camera(c.yaw);
        CHECK(is(step_for(Dir::Forward, cam.q, false), c.fAxis, c.fSign));
        CHECK(is(step_for(Dir::Right, cam.q, false), c.rAxis, c.rSign));
    }
}

TEST(pitch_does_not_change_the_axes) {
    for (double pitch : {-60.0, -30.0, 30.0, 60.0}) {
        const Quat cam = camera(90, pitch);
        CHECK(is(step_for(Dir::Forward, cam.q, false), X, 1));
        CHECK(is(step_for(Dir::Right, cam.q, false), Z, -1));
    }
}

TEST(up_and_down_are_world_y) {
    const Quat cam = camera(123, 45);
    CHECK(is(step_for(Dir::Up, cam.q, false), Y, 1));
    CHECK(is(step_for(Dir::Down, cam.q, false), Y, -1));
    CHECK(is(step_for(Dir::Up, cam.q, true), Y, 1));
}

TEST(flip_swaps_left_and_right_only) {
    const Quat cam = camera(0);
    CHECK(is(step_for(Dir::Right, cam.q, true), X, -1));
    CHECK(is(step_for(Dir::Left, cam.q, true), X, 1));
    CHECK(is(step_for(Dir::Forward, cam.q, true), Z, 1));
}

TEST(lock_needs_a_recent_valid_cursor) {
    Lock lock;
    Cursor none;
    CHECK(toggle(lock, none, 1000, false) == Toggle::NoRecentCursor);
    CHECK(!lock.locked);

    const Cursor stale = valid_cursor(1, 2, 3, 1000);
    CHECK(toggle(lock, stale, 1000 + kLockFreshMs + 1, false) == Toggle::NoRecentCursor);
    CHECK(!lock.locked);

    CHECK(toggle(lock, stale, 1000 + kLockFreshMs, false) == Toggle::Locked);
    CHECK(lock.locked);
}

TEST(lock_replays_the_raw_cursor_exactly) {
    const int64_t x = 0x123456789ALL, y = -0x0FEDCBA987LL, z = 7;
    const Cursor cur = valid_cursor(x, y, z, 50);
    Lock lock;
    CHECK(toggle(lock, cur, 60, false) == Toggle::Locked);

    int64_t pos[3] = {0, 0, 0};
    float rot[4] = {0, 0, 0, 0};
    uint8_t valid = 0;
    apply(lock, pos, rot, &valid);
    CHECK(pos[0] == x && pos[1] == y && pos[2] == z);
    CHECK(rot[0] == 0.1f && rot[1] == 0.2f && rot[2] == 0.3f && rot[3] == 0.9f);
    CHECK(valid == 1);
}

TEST(toggle_again_unlocks_and_clears_the_offset) {
    const Cursor cur = valid_cursor(0, 0, 0, 0);
    Lock lock;
    toggle(lock, cur, 0, false);
    move(lock, Step{X, 1}, 3);
    CHECK(toggle(lock, cur, 10, false) == Toggle::Unlocked);
    CHECK(!lock.locked);
    CHECK(toggle(lock, cur, 20, false) == Toggle::Locked);
    CHECK(lock.cells[0] == 0 && lock.cells[1] == 0 && lock.cells[2] == 0);
}

TEST(moves_add_whole_cells_per_axis) {
    const Cursor cur = valid_cursor(100, 200, 300, 0);
    Lock lock;
    toggle(lock, cur, 0, false);
    move(lock, Step{X, 1}, 1);
    move(lock, Step{X, 1}, 4);
    move(lock, Step{Y, -1}, 1);
    move(lock, Step{Z, -1}, 4);
    CHECK(lock.cells[0] == 5 && lock.cells[1] == -1 && lock.cells[2] == -4);

    int64_t pos[3];
    float rot[4];
    uint8_t valid;
    apply(lock, pos, rot, &valid);
    CHECK(pos[0] == 100 + 5 * kCell);
    CHECK(pos[1] == 200 - kCell);
    CHECK(pos[2] == 300 - 4 * kCell);
}

TEST(moves_are_ignored_while_unlocked) {
    Lock lock;
    move(lock, Step{X, 1}, 1);
    CHECK(lock.cells[0] == 0);
}

TEST(offsets_at_large_and_negative_coordinates) {
    // 100 km out, in both directions: far beyond any map, well inside int64.
    const int64_t far = 100000LL << 32;
    for (int64_t base : {far, -far}) {
        const Cursor cur = valid_cursor(base, base + 12345, -base, 0);
        Lock lock;
        toggle(lock, cur, 0, false);
        move(lock, Step{X, -1}, 64);
        move(lock, Step{Z, 1}, 64);
        int64_t pos[3];
        float rot[4];
        uint8_t valid;
        apply(lock, pos, rot, &valid);
        CHECK(pos[0] == base - 64 * kCell);
        CHECK(pos[1] == base + 12345);
        CHECK(pos[2] == -base + 64 * kCell);
        CHECK(metres(pos[0]) == metres(base) - 32.0);
    }
}

TEST(round_to_cell) {
    CHECK(round_to_cell(0) == 0);
    CHECK(round_to_cell(kCell / 2 - 1) == 0);
    CHECK(round_to_cell(kCell / 2) == kCell);
    CHECK(round_to_cell(kCell + 5) == kCell);
    CHECK(round_to_cell(-1) == 0);
    CHECK(round_to_cell(-kCell / 2 - 1) == -kCell);
    CHECK(round_to_cell(-kCell) == -kCell);
    CHECK(round_to_cell(-3 * kCell + 7) == -3 * kCell);
}

TEST(snap_on_lock_rounds_the_position) {
    const Cursor cur = valid_cursor(kCell + 9, -kCell - 9, 3 * kCell - 1, 0);
    Lock lock;
    toggle(lock, cur, 0, true);
    CHECK(lock.pose.pos[0] == kCell);
    CHECK(lock.pose.pos[1] == -kCell);
    CHECK(lock.pose.pos[2] == 3 * kCell);
}

TEST(auto_unlock_after_the_cursor_goes_quiet) {
    Cursor cur = valid_cursor(0, 0, 0, 5000);
    Lock lock;
    CHECK(!should_auto_unlock(lock, cur, 99999));  // not locked: nothing to do
    toggle(lock, cur, 5000, false);
    CHECK(!should_auto_unlock(lock, cur, 5000 + kAutoUnlockMs));
    CHECK(should_auto_unlock(lock, cur, 5000 + kAutoUnlockMs + 1));
    cur.seenMs = 7000;
    CHECK(!should_auto_unlock(lock, cur, 7500));
}

TEST(a_cursor_stamped_after_now_counts_as_fresh) {
    // The hook and the input thread read the clock separately, so "then" can be the later.
    const Cursor cur = valid_cursor(0, 0, 0, 1010);
    Lock lock;
    CHECK(elapsed_ms(1000, 1010) == 0);
    CHECK(toggle(lock, cur, 1000, false) == Toggle::Locked);
    CHECK(!should_auto_unlock(lock, cur, 1000));
}

TEST(repeater_press_delay_then_rate) {
    Repeater r;
    CHECK(!r.poll(false, 0, 300, 100));
    CHECK(r.poll(true, 1000, 300, 100));   // press
    CHECK(!r.poll(true, 1008, 300, 100));
    CHECK(!r.poll(true, 1299, 300, 100));
    CHECK(r.poll(true, 1300, 300, 100));   // first repeat after the delay
    CHECK(!r.poll(true, 1399, 300, 100));
    CHECK(r.poll(true, 1400, 300, 100));
    CHECK(!r.poll(false, 1410, 300, 100)); // release
    CHECK(r.poll(true, 1420, 300, 100));   // a new press fires at once
}

TEST(repeater_does_not_burst_after_a_late_poll) {
    Repeater r;
    CHECK(r.poll(true, 0, 300, 100));
    CHECK(r.poll(true, 2000, 300, 100));  // very late: one step, not seventeen
    CHECK(!r.poll(true, 2050, 300, 100));
    CHECK(r.poll(true, 2100, 300, 100));
}

TEST(repeater_rate_zero_never_repeats) {
    Repeater r;
    CHECK(r.poll(true, 0, 300, 0));
    CHECK(!r.poll(true, 100000, 300, 0));
}

TEST(published_value_round_trips) {
    Published<Lock> box;
    Lock out;
    CHECK(box.read(out));
    CHECK(!out.locked);

    Lock in;
    in.locked = true;
    in.pose.pos[1] = -5 * kCell;
    in.cells[2] = 9;
    box.write(in);
    CHECK(box.read(out));
    CHECK(out.locked && out.pose.pos[1] == -5 * kCell && out.cells[2] == 9);
}

TEST(trybox_updates_and_reads) {
    TryBox<Seen> box;
    CHECK(box.try_update([](Seen& s) { s.cam[0] = 42; }));
    const Seen s = box.read([] {});
    CHECK(s.cam[0] == 42);
    // A writer that arrives while the box is busy skips instead of waiting.
    bool innerRan = false;
    box.try_update([&](Seen&) { innerRan = box.try_update([](Seen&) {}); });
    CHECK(!innerRan);
}
