# Brief: Enshrouded block lock-and-nudge mod

Working name: **BlockNudge** (rename freely).

## Goal

A small, single-purpose client mod for Enshrouded (PC, Steam) that adds Satisfactory-style
nudge controls to block placement with the Construction Hammer:

1. Aim a block ghost as usual.
2. Press **Lock**. The ghost stops following the aim and stays where it is.
3. Tap nudge keys to move it one grid cell at a time (0.5 m), camera-relative.
4. Left click places it with the game's own placement path. Press Lock again to release.

That is the whole product. Everything else is out of scope.

## Non-goals

- No sub-grid movement, no arbitrary rotation. Blocks are voxels on a 0.5 m grid.
- No prop/furniture placement (the FreePlace mod covers that).
- No area fill, replace, blueprints, creative mode, or in-game menus (Building Tools
  covers those).
- No hover-in-front-of-camera mode in v1. Lock captures the vanilla ghost wherever it is.
- No changes to cost, undo, or replication. The mod only changes where the cursor is.

## Prior art and licensing

The reverse engineering comes from **Enshrouded Building Tools** by Aleksander Wojtal, MIT:
https://github.com/AleksanderWojtal-CC/Enshrouded-BuildingTools

Facts below were read from that repo at commit `6af1321` (v0.33.2, 2026-11-14). Clone it
for reference before starting. Useful places:

- `src/main.cpp`: `SIG_CURSOR_XF`, `cursor_xf_hook`, `find_unique`, `find_hookable`
- `docs/reverse-engineering-notes.md`: "Engine facts", "Test 15", "0.21.0: dedicated server"
- `docs/HOW-IT-WORKS.md`: "Loading", "Free placement"

This mod is MIT too. We take the signature, struct offsets, and the idea of post-hooking
the cursor transform, and write our own code around them. Requirements:

- `README.md` credits Building Tools and links the repo.
- If any of his code is copied verbatim, add `THIRD-PARTY-NOTICES.md` with his copyright
  line and the MIT text.
- MinHook is BSD-2-Clause: include its notice in `THIRD-PARTY-NOTICES.md`.

## What we know about the game (from upstream)

**The function to hook.** It turns the aim ray into the place cursor transform. RVA
`0x28bc80` in the original 1.0 client; find it by signature, never by address.

```
uint8_t* CursorXf(uint8_t* out, uint8_t* sys, uint8_t* query, uint8_t* ray);

SIG_CURSOR_XF =
  48 8B C4 55 53 56 57 41 56 48 8D 6C 24 90 48 81 EC 70 01 00 00 0F 29 70 C8
```

Upstream reports this signature matching on 1.0, Patch #1 (1.0.1.0), Hotfix #4 (1.0.1.1)
and Hotfix #5 (1.0.1.2). The installed game may be newer. Log the match result.

**`out` (0x38 bytes):**

| Offset | Type | Meaning |
|---|---|---|
| `+0x00` | `int64[3]` | position x, y, z, fixed point, 1 m = 2^32 |
| `+0x18` | `float[4]` | rotation quaternion |
| `+0x28` | `bool` | valid |

**`query`:** `query[3]` is the cursor mode. `4` = blueprint mode, meaning a building shape
with the hammer or a building material in hand. Only act when `query[3] == 4`.

**`sys` (the cursor system):**

| Offset | Type | Meaning |
|---|---|---|
| `+0x640` | `int64[3]` | camera position, same fixed point |
| `+0x658` | `float[4]` | camera rotation quaternion (x, y, z, w) |

Camera forward from that quaternion, as upstream computes it:

```
fwd = { 2*(x*z + w*y), 2*(y*z - w*x), 1 - 2*(x*x + y*y) }
```

**Other facts:**

- Y is up.
- One grid cell is 0.5 m = `0x80000000` in fixed point.
- The function runs about twice per frame, and for more than one cursor system (`sys`)
  in a session. Keep the lock state global, not per `sys`.
- Its raw output is the ray hit, not yet on the grid. The game floors it to 0.5 m
  downstream, and where the ghost sits also depends on the rotation.
- If `valid` is 0 at click time, the game drops the click.
- Upstream's free placement is a post-hook that overwrites `pos`, restores the last valid
  rotation, and sets `valid = 1`. The game then ghosts and places there normally.
- This function exists in the client only. The server receives the cursor as part of the
  client's input. That is why a cursor-only mod should work in multiplayer, but treat it
  as unproven.

## Design

### Hook behaviour

Post-hook `CursorXf`. Call the original first, then:

```
if out, sys, query are null or query[3] != 4: return
if state == Unlocked:
    if out.valid: remember last_valid = {pos, rot}     // exact copy
    return
if state == Locked:
    out.pos   = locked.pos + offset_cells * 0x80000000   // per axis
    out.rot   = locked.rot
    out.valid = 1
```

On Lock: copy `last_valid` into `locked`, zero the offset. Refuse to lock if there has been
no valid cursor in the last ~300 ms (player is aiming at the sky), and say so in the log.

