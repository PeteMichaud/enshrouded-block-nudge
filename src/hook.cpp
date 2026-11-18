#include "hook.h"

#include <windows.h>

#include <cstring>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#include <MinHook.h>

#include "log.h"
#include "scan.h"
#include "shared.h"

Shared g_shared;

// The game function that turns the aim ray into the place cursor transform. Signature and
// struct offsets are from Enshrouded Building Tools (see README).
static const char* kCursorXfSig =
    "48 8B C4 55 53 56 57 41 56 48 8D 6C 24 90 48 81 EC 70 01 00 00 0F 29 70 C8";
static const size_t kHookBytes = 5;  // what another mod's jump over the function start replaces

static const size_t kOutRot = 0x18, kOutValid = 0x28;  // out: int64 pos[3], float rot[4], bool valid
static const size_t kQueryMode = 3;                    // query[3]: which cursor this call is for
static const size_t kSysCamPos = 0x640, kSysCamRot = 0x658;

typedef uint8_t* (*CursorXfFn)(uint8_t* out, uint8_t* sys, uint8_t* query, uint8_t* ray);
static CursorXfFn g_original = nullptr;
static uint8_t* g_image = nullptr;

#if defined(_MSC_VER)
#define CALLER_ADDRESS() ((uint8_t*)_ReturnAddress())
#else
#define CALLER_ADDRESS() ((uint8_t*)__builtin_return_address(0))
#endif

// Runs on game threads, once or twice a frame. No waiting, no I/O.
static uint8_t* cursor_hook(uint8_t* out, uint8_t* sys, uint8_t* query, uint8_t* ray) {
    uint8_t* const caller = CALLER_ADDRESS();
    uint8_t* result = g_original(out, sys, query, ray);
    g_shared.calls.fetch_add(1, std::memory_order_relaxed);
    if (!out || !sys || !query) return result;
    const uint8_t mode = query[kQueryMode];
    g_shared.mode.store(mode, std::memory_order_relaxed);
    if (mode >= kModes) return result;
    g_shared.modeCalls[mode].fetch_add(1, std::memory_order_relaxed);
    const bool lockable = (g_shared.lockModes.load(std::memory_order_relaxed) >> mode) & 1;

    HookLock lock;
    const bool locked = lockable && g_shared.lock.read(lock) && lock.lock.locked;
    const uint64_t now = GetTickCount64();

    g_shared.seen.try_update([&](Seen& s) {
        ModeSeen& m = s.modes[mode];
        memcpy(m.raw.pos, out, sizeof(m.raw.pos));
        memcpy(m.raw.rot, out + kOutRot, sizeof(m.raw.rot));
        m.rawValid = out[kOutValid] != 0;
        memcpy(m.cam, sys + kSysCamPos, sizeof(m.cam));
        memcpy(m.query, query, sizeof(m.query));
        m.rayType = ray ? ray[0] : 255;
        m.callerRva = (uint32_t)(caller - g_image);
        m.sys = (uintptr_t)sys;
        m.seenMs = now;
        if (!locked && m.rawValid) {
            m.lastValid = m.raw;
            m.validMs = now;
            m.haveValid = true;
        }
        if (!lockable) return;
        memcpy(s.cur.camRot, sys + kSysCamRot, sizeof(s.cur.camRot));
        s.cur.seenMs = now;
        if (!locked && m.rawValid) {
            s.cur.lastValid = m.raw;
            s.cur.validMs = now;
            s.cur.haveValid = true;
        }
    });

    if (locked && lock.has[mode]) {
        nudge::Lock held = lock.lock;
        held.pose = lock.pose[mode];
        int64_t pos[3];
        float rot[4];
        uint8_t valid;
        nudge::apply(held, pos, rot, &valid);
        memcpy(out, pos, sizeof(pos));
        memcpy(out + kOutRot, rot, sizeof(rot));
        out[kOutValid] = valid;
    }
    return result;
}

