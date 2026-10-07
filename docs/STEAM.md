# Steam

## The Steam release (app 3320)
Works (tested 2026-10-05 from the Steam client with Proton Experimental, and with a copy under Proton-GE: a level,
speed, pause).

How the Steam release starts: its `Insaniquarium.exe` (3.8 MB) is PopCap's launcher. It carries the game exe inside,
writes it out as `ProgramData\PopCap Games\Insaniquarium\popcapgame1.exe` and starts that with
`-changedir=<game folder>`. The embedded game is the same build as the classic 1.1 download (identical code, only the
header and resources differ), so the mod recognises it by its code hash.

Consequences for installing:
- The loader (`ddraw.dll`) has to sit **next to `popcapgame1.exe`**: Windows looks for `ddraw.dll` beside the running
  exe first, then in the system folder, and only then in the current folder (the game folder).
- `mods/`, the settings and the log stay in the Steam game folder: the core takes it from `-changedir=`.
- Installers: `tools/install-steam.sh` (Linux/Steam Deck: into the game's Proton prefix, plus a Wine DLL override for
  `popcapgame1.exe` in that prefix, so no launch options are needed) and `install-steam.bat` (Windows). Both uninstall
  too. Player steps: [INSTALL.md](INSTALL.md).

## Notes
Under Proton the game starts fullscreen by default (a scaled fake display mode); with the mod, it opens
in the window you chose instead. If the Steam overlay misbehaves with the DirectDraw game, turn it off for it.

## Steam for co-op
Works (2026-10-05: lobby created in the Steam release, listed and found from another process, code copied). Not yet
tested between two Steam accounts.
- **Lobbies:** `ISteamMatchmaking` lobbies, listed or private, tagged `insaniquarium-remastered-mod-<hash>` (the hash
  covers the mod version and mod list, so only matching games see each other; App ID 480 is shared with other
  projects). Private games are joined by a code: "S" + the lobby's account number in base 33. The code is shown to
  everyone in the lobby with a Copy button; Ctrl+V pastes one into the join box.
- **Networking:** `ISteamNetworkingMessages` between Steam users, relayed by Valve (no port forwarding, NAT traversal
  included), reliable messages.
- **App ID 480** ("Spacewar", Valve's public test app), set as `SteamAppId` in the environment before Steam starts in
  the game. Players show as "playing Spacewar".
- **`steam_api.dll`:** the 32-bit flat API. Valve's Steamworks redistributable, so the release zip leaves it out
  (`tools/package.sh --with-steam-api` adds it). Where the mod gets one, in order:
  1. `mods\coop\steam_api.dll` (not `mods\`, where the loader would take it for a mod);
  2. the copy found last time (`[coop] steam_api=` in the ini);
  3. a search of the player's Steam libraries, once, when Steam is first picked: the `steamapps\common` this game is
     in, Steam's own (registry `SteamPath`, or `STEAM_COMPAT_CLIENT_INSTALL_PATH` under Proton, as a `Z:` path) and the
     others in its `libraryfolders.vdf`, each game folder up to 5 levels deep (Unity games keep it in
     `<Game>_Data\Plugins\x86`), at most 40,000 folders. Each candidate's export table is read from the file first:
     only 32-bit DLLs with the flat API's interface accessors (SDK 1.49+, 2020) qualify; the one with the most exports
     (the newest) is loaded in place.
  Older SDKs work: `SteamAPI_Init` when there's no `SteamAPI_InitFlat` (before 1.58), and `SteamUser` v020-v023,
  `SteamFriends` v017-v018. Tested with SDK 1.53 (2021) and the current one (Steamworks.NET 2025.164.1). Nothing found: the co-op screen says so, and co-op by
  address still works.
- Not Remote Play Together: it's only for store games that enable it, and it shares one cursor instead of giving each
  player their own.
