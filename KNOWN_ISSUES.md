# Known issues

What does not work yet, or works only in some places. For fixing a problem, see
[docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md). What was tested, and on what, is in the README's
[Status](README.md#status) table. A new problem: [open an issue](https://github.com/undecuple/insaniquarium-remastered/issues/new/choose).

## Not tested yet

- **Windows 10/11.** The mod is built for it, but nobody has tested it on a real Windows PC. Reports are welcome.
- **Co-op over Steam between two Steam accounts.** It works in the tests so far, but those did not use two accounts.

## Limits

- **Only one build of the game.** The mod checks the game's code when it starts and stays off with any other version
  ([docs/GAME-UPDATES.md](docs/GAME-UPDATES.md)). If Steam updates the game, the game runs as normal until the mod is
  updated.
- **Co-op needs the same mod version for every player.** Version 0.4.0 and 0.4.1 players cannot play together. The
  online list shows each match's version and greys out the ones you cannot join.
- **Co-op over Steam needs Valve's `steam_api.dll`.** It is not in the download. The mod usually finds a copy in
  another Steam game ([docs/STEAM.md](docs/STEAM.md)).
- **HD sprites are not planned.** The game draws in 640x480; the large window scales that picture up.

## Small things

- **Tiling desktops (niri, sway, Hyprland) size the window themselves,** so you may see black bars. Pick **Borderless**
  in the Display tab to fill the screen.
- **The Windows arrow can show instead of the game's cursor** in the Large or Borderless window, when the game's
  cursor pictures could not be read. The log says "couldn't make the game's cursors".
- **Some antivirus programs distrust DLLs that change how a game behaves.** That is a false alarm; you can build the
  mod yourself ([BUILDING.md](BUILDING.md)).
