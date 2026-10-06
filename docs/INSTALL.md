# Installing Insaniquarium - Remastered Mod

Step-by-step, for Windows and Linux (including Steam Deck). It takes a few minutes, changes none of the game's own
files, and can be undone at any time (see [Uninstalling](#uninstalling)).

## Before you start
- **Your game:** *Insaniquarium! Deluxe* from **Steam** ([store page](https://store.steampowered.com/app/3320/)),
  version 1.1 (shown on the right of the title screen). With any other build the mod simply stays off.
- **The download:** `Insaniquarium-Remastered-Mod-<version>.zip`. Unzip it anywhere you like (right-click →
  *Extract All* on Windows; *Extract here* on Linux). You get this folder:
  ```
  ddraw.dll   install-steam.bat   install-steam.sh   INSTALL.txt   README.txt   CHANGELOG.txt   LICENSES.txt
  mods\       (the mods and remastered-mod.ini, the settings)
  ```
- Optional: compare the zip with `SHA256SUMS.txt` from the download page (Windows: `certutil -hashfile <zip> SHA256`
  in a command prompt; Linux: `sha256sum <zip>`).

Which part of this guide to follow:

| Your game | Your computer | Section |
|---|---|---|
| Steam | Windows | [Windows: Steam](#windows-steam) |
| Steam | Linux or Steam Deck | [Linux: Steam](#linux-and-steam-deck-steam) |

---

## Windows: Steam
The Steam version starts the real game from a second folder (`C:\ProgramData\PopCap Games\Insaniquarium`), so one
file has to go there too. A small helper does that.

1. In Steam, right-click **Insaniquarium! Deluxe** → **Manage** → **Browse local files**. A folder opens (usually
   `C:\Program Files (x86)\Steam\steamapps\common\Insaniquarium Deluxe`).
2. Copy **`ddraw.dll`**, **`install-steam.bat`** and the **`mods`** folder from the unzipped download into that folder.
   Windows may ask for administrator permission because it's under *Program Files*: click **Continue**.
3. Double-click **`install-steam.bat`** in that folder. A window says
   `Insaniquarium - Remastered Mod installed: loader in C:\ProgramData\PopCap Games\Insaniquarium ...`.
   Press a key to close it.
4. Start the game from Steam as usual. The title screen says **"Insaniquarium - Remastered Mod is on (16 mods)"**.

If Windows SmartScreen or your antivirus complains about `ddraw.dll` or the `.bat`: see [Is it safe?](#is-it-safe).

---

## Linux and Steam Deck: Steam
The game runs through Proton. The helper script puts the mod in the game folder, the loader into the game's Proton
folder, and tells Proton to use it, so nothing has to be set in Steam.

1. **Run the game once** from Steam, get to the title screen, and quit. (This creates its Proton folder. If Steam
   doesn't offer to play it, pick a Proton version in the game's **Properties → Compatibility**.)
2. **Steam Deck only:** hold the power button → **Switch to Desktop**.
3. Open the unzipped download folder in your file manager, right-click an empty spot → **Open Terminal Here**
   (Steam Deck: Dolphin → *Tools*/right-click → **Open Terminal Here**, or start *Konsole* and `cd` into the folder).
4. Run:
   ```sh
   bash install-steam.sh
   ```
   It finds the game in your Steam libraries (normal Steam, the Flatpak version and extra library folders on other
   drives or the SD card) and ends with `installed. Start the game from Steam as usual`.
   If it says the game wasn't found, tell it where Steam is: `STEAM_DIR=/path/to/Steam bash install-steam.sh`.
5. Start the game from Steam (Steam Deck: you can go back to Gaming Mode). The title screen says
   **"Insaniquarium - Remastered Mod is on (16 mods)"**.

**If the message doesn't appear:** in Steam, right-click the game → **Properties** → **General** → **Launch Options**,
enter
```
WINEDLLOVERRIDES="ddraw=n,b" %command%
```
and start it again.

**Without the script** (if you prefer doing it by hand):
1. Copy the `mods` folder into the game folder (Steam → right-click the game → **Manage → Browse local files**).
2. Copy `ddraw.dll` into the game's Proton folder:
   `<Steam library>/steamapps/compatdata/3320/pfx/drive_c/ProgramData/PopCap Games/Insaniquarium/`
   (create the last two folders if they're missing; the first library is usually `~/.local/share/Steam`).
3. Set the launch option above.

---

## After installing
- **Did it work?** The title screen message, and the log `mods/remastered-mod.log` in the game folder, which lists
  every mod it loaded.
- **The mod's features:** the **Remastered** button at the bottom left of the main menu (co-op, extra modes,
  achievements, settings, the list of mods). Settings also open with **F2**.
- **Your saves** are copied once to `userdata-before-remastered-mod` (next to the game's `userdata`; for the Steam
  release that's `C:\ProgramData\Steam\Insaniquarium`) before the mod changes anything.
- **Keys:** **F2** → Keys: click a key to change it; right-click or **Reset all keys** for the defaults.
- **The window:** the game opens in its own 640x480 window. For a bigger one: **F2** → Display → Window: **Large** (as
  big as your screen allows, sharp, resizable), **Borderless** (fills the screen) or **Fullscreen**; restart the game.
- **Screensaver:** **F11** on the main menu (or **Open** on the Mods page) shows your Virtual Tank full-screen; move
  the mouse to come back.
- **Playing without mods for once:** hold **Shift** while the game starts.

### Co-op
- **In a game and its lobby:** **Enter** opens a chat line that only the players in that game see (Enter sends, Esc
  drops; lines fade after 10 seconds; the key can be changed on the Keys tab); a **middle click** shows everyone where you point. Only the host changes the speed and
  the rules (mutators): the guests' games follow.
- **Online (the default):** nothing to set up. The co-op screen's **Online** tab uses the mod's public server: host a
  listed or private game, or join one from the list or with its room code.
- **Over Steam:** needs Valve's `steam_api.dll`. Usually nothing to do: the mod borrows a copy another Steam game of
  yours already has. If the co-op screen says none was found, put a 32-bit `steam_api.dll` into the game folder's
  `mods\coop\` (for example from the `Windows-x86` folder of a
  [Steamworks.NET standalone release](https://github.com/rlabrecque/Steamworks.NET/releases)).
- **Through your group's own server:** co-op screen → **Online** → **Server settings**: enter the server's name, port,
  TLS and key you were given, then **Test connection**. Setting up such a server: [SERVER.md](SERVER.md).
- **By address:** nothing to install; the host opens TCP port 27615 (or uses a VPN), see the README.

## Updating to a new version
Install the new version the same way, over the old one (let it replace the files). Your settings
(`mods/remastered-mod.ini`) are kept on Linux; on Windows, keep your own copy of that file if you changed it and put it
back afterwards. Steam on Windows: run `install-steam.bat` again.

## Uninstalling
Your profiles keep working without the mod.
- **Windows, Steam:** in the game folder, open a command prompt (type `cmd` in the folder's address bar and press
  Enter) and run `install-steam.bat uninstall`. Then delete `ddraw.dll`, `install-steam.bat` and the `mods` folder from
  the game folder.
- **Linux/Steam Deck, Steam:** in the unzipped download folder, `bash install-steam.sh --uninstall`; then delete the
  game folder's `mods` folder if you don't want the settings and log either. Remove the launch option if you set one.

The save backup (`userdata-before-remastered-mod`) can be deleted once you're happy.

## Problems
| What happens | What to do |
|---|---|
| No "Remastered Mod is on" message, no `mods/remastered-mod.log` | The loader isn't used. Windows: did `install-steam.bat` say *installed*? Linux: run the script again, or set the launch option / DLL override. |
| The log says "not the Insaniquarium Deluxe 1.1 game these mods are for" | A different version of the game: the mod stays off on purpose. |
| The game doesn't start any more | Uninstall as above (or hold Shift while starting), and look at the end of `mods/remastered-mod.log`. |
| A message says a mod "crashed and was switched off" | That mod is off until the next start; the log says where it failed. Please report it with the log. |
| `install-steam.sh` says "No Proton prefix" | Start the game once from Steam first (step 1). |
| Co-op says "too many lobbies for this account" | Only older versions of the mod: update. The game now clears its own old listings by itself. |
| The screensaver closes straight away | Moving the mouse or pressing a key ends it, as any screensaver; keep still for a second after starting it. The log says how it ended (`screensaver: ended after ...`). |

More in the README's *Troubleshooting* section.

## Is it safe?
The mod is a set of DLLs that run inside the game, like most game mods; it never changes the game's own files.
Antivirus programs sometimes distrust DLLs that change a game's behaviour; that's a false alarm, but only install the
mod from a source you trust. Every DLL carries version information (right-click → **Properties** → **Details**), and
the download's checksum (`SHA256SUMS.txt`) shows the zip is the one that was published.
