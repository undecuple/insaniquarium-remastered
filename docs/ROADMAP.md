# Roadmap

Insaniquarium - Remastered Mod brings new features to the original *Insaniquarium! Deluxe* 1.1 (the Steam release's
`Insaniquarium.exe`) as mods: small DLLs loaded into the running game. There is no source code for the game,
so every feature is **code injected into the game**: the loader (`ddraw.dll`) that the game loads by itself, a small
hooking framework in it, and one mod per feature that replaces or wraps the game's functions at known addresses and
calls the game's own functions to do the work.

Status (2026-10-06): **phases 1–4 and 6 done, phase 5 partly** (640x480, large, borderless or fullscreen; Direct3D presenting and HD
sprites not started). 16 mods: the loader, the mod API, hooks, an overlay on every screen, crash containment, safe
mode and save backup, plus every feature marked done below. Online co-op works through the mod's public server with
no setup. Tested under Wine, Proton-GE and the Steam release from the Steam client (Proton Experimental).

## Architecture
```
<game folder>
  Insaniquarium.exe          untouched
  ddraw.dll                  the core: DirectDraw proxy, game check, hooks (MinHook), mod loader, mod API
  mods/
    remastered-mod.ini       settings (remastered-mod.log: the log)
    timecontrol.dll          one DLL per feature
```
- **Loader:** the game loads DirectDraw with `LoadLibrary("ddraw.dll")`, which finds the proxy in the game folder
  first (Wine/Proton: a `ddraw=native,builtin` override). Every DirectDraw export jumps to the system's `ddraw.dll`.
  The game's first `DirectDrawCreate` (main thread, before resources load) loads the mods.
- **Steam release:** its `Insaniquarium.exe` is a launcher that writes the same game build out as
  `ProgramData\PopCap Games\Insaniquarium\popcapgame1.exe` and runs it with `-changedir=<game folder>`; the loader goes
  next to that file and finds the game folder from the argument ([STEAM.md](STEAM.md)).
- **Language:** C/C++ cross-compiled with 32-bit mingw-w64. The game is MSVC 7.1 C++: `__thiscall` methods, MSVC
  vtables and `std::string` layouts (`include/game.h`).
- **Hooks:** MinHook for function entry hooks (thiscall detours as `__fastcall`; one function takes `this` in EDI and
  has an assembly detour), vtable patching for virtual methods, byte patches for constants.
- **Mod API (`include/remod.h`):** callbacks for the game update (28 ms ticks), tank drawing, an overlay on every
  screen, keys; drawing with the game's own `Graphics` and fonts; toasts; settings; a log. New screens will be built
  from the game's own classes (`Dialog`, `DialogButton`, `Checkbox`, `Slider`) by calling their constructors, so they
  look native.
- **Assets:** the game loads loose files from `images/`, `sounds/`, `music/`, `properties/resources.xml`; asset mods
  will be an overlay folder resolved by hooking the framework's file open.

