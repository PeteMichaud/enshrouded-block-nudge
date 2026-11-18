#include "input.h"

#include <windows.h>

#include <cstdio>

#include "log.h"
#include "nudge.h"
#include "shared.h"

using nudge::Dir;

static Config g_config;

static const DWORD kPollMs = 8;
static const uint64_t kTraceMs = 1000;
// Placing a block releases the lock. This is how long after the mouse button comes up, so the
// game has acted on the click before the cursor moves back.
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

// Calls a second for each mode the game used since the last sample, as "m3=60 m4=18".
struct ModeRates {
    uint32_t perSecond[kModes]{};
    char text[96]{};
};

static ModeRates mode_rates(const uint32_t calls[kModes], const uint32_t lastCalls[kModes], uint64_t spanMs) {
    ModeRates r;
    size_t n = 0;
    for (int m = 0; m < kModes; m++) {
        r.perSecond[m] = spanMs ? (uint32_t)((uint64_t)(calls[m] - lastCalls[m]) * 1000 / spanMs) : 0;
        if (calls[m] == lastCalls[m] || n + 16 > sizeof(r.text)) continue;
        n += (size_t)snprintf(r.text + n, sizeof(r.text) - n, "%sm%d=%u", n ? " " : "", m, (unsigned)r.perSecond[m]);
    }
    if (!n) snprintf(r.text, sizeof(r.text), "none");
    return r;
}