struct Text {
    uint8_t* base = nullptr;
    size_t size = 0;
};

static Text find_text(uint8_t* image) {
    Text t;
    auto* dos = (IMAGE_DOS_HEADER*)image;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return t;
    auto* nt = (IMAGE_NT_HEADERS64*)(image + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return t;
    logf("game image: base %p, size 0x%lx, link timestamp 0x%08lx", (void*)image,
         (unsigned long)nt->OptionalHeader.SizeOfImage, (unsigned long)nt->FileHeader.TimeDateStamp);
    auto* section = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (memcmp(section[i].Name, ".text", 6) == 0) {
            t.base = image + section[i].VirtualAddress;
            t.size = section[i].Misc.VirtualSize;
        }
    }
    return t;
}

static void log_bytes(const char* what, const uint8_t* p) {
    logf("%s: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X", what,
         p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
}

// The one place the signature matches, or null. If another mod has already hooked the
// function its first bytes are a jump, so retry without them and require that jump.
static uint8_t* find_cursor_xf(uint8_t* image, const Text& text) {
    const scan::Pattern sig = scan::parse(kCursorXfSig);
    const uint8_t* hits[8];
    size_t n = scan::find_all(text.base, text.size, sig, hits, 8);
    if (n == 1) {
        logf("signature CursorXf: 1 match at rva 0x%llx", (unsigned long long)(hits[0] - image));
        return (uint8_t*)hits[0];
    }
    if (n > 1) {
        logf("signature CursorXf: %u matches, first at rva 0x%llx; refusing to guess", (unsigned)n,
             (unsigned long long)(hits[0] - image));
        return nullptr;
    }

    n = scan::find_all(text.base, text.size, scan::wildcard_head(sig, kHookBytes), hits, 8);
    const uint8_t* hooked = nullptr;
    size_t hookedCount = 0;
    for (size_t i = 0; i < n && i < 8; i++) {
        const size_t avail = (size_t)(text.base + text.size - hits[i]);
        if (scan::starts_with_jump(hits[i], avail)) {
            hooked = hits[i];
            hookedCount++;
        }
    }
    if (hookedCount == 1 && n <= 8) {
        logf("signature CursorXf: found at rva 0x%llx, already hooked by another mod (starts with a jump); chaining to it",
             (unsigned long long)(hooked - image));
        return (uint8_t*)hooked;
    }
    logf("signature CursorXf: not found (%u candidates without the first %u bytes, %u starting with a jump)",
         (unsigned)n, (unsigned)kHookBytes, (unsigned)hookedCount);
    return nullptr;
}

bool hook_install() {
    uint8_t* image = (uint8_t*)GetModuleHandleW(nullptr);
    g_image = image;
    const Text text = find_text(image);
    if (!text.base || !text.size) {
        logf("no .text section found in the game image; game left vanilla");
        return false;
    }
    logf(".text: %p .. %p (rva 0x%llx, size 0x%llx)", (void*)text.base, (void*)(text.base + text.size),
         (unsigned long long)(text.base - image), (unsigned long long)text.size);

    uint8_t* target = find_cursor_xf(image, text);
    if (!target) {
        logf("the game's cursor function was not identified (game update?); game left vanilla");
        return false;
    }
    log_bytes("function start before hooking", target);

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK) {
        logf("MH_Initialize failed: %s; game left vanilla", MH_StatusToString(status));
        return false;
    }
    status = MH_CreateHook(target, (LPVOID)&cursor_hook, (LPVOID*)&g_original);
    if (status == MH_OK) status = MH_EnableHook(target);
    if (status != MH_OK) {
        logf("hooking failed: %s; game left vanilla", MH_StatusToString(status));
        MH_Uninitialize();
        return false;
    }
    log_bytes("function start after hooking", target);
    logf("cursor hook installed, trampoline at %p", (void*)g_original);
    return true;
}