Store the raw position verbatim and do not snap it ourselves. Replaying the same raw value
and rotation should make the game's own downstream snapping land on the same cell, so the
ghost must not jump at the moment of locking. Verify this in milestone 3. If it jumps, fall
back to upstream's approach (round to the 0.5 m grid ourselves).

Nudging adds whole cells, so the downstream snap moves the ghost by exactly that many cells.

### Camera-relative nudging

Horizontal nudges follow the view, as in Satisfactory:

- Take `fwd`, drop Y, pick the dominant of X and Z with its sign. That world axis is
  "forward".
- "Right" is the horizontal axis across forward that the camera's right vector points to.
  Upstream (`exact_begin` in `src/main.cpp`) takes camera right as the quaternion applied to
  +X: `right = { 1 - 2*(y*y + z*z), 2*(x*y + w*z), 2*(x*z - w*y) }`. Use the same, confirm
  in-game, and keep a `FlipLeftRight` ini switch in case it is wrong.
- Re-evaluate on every key press, not at lock time, so walking around the ghost keeps the
  keys intuitive.
- Up and down are always world Y.

### Controls

All rebindable in `BlockNudge.ini`. Defaults match Satisfactory with the Infinite Nudge mod
(see the next section); check them against Enshrouded's own bindings on first test.

| Action | Default | Source |
|---|---|---|
| Lock / unlock | `H` | Satisfactory |
| Nudge forward / back / left / right | arrow keys | Satisfactory |
| Nudge up / down | `PageUp` / `PageDown` | Satisfactory (1.1 and later) |
| Large step, held while nudging | `Left Shift` | Infinite Nudge |
| Mod on / off (off = vanilla) | none (ini only) | ours |

Ini options: the keys, `StepCells` (default 1), `LargeStepCells` (default 4, i.e. 2 m;
provisional), `KeyRepeat` (initial delay and rate, so holding a key walks the ghost),
`StayLockedAfterPlace` (default 1), `Log` (default 1). Added during implementation so that
test findings can be acted on without a rebuild: `FlipLeftRight`, `SnapOnLock` (the fallback
for a ghost that jumps at lock time), `Trace` (the once-a-second cursor line of milestone 2;
default 1 until release) and `StartDelayMs`.

Building Tools binds F5 to F11, PageUp/PageDown (free placement farther / closer), Home (its
in-game menu) and Q+wheel, and Shroudforge uses F9/F10. PageUp/PageDown therefore collide
with our vertical nudge when both mods are loaded. Users running several mods will need to
rebind; note this in the README.

### Where the control scheme comes from