## Features
Feasibility: **easy** (a byte patch or one hook), **medium** (several hooks, new UI from the game's classes), **hard**
(new subsystems).

| Feature | How | Feasibility | State |
|---|---|---|---|
| Time control (pause, 0.75x–2x) | change the update interval (`mFrameTime`, app `+0x454`); pause through `Board::Pause` | easy | **done** (`timecontrol`) |
| Autosave, "continue with shells" after a game over | call the game's own save on a timer and on pause / hook the game-over dialog and the app's button handler | easy–medium | **done** (`autosave`, `continues`) |
| Screenshot key, FPS counter | screen capture with GDI, PNG through GDI+; frames counted in the overlay | easy | **done** (`screenshot`, `fps`; keys set in the ini) |
| Accessibility: hungry-fish marker, coin values | overlay drawn from the board's object lists, the game's own `IsHungry` and coin values | easy | **done** (`accessibility`) |
| Hover to collect coins | each tick, a coin under the pointer gets the game's own click | easy | **done** (`hovercoins`) |
| Screensaver from the game (also on Linux/Proton) | saves the profile and starts the game's own exe with `-screensaver` (what `WinFish_Scr.exe` is built from) from inside the running game, so it shares the game's prefix and saves; the core keeps that copy windowed (WinFishApp::ReadFromRegistry hooked) in a borderless window covering the screen, with no mods; the game waits minimised | easy | **done** (`screensaver`) |
| Console and cheats (`~`) | a text overlay; commands call the game's own spawn functions | medium | |
| Mutators (15: hungry fish, double trouble, glass cannon, no pets, pacifist, heavy coins, tiny wallet, rich start, hard mode, golden guppies, coin combos, tank events, overeating, decay, gadgets) | hooks where the rules live (hunger tick, level start, money, alien spawn and constructor, laser hit, coin fall and pickup, fish eating, dead fish, store) plus overlay panels | medium | **done** (`mutators`) |
| Settings page | one of the game's own dialogs (`DoDialog`, resized), tabs and checkboxes drawn with the game's images and fonts, mouse through the API; F2 or a main-menu button; switch mods off | medium | **done** (`settings`) |
| About page, Mods page, Extra Modes screen | the same dialog approach (About and Mods buttons on the main menu: version, changes, keys, every mod with its description, an Open button for mods that have a screen; Extra Modes button on the main menu) | medium | **done** (`settings`, `extramodes`) |
| Achievements and shell rewards | counters from hooks (coin pickup, alien death, fish growth, egg pieces, level won via `BankCoins` with 4 egg pieces, Virtual Tank purchase), banner, rewards paid into the profile on the main menu, list dialog; `userdata\achievements<n>.txt` | medium | **done** (`achievements`) |
| Daily challenge, Boss rush, Endless | Challenge-mode games started through `StartGame`; level setup hooked, the alien schedule steered from the wave counter; records per profile | medium | **done** (`extramodes`) |
| Sandbox editor | the hidden sandbox (mode 3) from Extra Modes, a palette overlay, the game's positional spawners, save slots | medium | **done** (`sandbox`) |
| Level definitions (`levels.json`) | hook `Board::SetupLevel` / `StartLevel` and the waves (store slots, prices, aliens, money, food price, backdrop, egg pieces, start fish) | medium | **done** (`content`) |
| Extra Hall of Fame pages | Records screen in Extra Modes (Hard mode, Endless, Boss rush tables; `userdata\halloffame_extra.txt`) | medium | **done** (`extramodes`) |
| Asset overlays | `mods\<pack>\assets` replaces game files: kernel32 `CreateFileA`/`GetFileAttributesA` redirected (images, sounds, music, data) | medium | **done** (`content`) |
| HD sprites | needs the game's 640x480 software renderer replaced by one that draws each sprite at screen resolution (every `Graphics` call re-done in Direct3D) | hard | not planned for now |
| Play as Alien | 20-stage campaign with two lives; the player's alien steered by hooking `Alien::Think`, an AI keeper clicking through the board's own `MouseDown`, murk overlay; keeper achievements don't count while playing the alien | hard | **done** (`alienplay`) |
| Window choices: the game's 640x480 window (default, kept windowed), large (resizable, whole multiples of 640x480 that fit the screen), borderless, fullscreen; fit / integer scaling | the core wraps DirectDraw (primary surface `Blt`) and stretches the game's 640x480 frame into the window; the game's own cursor shown as a scaled Windows cursor (its `SetCursor` and `GetCursorPos` imports); mouse positions mapped back (`core/display.cpp`) | medium | **done** (`[display] window=native`) |
| Resizable window, smooth (filtered) scaling, HD sprites | present through Direct3D 11/OpenGL instead of DirectDraw's stretch | hard | |
| Co-op (2–4 players): roles, avatars, versus, rescue, split wallets, ping markers, chat | **done:** lockstep over TCP (`coop`): inputs filtered from the window, bundled by the host per tick, injected through the game's own deferred messages at the start of each tick (`WinFishApp::UpdateFrames` hooked); host profile copied in memory; state hashes every 64 ticks; roles, chat, pings, rescue, scaling; avatars (the game's pet and fish portraits on each pointer, drawn scaled through API 7's `draw_image_scaled`); versus (the first guest's input steers every alien through the `Alien::Think` hook, applied at the lockstep tick, click to dash/fire; the winner is announced); split wallets (host's choice: the acting player's wallet is swapped into the board's money while their input is applied, coins remember who clicked them, hold-to-feed charges whoever fed, pets' pickups are shared out, the money label shows your own). Tested with two games (in step to the end, the alien ate every fish), and soak-tested for hours with random input and automatic restarts: five causes of drift found and fixed (other mods' speed and modes, input queued from before a session, a button held at the start, the pets' idle count); diagnostics log the hash's parts, the RNG's callers and every applied press. A bug in the game itself made alien deaths drift: `Shot`'s constructor reads its kind before setting it (a random draw depending on leftover heap memory); cleared first in a session. Resync: after a desync the host snapshots its game (the board through the game's own save, caught in memory; the RNG, globals, co-op and mutator state) and every machine reloads it at the same tick. Joining a game in progress: the joiner gets the start settings, then everyone loads a snapshot that includes them. The simulation's roster changes only at lockstep ticks (a leave is an input event). Host authority: guests can't open the host's menus or dialogs, change mutators or the speed (time control is the host's, applied at a lockstep tick); a guest who missed inputs jumps ahead to a snapshot. **To do:** a rare drift in the first seconds of a session (being traced; resync now recovers from it) | lockstep: intercept input at the game's message handling, run each update only when every player's input for it has arrived, share the random seed, send joiners a snapshot made with the game's own save/load, hash the state to catch desyncs. Possible because the game is deterministic per tick (its own MT random generator; cosmetic CRT `rand`) | hard | |
| Online co-op: lobby browser, public/private games, join by code | **done** for Steam (`coop/steam.cpp`: lobbies tagged with the mod set, App ID 480, `ISteamNetworkingMessages` relay, codes); direct address over TCP. Not yet tested between two Steam accounts. **Done** through Nakama (`coop/online.cpp`: WinHTTP, named relayed matches, `remod_lobbies` listing, reconnect with backoff, the server's rules: rate limits, version gate, names and chat filtered): the mod's public server (insanicoop.mychud.net) by default, no setup; or your own server in Server settings. Tested with two games: listed and private, 15-minute runs without desyncs or drops | Steam lobbies and Steam networking (App ID 480, [STEAM.md](STEAM.md)); the public Nakama server, or your own for groups that prefer it ([SERVER.md](SERVER.md), `server/`) | hard | done (Steam between two accounts untested) |

Out of scope: the game's registration and DRM wrapper (left alone).

## Phases
1. **Loader and framework** (done): proxy, game check, MinHook, mod API, overlay, log, crash containment, safe mode,
   save backup.
2. **Easy wins** (done): time control, key bindings (a Keys tab: every key, defaults, the game's Space), FPS counter, screenshots, autosave, hover coins, screensaver;
   the console is still open.
3. **Content** (done): asset overlays, level definitions, mutators, settings page, achievements.
4. **Modes** (done): Extra Modes screen, daily challenge, boss rush, endless, sandbox editor, Hall of Fame pages, Play
   as Alien.
5. **Display wrapper:** large resizable window and borderless (done); filtered scaling, HD sprites (not started).
6. **Co-op** (done): LAN lockstep, roles/versus/avatars, split wallets, joining in progress, online through Steam and
   the public server with a lobby browser.

## Risks
- **Other game builds:** every address is for this build; other releases are recognised only if their code matches
  (the Steam release does), otherwise the mod stays off until an address table for them exists.
- **Calling into a 2003 MSVC C++ binary:** strings, vectors and allocations must use the game's own runtime and
  layouts (call its allocator, never mix heaps).
- **Effort:** phases 2–4 are moderate; the display wrapper and co-op are large.
