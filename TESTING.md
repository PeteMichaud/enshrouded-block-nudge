# Testing BlockNudge

Nothing can test the hook without the game, so every build is checked by hand in the game.
This is the routine. It takes about five minutes.

## 1. Install

From a checkout, with the game closed:

```
install.cmd -ResetIni
```

Then set `Trace=1` in the game folder's `BlockNudge.ini`, so the log records what the game's
cursor is doing each second.

`install.cmd` finds Enshrouded through Steam and copies `dist\dinput8.dll` next to
`enshrouded.exe`, plus `BlockNudge.ini` if the game folder has none. It prints the game
folder and where the log will be. Options:

| Option | What it does |
|---|---|
| `-GameDir "path"` | Use this folder instead of looking through Steam |
| `-ResetIni` | Replace the game folder's `BlockNudge.ini` with the default |
| `-Force` | Replace a `dinput8.dll` that belongs to another mod |
| `-Uninstall` | Remove the DLL and the ini; the game is vanilla again |

It refuses to run while the game is open, and it will not overwrite another mod's
`dinput8.dll` unless told to.

## 2. Run the session

Do these in order. Each step proves one milestone, so stop at the first one that fails.

1. **Start the game** and load a world. If the game does not start, or crashes, that is the
   finding: run `install.cmd -Uninstall` and send the log.
2. **Take out the Construction Hammer** with a block shape selected. Aim at the ground or a
   wall for a few seconds, so the log collects a few `trace` lines.
3. **Press `H`**, then move the camera around. The ghost should stay where it was. Watch
   for whether it jumped at the moment you pressed `H`, and whether it stays lit.
4. **Tap each nudge key once**: the four arrow keys, `Home`, `End`. The ghost should
   move one cell (0.5 m) each time. Check that left and right go the way you expect, then
   turn the camera a quarter turn and check again.
5. **Hold `Left Shift` and tap an arrow**: the ghost should move four cells (2 m).
6. **Hold an arrow key**: the ghost should walk after a short pause.
7. **Left click** to place. The block should appear where the ghost was, at normal cost,
   and the ghost should follow your aim again.
8. **Lock again, then press `H`** to unlock without placing. The ghost should follow your
   aim again.
9. **Put the hammer away** while locked (lock first, then switch tool). Take it out again:
   the lock should be gone.

## 3. Report

Send back:

- `BlockNudge.log` from the game folder (next to `enshrouded.exe`).
- For steps 3 to 9, what you saw wherever it was not what the step says.

The log alone answers steps 1 and 2: it shows whether the DLL loaded, whether the game's
cursor function was found, and what the cursor looked like each second.

## Quick fixes that need no new build

Edit `BlockNudge.ini` in the game folder and restart the game.

| Symptom | Setting |
|---|---|
| The ghost jumps when you lock | `SnapOnLock=1` |
| Left and right are swapped | `FlipLeftRight=1` |
| A key collides with the game or another mod | Rebind it under `[Keys]` |
| The game misbehaves and you want it vanilla without uninstalling | `Enabled=0` |

## Later checks

Once the session above passes, these are the open questions. Each answer goes
into the checklist in `README.md`.

- **Reach:** lock, walk away, click. At what distance does placing stop working, and does
  the game say anything?
- **Flame altar:** lock a ghost outside the altar's area. Does the game refuse as usual?
- **Removal:** right click while locked. Does it remove at the locked position?
- **Large shapes:** try a 2 m block and a prefab piece. Is a one-cell nudge sensible?
- **Snapping mode:** toggle the game's own snapping with a locked ghost.
- **Key clashes:** does `H` or `Left Shift` (sprint) do anything unwanted while building?
  Does `H` typed into chat toggle the lock?
- **Multiplayer:** join a player-hosted game and a dedicated server with the mod on your
  client only. Does the block appear for everyone?
- **Building Tools:** install it alongside (see "Install" in `README.md` for chain loading)
  and check that both mods' logs show their hooks installed.
