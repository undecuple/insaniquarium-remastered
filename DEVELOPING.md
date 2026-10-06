# Insaniquarium - Remastered Mod: developer guide

Building, testing and writing mods. The player guide (features, settings) is [README.md](README.md); installing: [docs/INSTALL.md](docs/INSTALL.md).
The Mods page and how mods tie into it: [docs/MOD-MANAGER.md](docs/MOD-MANAGER.md).
Plans: [docs/ROADMAP.md](docs/ROADMAP.md) (features, phases),
[docs/STEAM.md](docs/STEAM.md) (the Steam release, Steam for co-op), [docs/GAME-UPDATES.md](docs/GAME-UPDATES.md) (each mod's game dependencies; what to do when the game is updated:
`tools/check-game.py`), [docs/SERVER.md](docs/SERVER.md) (hosting a
Nakama co-op server; files in `server/`).

Status: all 16 mods working (see [docs/ROADMAP.md](docs/ROADMAP.md)): the loader, hooks, mod API, overlay on every screen,
crash containment, safe mode and save backup, the window choices, online co-op through the public server. Tested under
Wine, Proton-GE and the Steam release from the real Steam client (Proton Experimental, on a Wayland desktop); not yet on
Windows itself.

## How it works
`ddraw.dll` is a proxy: the game loads DirectDraw from its own folder first, and every DirectDraw call is passed to
the system's real `ddraw.dll`. On load the core checks the exe and hooks a few game functions with MinHook; the game's
first `DirectDrawCreate` (main thread, before resources load) loads every `mods/*.dll` and calls its
`RemodInit`. Mods get a small C API (`include/remod.h`): hooks, callbacks (game update, tank draw, overlay on
every screen, keys), drawing with the game's own fonts, toasts, settings, a log. Game addresses and field offsets come
from reverse-engineering the exe (`include/game.h`).

A `winmm.dll` proxy (the game imports winmm directly, so it loads even earlier) was tried first and dropped: under Wine
the real winmm loads msacm32, which calls winmm, i.e. the half-loaded proxy, and the game crashes in its BASS setup.

## Building
```sh
./build.sh                 # core + every mod -> build/dist (fetches MinHook into third_party/ on first run)
./build.sh timecontrol     # core + one mod
```
Cross-compiles with 32-bit mingw-w64 (`pkgsCross.mingw32` from nixpkgs; `build.sh` enters a nix-shell if the compiler
isn't on PATH; on Debian/Ubuntu `tools/ci-setup.sh` installs it). Output DLLs are self-contained (static
libgcc/libstdc++) and reproducible (no link timestamps).

**Releasing:** bump `REMOD_VERSION` and add the changelog entry in `include/version.h`, commit, then
`git tag v<version> && git push origin v<version>`: `.github/workflows/release.yml` builds the zip and publishes a
GitHub release with the zip, `SHA256SUMS.txt` and that version's changelog. Every push and pull request is built by
`.github/workflows/build.yml` (the zip is kept as an artifact). By hand: `tools/package.sh` (zip + `SHA256SUMS.txt`
in `build/release/`), `tools/release-notes.sh <version>` (the release text).

**Dependency updates:** `renovate.json` lets Renovate (the free Mend Renovate app on GitHub: install it for the
repository, no workflow needed) open pull requests each Monday for the GitHub Actions, the co-op server images in
`server/` and the MinHook and Steamworks.NET versions pinned in `tools/fetch-deps.sh` (`# renovate:` comments). The
CI image stays put (it fixes the compiler, so releases stay reproducible), Postgres major versions are never
proposed (they need a data migration), and nothing merges itself: the build workflow checks each pull request; try
the build in the game before merging.