Checked on 2026-11-17 against Infinite Nudge 3.1.0 source
(https://github.com/mklierman/SatisfactoryMods, `InfiniteNudge/`, commit `9b12b47`). The
key bindings were read out of `Content/Inputs/MC_InfiniteNudge_MappingContext.uasset`.

Infinite Nudge does not define the lock or nudge keys. Those are vanilla Satisfactory:
`H` locks and unlocks the hologram, the arrow keys nudge horizontally relative to the view,
and `PageUp` / `PageDown` nudge vertically. The mod hooks the vanilla nudge distance and
adds:

| Infinite Nudge control | Default | Here |
|---|---|---|
| Tiny step modifier (20 cm) | `Left Ctrl` | Not carried over: below the 0.5 m grid |
| Small step modifier (50 cm) | `Left Alt` | Not carried over: below the 0.5 m grid |
| Large step modifier (250 cm) | `Left Shift` | `Left Shift` = `LargeStepCells` |
| Cycle scroll mode (rotate / scale axes) | tap `Z` | Not carried over: no rotation or scale |
| Mode menu, including "Reset Transform" | hold `Z` | Not carried over: no reset; unlock and re-lock |
| Rotate or scale in the current mode | mouse wheel | Not carried over |
| Accept | `\` | Not carried over |
| Repeat while a nudge key is held | `NudgeRepeatDelay` | `KeyRepeat` |

The modifier is sampled at the moment of each nudge step, as Infinite Nudge does. Unmodified
step is 100 cm there and one cell here.

Two cautions:

- The mod's ficsit.app page describes the modifiers differently (Left Shift = 50 cm, Right
  Shift = 250 cm). The shipped mapping asset is what is listed above; it last changed in
  June 2026, before the September 2026 release.
- `Left Shift` is sprint in Enshrouded. We poll and do not swallow keys, so the game still
  sees it. That should be harmless while standing and tapping arrows; confirm in milestone 5.

With `StayLockedAfterPlace=1` the lock and offset survive a click, so the player can place,
nudge, place, nudge. This is the main use case for building runs.

Auto-unlock when the hammer has been put away: if the hook has not seen a `query[3] == 4`
call for ~1 s, clear the lock.

### Input

A polling thread with `GetAsyncKeyState`, edge detection and key repeat (the large-step
modifier is read with each step, not edge-detected), active only while
the game window is foreground (compare the foreground window's process id to ours). No
low-level keyboard or mouse hooks in v1.

The hook runs on game threads and input on ours. Share state through atomics or a small
lock; the hook must never block.

### Feedback

v1: the ghost itself, plus log lines. No overlay. An on-screen indicator ("LOCKED +2 / 0 /
-1") is a later nice-to-have and must not hold up v1.

## Architecture

- **Proxy DLL** named `dinput8.dll`, placed next to `enshrouded.exe`. Export
  `DirectInput8Create` and forward it to the real DLL loaded explicitly from
  `%SystemRoot%\System32\dinput8.dll`.
- **Chain-loadable.** Start everything from `DllMain` (create a thread and return; do no
  real work under the loader lock), so the DLL also works when renamed and loaded by
  another loader such as the XHL Native Core's `dinput8-chain.ini` or Shroudforge. Keep the
  ini and log next to `enshrouded.exe` regardless of the DLL's own name.
- **Init thread.** Wait for start-up to finish before scanning (upstream sleeps 4 s to let
  the Steam stub unpack). Scan the `.text` section of the main module for the signature.
  Require exactly one match.
- **Already-hooked case.** If Building Tools or FreePlace loaded first, the function's
  first 5 bytes are a jump and the signature will not match. Retry with the first 5 bytes
  as wildcards and accept a single match that begins with a jump (`E9`, or `FF 25`).
  MinHook can hook over a leading relative jump. Two mods that both post-hook this function
  will fight over the output while both are active; document that Building Tools' free
  placement should be off while a lock is held.
- **Hooking.** MinHook, fetched at a pinned release tag and linked statically.
- **Fail closed.** No match, several matches, or a MinHook error: install nothing, write
  the reason to the log, and leave the game vanilla.
- **Guard reads.** Check pointers for null and keep the hook body tiny. A fault here
  crashes the game.

### Layout

```
src/proxy.cpp      DirectInput8Create forwarder, DllMain
src/scan.{h,cpp}   pattern scanner over .text
src/hook.cpp       MinHook setup, cursor hook
src/nudge.{h,cpp}  pure logic: state, axis mapping, fixed-point maths
src/keys.{h,cpp}   pure: key names for the ini
src/shared.h       the state the hook and the input thread exchange without waiting
src/input.cpp      polling thread
src/config.cpp     ini
src/log.cpp        BlockNudge.log (previous session kept as .prev.log)
tests/             unit tests for nudge.cpp and scan.cpp, no game needed
```

Keep `nudge.cpp` free of Windows and game dependencies so it is unit-testable: the state
machine, the camera-to-axis mapping (all four quadrants plus the diagonals), offset
arithmetic at large coordinates and negative coordinates, the large-step modifier, and key
repeat timing.

### Build

Windows x64 only. C++17, CMake, MSVC or clang-cl, static CRT (`/MT`) so there is nothing to
redistribute. Test builds are cross-compiled on the Mac with MinGW-w64 (`cmake --preset
mingw`); that build links the Universal CRT that ships with Windows 10 and 11, which also
needs nothing redistributed. A `package_zip` target (`cmake --build --preset package`) that zips `dinput8.dll`, a default `BlockNudge.ini`,
`README.md`, `LICENSE`, and `THIRD-PARTY-NOTICES.md`.

## Milestones

You cannot run the game; the user tests each build by hand and pastes the log back. Make
every milestone produce log output that proves it worked.

1. **Proxy loads.** The game starts normally with our `dinput8.dll` present; the log shows
   the module base and `.text` range.
2. **Signature and pass-through hook.** Log the match RVA. The hook calls the original and
   changes nothing. Log (rate-limited, about once per second) `query[3]`, `valid`, `pos` in
   metres, `rot`, camera position and `fwd`. Game behaviour is unchanged.
3. **Lock.** Lock holds the ghost in place while the camera moves. Check: no jump at lock
   time, the ghost stays lit and valid, and a click places the block there.
4. **Nudge.** World-axis nudges first, then camera-relative. Confirm left/right handedness.
5. **Polish.** Ini, key repeat, large-step modifier, stay-locked-after-place, auto-unlock,
   on/off switch.
6. **Coexistence.** Chain-loaded start; start with Building Tools already hooked.
7. **Release.** README (install, keys, compatibility, credits), licences, zip.

## Things to find out by testing

Write the answers into the README as they come in.

- Does the ghost jump at lock time (raw replay), or do we need to snap ourselves?
- Is there a reach limit? Lock, walk away, click: at what distance does placement stop
  working, and does the game say anything when it does?
- Outside the flame altar area: does the game refuse as it would normally?
- Right-click removal while locked: does it target the locked position? Decide whether
  that is a feature or whether the override should apply to placement only.
- Large shapes (2 m blocks, prefab pieces): does a one-cell nudge behave sensibly, and
  would a "step by one shape size" option be worth adding?
- Does vanilla snapping mode (the game's own snap toggle) interact with a locked cursor?
- Multiplayer: joined to a player-hosted game and to a dedicated server, with the mod on
  the client only. Does the block appear for everyone, and does anything desync?

## Definition of done for v1

- Lock, nudge on three axes, place, repeat, unlock, all with the hammer and any block shape.
- Costs, undo and the build sound behave exactly as in vanilla.
- With the mod switched off in the ini, or with the signature missing, the game is vanilla.
- A game update that breaks the signature produces one clear log line and no crash.
- Unit tests pass for the pure logic.