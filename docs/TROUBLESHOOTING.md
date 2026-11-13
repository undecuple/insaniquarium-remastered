# Troubleshooting

Everything Insaniquarium - Remastered Mod does is written to **`mods/remastered-mod.log`** in the game folder. Look there first.

| Problem | What to do |
|---|---|
| No "Remastered Mod is on" message on the title screen, no log file | The loader isn't being used. Windows: did `install-steam.bat` say *installed* (it copies `ddraw.dll` to `C:\ProgramData\PopCap Games\Insaniquarium`)? Linux: run `install-steam.sh` again, or set the launch option `WINEDLLOVERRIDES="ddraw=n,b" %command%`. |
| Log says "not the Insaniquarium Deluxe 1.1 game these mods are for" | A different version of the game: Insaniquarium - Remastered Mod stays off on purpose. |
| Log says "safe mode" | Shift was held while the game started, or `[core] enabled=0`. |
| A message says a mod "crashed and was switched off" | That mod is off until the next start; the log names it and where it failed. Please report it. |
| The game doesn't start at all | Remove the loader (Windows: double-click `uninstall-steam.bat`; Linux: `install-steam.sh --uninstall`) and tell us what the log said (open an issue). |
| On a tiling desktop (niri, sway, Hyprland...) the window is another size, with black bars | The desktop sizes windows itself; the mod leaves its choice alone and scales the game into whatever size it gets. For the whole screen pick **Borderless** (or your desktop's own fullscreen for the window). |
| The window isn't the size you chose | The game is a large window by default: pick **640x480** or **Borderless** in the Display tab (or `[display] window=normal`, `borderless`); it changes at once. The log says "native window: ..." for Large and Borderless, "display: A -> B" for each change, and "display: switching the game to ..." when the game came up in the other screen mode. |
| Speed keys do nothing | They only work inside a tank, and the game window must have focus. In co-op only the host changes the speed (for everyone); a guest gets "Only the host controls the speed in co-op". Check the keys in `remastered-mod.ini`. |
| The screensaver closes straight away | Moving the mouse or pressing a key ends it, as any screensaver; keep still for a second after starting it. The log says how it ended (`screensaver: ended after ...`). |
| The Windows arrow shows instead of the game's cursor | Only in the **Large** or **Borderless** window, if the game's cursor pictures couldn't be read: the log says "couldn't make the game's cursors". |
| Co-op says "Out of sync" | The players' games no longer match; the host sends everyone its game and you carry on from there ("Back in step with the host"), a few moments later than you were. If it keeps happening, report it with every player's `remastered-mod.log` (it records what differed): usually different mod versions. |
| Co-op: "The game is full" | The game has four players already (joining a game in progress is fine while there is room). |