## Testing without Windows
```sh
GAME=/path/to/Insaniquarium tools/run-wine.sh wait:20 focus click:305:430 wait:4 click:388:206 wait:3 click:305:430 wait:5 \
  click:464:85 wait:6 click:320:430 wait:6 key:F8 shot:/tmp/fast.png key:F5 shot:/tmp/paused.png
```
Runs a copy of your game folder (`GAME=...`, copied once to `$TESTDIR`, default `/tmp/remod-test`; the original is never
touched) with `build/dist` installed, under
Wine on a hidden X display (Xvfb: no window, no focus stealing), driven by steps: `wait:S`, `focus` (the game only runs
while its window is active), `click:X:Y` (game coordinates), `key:F5`, `type:TEXT`, `shot:FILE.png`, `info`. Prints
`mods/remastered-mod.log`. `NOMODS=1` runs the unmodded game for comparison; `INI="display.window=native"` sets
settings for the run (`section.key=value ...`; `SCREEN=1280x720x24` sets the display size). Other Wines: `WINE=/path/to/bin/wine`;
Proton: `PROTON=~/.local/share/Steam/compatibilitytools.d/Proton-GE\ Latest TESTDIR=~/.cache/remod-proton`;
the Steam release: add `GAME=<a copy of its folder> STEAMBUILD=1 SteamAppId=3320` (loader installed into the test
prefix's ProgramData like `install-steam.sh` does)
(through `proton run` inside `steam-run`). The runner starts the game windowed (registry `ScreenMode=0`; `WINDOWED=0`
keeps the game's own setting): fullscreen under Proton is a scaled fake mode where headless input doesn't work.
Under Proton in Xvfb, mouse presses also never reach the borderless window (a window exactly the size of the screen;
motion arrives, presses don't; on a real desktop it works), and the Steam release may not start at all: keep Proton runs
at `window=normal` (the default), or use `REALDISPLAY=:0` to run on your own screen instead of a hidden one (the window
shows and takes the focus; sound stays off). `DIST=FOLDER` installs another build than `build/dist` (e.g. an unpacked
release, for a clean-install test); `WM=/path/to/openbox` runs a window manager in the hidden display (needed to see the
Large window's frame and placement).
First-run screens: the "check for updates" question (No = 388,206), then the
name box.

## Writing a mod
A mod is a 32-bit Windows DLL in the game's `mods\` folder that exports one function, `RemodInit`. The loader calls it
once at start-up, on the game's main thread, before the game loads its resources; the mod registers callbacks and hooks
there and returns 1 (or 0 to stay out, e.g. when the core is too old).

```c
#include "remod.h"
static const RemodApi* api;
static void Overlay(void* g) { if (api->board()) api->draw_text(g, "Hello", 10, 470, 0xffffffff); }
__declspec(dllexport) const char* RemodDescribe(void) { return "Says hello in the corner of the tank."; }
__declspec(dllexport) int RemodInit(const RemodApi* a)
{
    if (a->version < REMOD_API_VERSION) return 0;   // or check only the version your mod needs
    api = a;
    api->on_overlay(Overlay);
    return 1;
}
```

**Start from an example:** [`examples/hello`](examples/hello/hello.cpp) (toast, overlay, a setting, the Mods page) or
[`examples/template`](examples/template/template.cpp) (every callback stubbed, a hook, settings, persistence). Copy
one to `mods/<name>/<name>.cpp` (the file name is the mod's name) and run `./build.sh <name>`: the DLL lands in
`build/dist/mods/<name>.dll`. To use it, copy that DLL into the game's `mods\` folder (see the README's *Where the
files go*). Your mod's settings go in `mods\remastered-mod.ini` under `[<name>]`; a section that isn't there yet reads
as the defaults you pass, and `config_set` adds it.

**The API** (`include/remod.h`, plain C; every function lists the version that added it):

| Group | Functions |
|---|---|
| Callbacks | `on_tick` (every 28 ms game update in a tank), `on_draw` (after the tank is drawn), `on_overlay` (over every screen), `on_key`, `on_mouse` (return 1 to keep the input from the game), `on_config` (settings changed) |
| Game state | `app()` (the game object), `board()` (the tank, or 0), `mouse_pos` |
| Drawing | `fill_rect`, `draw_text`, `draw_text_font`, `text_width(_font)`, `draw_image`, `draw_image_part`, `draw_image_scaled`, `redraw` (after your overlay changes), `capture_frame` |
| Game functions | `hook(target, detour, &original)` (MinHook; several mods can hook the same function), `play_sound` |
| Settings | `config_int`, `config_string`, `config_set`, `config_changed` |
| Messages | `toast`, `log` (to `mods\remastered-mod.log`) |
| Mods | `mod_list` (every DLL in `mods\` with its state and description), `restart` |
| Co-op | `set_input_filter`, `inject_input` (used by `coop`; see *Mods and co-op*) |

**Keys:** read a mod's keys from the ini (`config_int("mymod", "key", VK_F9)`, again in `on_config`, so changes from
the settings page apply at once; 0 = no key) and show them with `ui::KeyName(vk)` ("F9", "Space"), never as fixed text.
A key your `on_key` returns 1 for is yours: its auto-repeats and the character it types (Space's `' '`) don't reach the
game either. The settings page's Keys tab lists the built-in mods' keys.

Optional exports: `RemodDescribe()` (one line shown on the Mods page) and `RemodOpen()` (gives the mod an **Open**
button there): [docs/MOD-MANAGER.md](docs/MOD-MANAGER.md).

**Rules that keep players' games safe:**
- Everything runs on the game's main thread, callbacks included; never block it (no waiting on the network or long
  file work: use a thread and hand results back in `on_tick`/`on_overlay`).
- Persist only in your own files: the ini through `config_set`, or a file of your own next to the saves. Never write the
  game's save files or its registry settings.
- A crash in your mod is caught: the core switches the mod off for the session and says so; the game goes on. The log
  names the mod and the callback.
- Check `api->version` before using a function newer than the core you require (new functions are only ever appended).

**Game objects and functions:** pointers to game objects are opaque in the API; `include/game.h` has the addresses of
game functions, field offsets and resource globals that the shipped mods use (all from reverse-engineering the 1.1 exe;
read `game::at<T>(object, offset)` to access a field). `include/remodui.h` draws the mod's UI in the game's own look
(buttons, edit boxes, checkboxes, tooltips, dialogs) and has small helpers (`ui::OnMainMenu`, `ui::DialogCount`,
`ui::CoopPlaying`, `ui::Has`/`ui::Call` for another mod's exports). Game methods are `__thiscall`: hook them with a
`__fastcall(this, edx, args...)` detour and call the original through a `__thiscall` pointer (see `timecontrol`'s
`Board::Pause` hook). Before hooking a function not in `game.h`, read its disassembly (Ghidra, or
`objdump -d -M intel --start-address=...` on the exe): some use other conventions (`WidgetManager::DrawScreen` takes
`this` in EDI; the core's detour for it is in assembly). `tools/check-game.py` checks a game exe against the signatures
in `tools/signatures.txt`.

**Sharing your mod:** a mod is one DLL (plus, if you like, a folder of its own under `mods\` for data). Players copy it
into their `mods\` folder; it appears on the Mods page with your description and can be switched off there. Say which
`REMOD_API_VERSION` it needs, and publish the source so players can build it themselves: mods run with the game's rights.

## Content packs
No code needed: a folder inside `mods\` (any name) can replace the game's images, sounds and music and change what
levels start with. Several packs apply in name order (later ones win); the game's own files are never changed.
```
mods\<pack>\assets\images\...       replaces the game's files of the same relative path and name
mods\<pack>\assets\sounds\...       (also music\, properties\ ...)
mods\<pack>\content\levels.json     what levels start with: store, prices, aliens, money...
```
`levels.json` (comments and trailing commas allowed):
```json
{ "levels": [ { "tank": 1, "levels": [2, 3], "modes": ["adventure"], "money": 500, "foodPrice": 10,
                "alienDelay": 2000, "backdrop": 2, "eggPieces": 2, "startGuppies": 4, "startBreeders": 0,
                "aliens": ["sylvester", "balrog"], "unlockAll": false,
                "store": { "carnivore": { "slot": 2, "price": 800, "unlocked": true }, "starpotion": null } } ] }
```
Store items: guppy breeder foodquality foodquantity carnivore starpotion starcatcher guppycruncher beetlemuncher
ultravore weapon egg. Aliens: littlesylvester sylvester balrog gus destructor ulysses psychosquid bilaterus (or 1-12).
Never applied to the Virtual Tank, the sandbox, the screensaver or the Extra Modes. Don't redistribute the game's own
files in a pack: only your own art and sounds.

## Mods and co-op
Co-op runs every player's game in lockstep: the host collects everyone's input, all machines apply it at the same
tick, and the games compare a hash of their state every 64 ticks. Anything that changes the game on one machine only
puts that player's game out of step for the rest of the session. So a mod that changes gameplay must:
- **stand aside while co-op plays:** check `CoopPlaying()` (exported by `coop.dll`; `ui::CoopPlaying()` in
  `remodui.h`) wherever it would act, not only in its key handler. A mode or setting chosen before the session must
  not carry into it (`timecontrol` forces normal speed, `alienplay` and `extramodes` switch off, `autosave` waits).
- **act only from game input or game ticks:** never from a wall-clock timer, a key the player pressed, or the drawing
  code. Calls into the game's random generator (`MTRand` at app +0x7b0) outside a lockstep tick are logged as a bug.
- **use the lockstep pointer in co-op:** `CoopPointer(slot, &x, &y)` gives each player's pointer as the game applied
  it (the same on every machine); `api->mouse_pos` is this machine's only (see `hovercoins`).
- **hand over level state:** when a player joins a game in progress, or after a desync, every machine reloads the
  host's snapshot (the board through the game's own save). A mod that keeps state for the level exports it (see
  `MutatorsSave`/`MutatorsLoad`, `ContinuesBought`/`ContinuesSetBought`; `coop.cpp`'s `TakeSnapshot` calls them), and
  drops any pointers to game objects when the board is reloaded (`MutatorsBoardReloaded`).
- **leave the game's control to the host:** the host builds every input bundle and drops guests' keys, their clicks on
  the tank's Menu button and their clicks while a dialog is open, so a guest can't pause, quit or answer for everyone.
  A host-only action that changes the game goes through the lockstep like input (see `timecontrol`: it asks `coop.dll`'s
  `CoopRequestSpeed`, which every machine applies at the same tick); `CoopIsHost()` says whether this machine hosts.
- Display-only things (overlays, toasts, sounds) are fine either way.

When a session goes out of step, `remastered-mod.log` on each machine shows, for the last two hashed ticks, what the
hash was made of (`parts:`: RNG index, money, timers, object counts and positions), the game RNG's callers by return
address, and every press applied with the widget that received it. Compare the two logs: the first part that differs
names the cause.

## Layout
```
include/remod.h    the mod API (C)
include/game.h         addresses and offsets in the original exe
core/core.cpp          the loader/core (ddraw proxy, exe check, hooks, mod loading, API)
core/display.cpp       the large window ([display]: DirectDraw primary-surface Blt wrapper, mouse mapping, scaled cursors)
loader/ddraw.exports   the DirectDraw exports the proxy forwards
mods/<name>/           one folder per mod
tests/<name>/          test-only mods (crashtest; testkit: F9 starve every fish, F10 +5000 shells and level 1-2, F11
                       six coins; K starve one guppy; C click the first corpse; F feed the first guppy while full; B buy a guppy), built by `build.sh --tests`, installed by `TESTMODS=1 run-wine.sh`
examples/<name>/       example mods for authors (hello, template), built by `build.sh --tests`; never released
tools/                 fetch-deps.sh (MinHook), gen-proxy.py (export stubs), run-wine.sh (tests), install-steam.sh,
                       package.sh (release zip), release-notes.sh, ci-setup.sh (CI build tools),
                       check-game.py + signatures.txt (checks a game exe against game.h)
server/                your own co-op server: Nakama + Postgres compose files (docs/SERVER.md)
install-steam.bat      Windows installer for the Steam release (copied into build/dist)
.github/workflows/      CI: build on every push and pull request, release on a version tag
remastered-mod.ini          default settings (build/dist/mods/remastered-mod.default.ini: the core makes the player's
                            remastered-mod.ini from it and adds new keys later; kDefaultChanges in core.cpp moves
                            settings still at an old default when a version changes one)
```
Third-party: MinHook (BSD-2-Clause, Tsuda Kageyu), downloaded by `tools/fetch-deps.sh`.

## Game check
The loader only turns on for the game build the addresses were taken from: it hashes the game's code in memory
(`include/game.h`), so the plain 1.1 exe and the Steam release's extracted `popcapgame1.exe` (same code, different
header and resources) both pass, and anything else gets a plain DirectDraw pass-through (see `mods/remastered-mod.log`).
The Steam release's `Insaniquarium.exe` is a launcher that writes the game out as
`ProgramData\PopCap Games\Insaniquarium\popcapgame1.exe` and starts it with `-changedir=<game folder>`; the core takes
the game folder (for `mods/`) from that argument.
