# Troubleshooting

Find your problem by what you see. Each entry says why it happens, what to do, and what the log says.

**The log:** everything Insaniquarium - Remastered Mod does is written to **`mods/remastered-mod.log`** in the game
folder. Look there first. When you report a problem, attach it (in co-op, every player's log).

**First check:** the title screen should say "Remastered Mod is on". If you start the game and it does not, go to the
first entry below. To play once without any mod, hold **Shift** while the game starts.

## The game or the title screen

### No "Remastered Mod is on" message, and no log file
- **Why:** the loader (`ddraw.dll`) is not being used.
- **Windows:** the installer copies `ddraw.dll` to `C:\ProgramData\PopCap Games\Insaniquarium`. Did `install-steam.bat`
  say *installed*? If not, run it again.
- **Linux / Steam Deck:** run `install-steam.sh` again, or set the launch option `WINEDLLOVERRIDES="ddraw=n,b" %command%`.

### The log says "not the Insaniquarium Deluxe 1.1 game these mods are for"
- **Why:** this is a different version of the game. The mod stays off on purpose, and the game runs as normal.
- **Fix:** none needed. Update the mod when a new version supports your game build ([GAME-UPDATES.md](GAME-UPDATES.md)).

### The log says "safe mode"
- **Why:** Shift was held while the game started, or the settings file has `[core] enabled=0`.
- **Fix:** start the game again without Shift, or set `enabled=1`.

### A message says a mod "crashed and was switched off"
- **Why:** one mod failed. It stays off until the next start. The rest keep working.
- **Log:** it names the mod and where it failed. Please report it with the log.

### The game doesn't start at all
- **Fix:** remove the loader. On Windows, double-click `uninstall-steam.bat`. On Linux, run
  `install-steam.sh --uninstall`. Then open an issue and say what the end of the log said.

## The window

### The window is another size, with black bars, on a tiling desktop (niri, sway, Hyprland...)
- **Why:** the desktop sizes windows itself. The mod leaves that choice alone and scales the game into the size it gets.
- **Fix:** for the whole screen, pick **Borderless** in the Display tab, or use your desktop's own fullscreen.

### The window isn't the size you chose
- **Why:** the game opens in a large window by default.
- **Fix:** in the Display tab, pick **640x480** or **Borderless**. In the settings file the values are
  `[display] window=normal` and `borderless`. It changes at once.
- **Log:** `native window: ...` for Large and Borderless, `display: A -> B` for each change, and
  `display: switching the game to ...` when the game came up in the other screen mode.

### The Windows arrow shows instead of the game's cursor
- **Why:** this only happens in the **Large** or **Borderless** window, when the game's cursor pictures could not be read.
- **Log:** "couldn't make the game's cursors".

## Keys and tools

### Speed keys do nothing
- **Why:** they only work inside a tank, and the game window must have focus. In co-op only the host changes the speed
  for everyone. A guest sees "Only the host controls the speed in co-op".
- **Fix:** check the keys in `remastered-mod.ini`, or in the settings page under **Keys**.

### The screensaver closes straight away
- **Why:** moving the mouse or pressing a key ends it, as with any screensaver.
- **Fix:** keep still for a second after starting it.
- **Log:** `screensaver: ended after ...` says how it ended.

## Co-op

### Co-op says "Out of sync"
- **Why:** the players' games no longer match. The host sends everyone its game, and you carry on from there ("Back in
  step with the host"), a few moments later than you were.
- **If it keeps happening:** report it with every player's `remastered-mod.log`. It records what differed. The usual
  cause is different mod versions.

### Co-op: "The game is full"
- **Why:** the game already has four players. Joining a game in progress works while there is room.

### A match in the list is greyed out
- **Why:** its mod version or mod list differs from yours. Co-op only joins the same version.
- **Fix:** update the mod, or ask the host which version they use.

More: [INSTALL.md](INSTALL.md) has install problems, [KNOWN_ISSUES.md](../KNOWN_ISSUES.md) lists what does not work
yet, and you can [open an issue](https://github.com/undecuple/insaniquarium-remastered/issues/new/choose).
