#include "input.h"

#include <windows.h>

#include "log.h"
#include "nudge.h"
#include "shared.h"

using nudge::Dir;

static Config g_config;

static const DWORD kPollMs = 8;
static const uint64_t kTraceMs = 1000;
// With StayLockedAfterPlace=0: how long after the mouse button comes up the lock is released,
// so the game has acted on the click before the cursor moves back.
static const uint64_t kUnlockAfterClickMs = 150;

static bool key_down(int vk) {
    return vk > 0 && (GetAsyncKeyState(vk) & 0x8000) != 0;
}

static bool game_in_foreground() {
    const HWND window = GetForegroundWindow();
    if (!window) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    return pid == GetCurrentProcessId();
}

static const char* dir_name(Dir d) {
    switch (d) {
        case Dir::Forward: return "forward";
        case Dir::Back: return "back";
        case Dir::Left: return "left";
        case Dir::Right: return "right";
        case Dir::Up: return "up";
        default: return "down";
    }
}

static void log_lock(const char* what, const nudge::Lock& lock) {
    int64_t pos[3];
    float rot[4];
    uint8_t valid;
    nudge::apply(lock, pos, rot, &valid);
    logf("%s: offset (%d, %d, %d) cells, cursor (%.3f, %.3f, %.3f) m", what, (int)lock.cells[0], (int)lock.cells[1],
         (int)lock.cells[2], nudge::metres(pos[0]), nudge::metres(pos[1]), nudge::metres(pos[2]));
}

static void log_trace(const Seen& seen, const nudge::Lock& lock, uint32_t callsPerSecond) {
    const float* q = seen.cur.camRot;
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float fwd[3] = {2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y)};
    const nudge::Pose& p = seen.raw;
    logf("trace: mode=4 valid=%d pos=(%.3f, %.3f, %.3f) rot=(%.3f, %.3f, %.3f, %.3f) cam=(%.3f, %.3f, %.3f) "
         "fwd=(%.2f, %.2f, %.2f) calls/s=%u locked=%d",
         seen.rawValid ? 1 : 0, nudge::metres(p.pos[0]), nudge::metres(p.pos[1]), nudge::metres(p.pos[2]),
         p.rot[0], p.rot[1], p.rot[2], p.rot[3], nudge::metres(seen.cam[0]), nudge::metres(seen.cam[1]),
         nudge::metres(seen.cam[2]), fwd[0], fwd[1], fwd[2], (unsigned)callsPerSecond, lock.locked ? 1 : 0);
}

static DWORD WINAPI input_thread(void*) {
    const Config& cfg = g_config;
    const struct {
        Dir dir;
        int key;
    } nudgeKeys[] = {
        {Dir::Forward, cfg.keyForward}, {Dir::Back, cfg.keyBack}, {Dir::Left, cfg.keyLeft},
        {Dir::Right, cfg.keyRight},     {Dir::Up, cfg.keyUp},     {Dir::Down, cfg.keyDown},
    };
    nudge::Repeater repeat[6];

    nudge::Lock lock;  // the authoritative copy; g_shared.lock is what the hook reads
    bool lockKeyWasDown = false, mouseWasDown = false;
    uint64_t unlockAtMs = 0;
    uint64_t lastTraceMs = 0;
    uint32_t lastCalls = 0, lastMode = 255;

    for (;;) {
        Sleep(kPollMs);
        const Seen seen = g_shared.seen.read([] { SwitchToThread(); });
        const uint64_t now = GetTickCount64();
        const bool foreground = game_in_foreground();

        if (nudge::should_auto_unlock(lock, seen.cur, now)) {
            lock = nudge::Lock{};
            g_shared.lock.write(lock);
            unlockAtMs = 0;
            logf("unlocked: no building cursor for %u ms (hammer put away?)", (unsigned)nudge::kAutoUnlockMs);
        }

        const bool lockKeyDown = foreground && key_down(cfg.keyLock);
        if (lockKeyDown && !lockKeyWasDown) {
            switch (nudge::toggle(lock, seen.cur, now, cfg.snapOnLock)) {
                case nudge::Toggle::Locked:
                    g_shared.lock.write(lock);
                    logf("locked: raw (%.4f, %.4f, %.4f) m, rot (%.3f, %.3f, %.3f, %.3f)%s",
                         nudge::metres(seen.cur.lastValid.pos[0]), nudge::metres(seen.cur.lastValid.pos[1]),
                         nudge::metres(seen.cur.lastValid.pos[2]), lock.pose.rot[0], lock.pose.rot[1],
                         lock.pose.rot[2], lock.pose.rot[3], cfg.snapOnLock ? ", snapped to the grid" : "");
                    break;
                case nudge::Toggle::Unlocked:
                    g_shared.lock.write(lock);
                    unlockAtMs = 0;
                    logf("unlocked");
                    break;
                case nudge::Toggle::NoRecentCursor:
                    logf("lock refused: no valid building cursor in the last %u ms (no shape in hand, or aiming at nothing)",
                         (unsigned)nudge::kLockFreshMs);
                    break;
            }
        }
        lockKeyWasDown = lockKeyDown;

        for (int i = 0; i < 6; i++) {
            const bool due = repeat[i].poll(foreground && key_down(nudgeKeys[i].key), now, (uint32_t)cfg.repeatDelayMs,
                                            (uint32_t)cfg.repeatRateMs);
            if (!due || !lock.locked) continue;
            // The modifier is read with each step, so it can be pressed or let go mid-run.
            const int cells = key_down(cfg.keyLargeStep) ? cfg.largeStepCells : cfg.stepCells;
            const nudge::Step step = nudge::step_for(nudgeKeys[i].dir, seen.cur.camRot, cfg.flipLeftRight);
            nudge::move(lock, step, cells);
            g_shared.lock.write(lock);
            char what[48];
            wsprintfA(what, "nudge %s = %c%c x%d", dir_name(nudgeKeys[i].dir), step.sign < 0 ? '-' : '+',
                      "XYZ"[step.axis], cells);
            log_lock(what, lock);
        }

        if (!cfg.stayLockedAfterPlace) {
            const bool mouseDown = foreground && key_down(VK_LBUTTON);
            if (lock.locked && mouseWasDown && !mouseDown) unlockAtMs = now + kUnlockAfterClickMs;
            mouseWasDown = mouseDown;
            if (lock.locked && unlockAtMs && now >= unlockAtMs) {
                lock = nudge::Lock{};
                g_shared.lock.write(lock);
                unlockAtMs = 0;
                logf("unlocked after a click (StayLockedAfterPlace=0)");
            }
        }

        if (cfg.trace && now - lastTraceMs >= kTraceMs) {
            const uint32_t calls = g_shared.calls.load(std::memory_order_relaxed);
            const uint32_t mode = g_shared.mode.load(std::memory_order_relaxed);
            const uint32_t perSecond = (uint32_t)((uint64_t)(calls - lastCalls) * 1000 / (now - lastTraceMs));
            if (seen.cur.seenMs && nudge::elapsed_ms(now, seen.cur.seenMs) < kTraceMs) {
                log_trace(seen, lock, perSecond);
            } else if (mode != lastMode) {
                // Without a building shape in hand, one line per change of mode is enough.
                logf("trace: mode=%u (no building shape in hand) calls/s=%u", (unsigned)mode, (unsigned)perSecond);
            }
            lastMode = mode;
            lastCalls = calls;
            lastTraceMs = now;
        }
    }
}

void input_start(const Config& config) {
    g_config = config;
    const HANDLE thread = CreateThread(nullptr, 0, input_thread, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
    else logf("could not start the input thread (error %lu); keys will not work", GetLastError());
}