static void log_mode(int mode, const ModeSeen& m, uint32_t perSecond, bool held) {
    const nudge::Pose& p = m.raw;
    logf("trace: mode=%d calls/s=%u valid=%d pos=(%.3f, %.3f, %.3f) rot=(%.3f, %.3f, %.3f, %.3f) cam=(%.3f, %.3f, %.3f) "
         "query=%02X %02X %02X %02X %02X %02X %02X %02X ray=%u caller=rva 0x%x sys=%p held=%d",
         mode, (unsigned)perSecond, m.rawValid ? 1 : 0, nudge::metres(p.pos[0]), nudge::metres(p.pos[1]),
         nudge::metres(p.pos[2]), p.rot[0], p.rot[1], p.rot[2], p.rot[3], nudge::metres(m.cam[0]),
         nudge::metres(m.cam[1]), nudge::metres(m.cam[2]), m.query[0], m.query[1], m.query[2], m.query[3],
         m.query[4], m.query[5], m.query[6], m.query[7], (unsigned)m.rayType, (unsigned)m.callerRva, (void*)m.sys,
         held ? 1 : 0);
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

    HookLock held;                   // the authoritative copy; g_shared.lock is what the hook reads
    nudge::Lock& lock = held.lock;
    bool lockKeyWasDown = false, mouseWasDown = false;
    uint64_t unlockAtMs = 0;
    uint64_t lastTraceMs = 0;
    uint32_t lastCalls[kModes]{};
    const uint32_t kNoIdleState = 0xffffffff;
    uint32_t lastIdleState = kNoIdleState;

    for (;;) {
        Sleep(kPollMs);
        const Seen seen = g_shared.seen.read([] { SwitchToThread(); });
        const uint64_t now = GetTickCount64();
        const bool foreground = game_in_foreground();
        uint32_t calls[kModes];
        for (int m = 0; m < kModes; m++) calls[m] = g_shared.modeCalls[m].load(std::memory_order_relaxed);

        if (nudge::should_auto_unlock(lock, seen.cur, now)) {
            held = HookLock{};
            g_shared.lock.write(held);
            unlockAtMs = 0;
            logf("unlocked: no building cursor for %u ms (hammer put away?)", (unsigned)nudge::kAutoUnlockMs);
        }

        const bool lockKeyDown = foreground && key_down(cfg.keyLock);
        if (lockKeyDown && !lockKeyWasDown) {
            switch (nudge::toggle(lock, seen.cur, now, cfg.snapOnLock)) {
                case nudge::Toggle::Locked:
                    // Each lockable mode is held where it was itself, so two cursors that
                    // differ (the cell to add to and the one aimed at, say) both stay put.
                    for (int m = 0; m < kModes; m++) {
                        const ModeSeen& s = seen.modes[m];
                        held.has[m] = ((cfg.cursorModes >> m) & 1) && s.haveValid &&
                                      nudge::elapsed_ms(now, s.validMs) <= nudge::kLockFreshMs;
                        if (!held.has[m]) continue;
                        held.pose[m] = s.lastValid;
                        if (cfg.snapOnLock)
                            for (int a = 0; a < 3; a++) held.pose[m].pos[a] = nudge::round_to_cell(held.pose[m].pos[a]);
                    }
                    g_shared.lock.write(held);
                    for (int m = 0; m < kModes; m++) {
                        if (!held.has[m]) continue;
                        const nudge::Pose& p = held.pose[m];
                        logf("locked: mode %d at (%.4f, %.4f, %.4f) m, rot (%.3f, %.3f, %.3f, %.3f)%s", m,
                             nudge::metres(p.pos[0]), nudge::metres(p.pos[1]), nudge::metres(p.pos[2]), p.rot[0],
                             p.rot[1], p.rot[2], p.rot[3], cfg.snapOnLock ? ", snapped to the grid" : "");
                    }
                    break;
                case nudge::Toggle::Unlocked:
                    held = HookLock{};
                    g_shared.lock.write(held);
                    unlockAtMs = 0;
                    logf("unlocked");
                    break;
                case nudge::Toggle::NoRecentCursor: {
                    char seenAgo[24] = "never", validAgo[24] = "never";
                    if (seen.cur.seenMs) wsprintfA(seenAgo, "%u ms ago", (unsigned)nudge::elapsed_ms(now, seen.cur.seenMs));
                    if (seen.cur.haveValid) wsprintfA(validAgo, "%u ms ago", (unsigned)nudge::elapsed_ms(now, seen.cur.validMs));
                    logf("lock refused: no valid cursor in a lockable mode in the last %u ms (calls/s by mode: %s; "
                         "last lockable call %s, last valid one %s)",
                         (unsigned)nudge::kLockFreshMs, mode_rates(calls, lastCalls, now - lastTraceMs).text, seenAgo,
                         validAgo);
                    break;
                }
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
            g_shared.lock.write(held);
            char what[48];
            wsprintfA(what, "nudge %s = %c%c x%d", dir_name(nudgeKeys[i].dir), step.sign < 0 ? '-' : '+',
                      "XYZ"[step.axis], cells);
            log_lock(what, lock);
        }

        const bool mouseDown = foreground && key_down(VK_LBUTTON);
        if (lock.locked && mouseWasDown && !mouseDown) unlockAtMs = now + kUnlockAfterClickMs;
        mouseWasDown = mouseDown;
        if (lock.locked && unlockAtMs && now >= unlockAtMs) {
            held = HookLock{};
            g_shared.lock.write(held);
            unlockAtMs = 0;
            logf("unlocked: block placed");
        }

        if (now - lastTraceMs >= kTraceMs) {
            const ModeRates rates = mode_rates(calls, lastCalls, now - lastTraceMs);
            bool lockableSeen = false;
            for (int m = 0; m < kModes; m++) {
                if (!((cfg.cursorModes >> m) & 1) || calls[m] == lastCalls[m]) continue;
                lockableSeen = true;
                if (cfg.trace) log_mode(m, seen.modes[m], rates.perSecond[m], lock.locked && held.has[m]);
            }
            if (lockableSeen) {
                if (cfg.trace) {
                    const float* q = seen.cur.camRot;
                    const float x = q[0], y = q[1], z = q[2], w = q[3];
                    logf("trace: fwd=(%.2f, %.2f, %.2f) calls/s by mode: %s, locked=%d", 2 * (x * z + w * y),
                         2 * (y * z - w * x), 1 - 2 * (x * x + y * y), rates.text, lock.locked ? 1 : 0);
                }
                lastIdleState = kNoIdleState;
            } else {
                // Without a lockable cursor, one line per change is enough: of which modes the
                // game runs, and of whether the game is in front.
                uint32_t idleState = foreground ? 0x100 : 0;
                for (int m = 0; m < kModes; m++)
                    if (calls[m] != lastCalls[m]) idleState |= 1u << m;
                if (cfg.trace && idleState != lastIdleState) {
                    logf("trace: no lockable cursor; calls/s by mode: %s; game %s", rates.text,
                         foreground ? "in front" : "in the background");
                }
                lastIdleState = idleState;
            }
            for (int m = 0; m < kModes; m++) lastCalls[m] = calls[m];
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
