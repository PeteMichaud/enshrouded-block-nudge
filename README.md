# BlockNudge

Satisfactory-style lock and nudge for block placement in Enshrouded (PC, Steam).

1. Aim a block with the Construction Hammer as usual.
2. Press **H**. The ghost stops following your aim and stays where it is.
3. Tap the arrow keys and Page Up / Page Down to move it one grid cell (0.5 m) at a time.
4. Left click places it. Press **H** again to release.

That is all it does. It only changes where the cursor is: costs, undo and placement are the
game's own.

> **Status: untested in the game.** The code builds and its unit tests pass, but nothing
> here has been confirmed in Enshrouded yet. See "Testing status" below.

## Install

Copy `dinput8.dll` and `BlockNudge.ini` into the game folder, next to `enshrouded.exe`.
To uninstall, delete them.

From a checkout of this repository on Windows, `install.cmd` does the copying: it finds the
game through Steam and installs the prebuilt `dist\dinput8.dll`. `install.cmd -Uninstall`
removes it again.

If another mod already uses `dinput8.dll` there, rename BlockNudge's DLL (for example
`BlockNudge.dll`) and load it with that mod's loader instead, such as an entry in the XHL
Native Core's `dinput8-chain.ini`. The ini and the log stay next to `enshrouded.exe`.

## Keys

| Action | Default |
|---|---|
| Lock / unlock the ghost | `H` |
| Nudge forward / back / left / right | arrow keys |
| Nudge up / down | `Page Up` / `Page Down` |
| Large step (4 cells = 2 m), held while nudging | `Left Shift` |

Forward, back, left and right follow the camera: forward is whichever horizontal grid
direction you are mostly facing. Holding a nudge key repeats. To start over, unlock and lock
again.

The keys are the ones Satisfactory uses for its hologram lock and nudge, with the large-step
modifier from its Infinite Nudge mod.

The lock is released by itself when you put the hammer away.

## Settings

Everything is in `BlockNudge.ini`, with a comment on each setting. Restart the game after a
change. `Enabled=0` turns the mod off completely.

## Compatibility

- The mod finds the game's code by a byte signature, not by address. After a game update
  that changes that code, it installs nothing, writes one line to `BlockNudge.log`, and the
  game runs as vanilla.
- **Enshrouded Building Tools:** its free placement moves the same cursor. Keep free
  placement off while a lock is held. Its defaults also use `Page Up` / `Page Down` (free
  placement distance), so rebind one of the two mods if you use both.
- **Shroudforge** and other mods with their own hotkeys: rebind in `BlockNudge.ini` if they
  collide.
- The keys are read while the game window is in front, including while you type in chat.
- Client only. It is not meant for the dedicated server and does nothing there.

## Troubleshooting

`BlockNudge.log` is written next to `enshrouded.exe` (the previous session is kept as
`BlockNudge.prev.log`). It says whether the game's cursor function was found and hooked, and
logs every lock, unlock and nudge.

## Testing status

How to test a build is in [TESTING.md](TESTING.md). To be filled in as each is checked in
the game:

- [ ] The game starts with the DLL present and the log shows the module base and `.text` range.
- [ ] The signature matches the installed game version.
- [ ] Locking does not make the ghost jump (if it does: `SnapOnLock=1`).
- [ ] Left and right are the right way round (if not: `FlipLeftRight=1`).
- [ ] A click places the block at the locked position.
- [ ] Reach limit when walking away from a locked ghost.
- [ ] Behaviour outside the flame altar area.
- [ ] Right-click removal while locked.
- [ ] Large shapes and prefab pieces.
- [ ] The game's own snapping mode with a locked cursor.
- [ ] `Left Shift` (sprint) and `H` against the game's own bindings.
- [ ] Multiplayer, player-hosted and dedicated server.
- [ ] Loaded alongside Enshrouded Building Tools.

## Building

Windows x64 only. CMake 3.21 or later; MinHook is fetched at a pinned commit.

```
# Unit tests, on any machine
cmake --preset tests && cmake --build --preset tests && ctest --preset tests

# The DLL, cross-compiled with MinGW-w64 (macOS: brew install cmake ninja mingw-w64)
cmake --preset mingw && cmake --build --preset mingw        # build/mingw/dinput8.dll
cmake --build --preset package                              # the release zip, and refreshes dist/dinput8.dll

# The DLL and tests with Visual Studio 2022 on Windows
cmake --preset msvc && cmake --build --preset msvc
```

`dist/dinput8.dll` is the committed MinGW build that `install.cmd` installs; rebuild it with
the `package` preset before committing a code change.

The MinGW build links the Universal C Runtime that ships with Windows 10 and 11; the MSVC
build links the C runtime statically. Neither needs anything installed alongside it.

## Credits

The reverse engineering this mod depends on (the cursor function's signature, the layout of
its arguments, and the idea of overriding its result) comes from
[Enshrouded Building Tools](https://github.com/AleksanderWojtal-CC/Enshrouded-BuildingTools)
by Aleksander Wojtal, MIT licence. If you want area fill, replace, blueprints or free
placement, use that mod.

Hooking is done with [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu; see
`THIRD-PARTY-NOTICES.md`.

BlockNudge is MIT licensed; see `LICENSE`.
