# Coming from an older version

Usually there is nothing to do but install the new version over the old one ([INSTALL.md](INSTALL.md#updating-to-a-new-version)):
your settings, saves, records and achievements are kept, and files an old version had that the new one dropped are
removed at the next start. This page lists the few cases where something from an older version needs a look.

## Every update
- **Close the game first**, then run the installer again (`install-steam.bat` on Windows, `bash install-steam.sh` on
  Linux / Steam Deck). The loader sits in a second folder, so copying the files alone doesn't update it; the title
  screen says so when the loader and the mods don't match.
- **Co-op:** every player needs the same version. The co-op screen says when the mods differ.
- **Settings:** a setting you never changed moves to the new default when a version changes one, and
  `mods/remastered-mod.log` lists each move (`settings: ... (the new default; it was the old one)`). Values you chose
  yourself stay.

## From the versions that had you copy files into the game folder (0.3.0 and older, Windows)
The Windows installer now works from the unzipped download: double-click `install-steam.bat` there, and it finds the
game, copies the mods and puts the loader in place. Running it from the game folder, as before, still works.

Files the old way left in the game folder (Steam → right-click the game → **Manage → Browse local files**):
- `ddraw.dll`: not used (the loader that counts is the one next to the real game in
  `C:\ProgramData\PopCap Games\Insaniquarium`). Delete it, unless it belongs to another tool you put there yourself,
  such as a graphics wrapper.
- `install-steam.bat`: an old copy of the installer; delete it.
- The `mods` folder: keep it. It holds your settings (`remastered-mod.ini`) and the log.

To uninstall, double-click `uninstall-steam.bat` in the unzipped download.

## From 0.2.1 and older: the Fullscreen window choice
0.2.2 replaced the Fullscreen choice with **Borderless** (it fills the screen without a screen-mode change). A
`window=fullscreen` line kept from an older version still starts the game in the old fullscreen mode, but the settings
page no longer offers it. To move to the new way: **F2** → **Display** → **Window**: **Borderless** (or **Large** for a
window).

## From 0.2.2 and older: the installer closed at once (Windows)
In the default Steam folder (`Program Files (x86)`), `install-steam.bat` from 0.2.2 and older closed without
installing. If you copied `ddraw.dll` into `C:\ProgramData\PopCap Games\Insaniquarium` by hand because of that, nothing
changes: running the new installer replaces that copy.

## From 0.1.0
0.1.0 had no list of its own files, so a file it installed that a later version dropped would stay in `mods`; no
version since has dropped one of 0.1.0's files, so there is nothing to clean up.

## Going back to an older version
Uninstall, then install the older version the way its own INSTALL.txt says. Your settings file stays. Saves are the game's own and work with any version, or without the mod. The save backup
made at the first start (`userdata-before-remastered-mod`, next to the game's `userdata`) is the state before any
version of the mod ran.
