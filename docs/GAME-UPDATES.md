# The mods, and what to check when the game is updated

Every mod works by calling and hooking functions of one specific game build, at fixed addresses. This page lists what
each mod depends on, and what to do if the game (most likely the Steam release) is ever updated.

## Which build the mods are for
*Insaniquarium! Deluxe* 1.1: the classic PopCap exe and the Steam release (App 3320), which run **the same game
code**. The loader recognises it by hashing the game's code in memory (`GAME_TEXT_HASH` in `include/game.h`: FNV-1a
64 over `.text`, RVA 0x1000, 0x1948fc bytes; image size 0x24c000). On any other build the loader stays off, the
game runs unmodded, and `mods/remastered-mod.log` says
`not the Insaniquarium Deluxe 1.1 game these mods are for: mods stay off`.

So an update can never crash the game through the mods; at worst the mods silently switch off until they're updated.

## What the Steam release is made of
- `steamapps/common/Insaniquarium Deluxe/Insaniquarium.exe` is a **launcher** (DRM wrapper). It writes the real game
  out as `C:\ProgramData\PopCap Games\Insaniquarium\popcapgame1.exe` and starts it with `-changedir=<game folder>`.
  On Linux that folder is inside the game's Proton prefix:
  `steamapps/compatdata/3320/pfx/drive_c/ProgramData/PopCap Games/Insaniquarium/`.
- The loader (`ddraw.dll`) sits next to `popcapgame1.exe` (`install-steam.bat` / `tools/install-steam.sh` put it
  there); the mods and settings stay in the Steam folder's `mods\`.
- Saves: `C:\ProgramData\Steam\Insaniquarium\userdata` (the game's data folder is `ProgramData\<PartnerName>\<ProdName>`,
  and the Steam release's `properties/partner.xml` sets PartnerName to `Steam`; other releases use `PopCap Games`). The
  mods read the folder from the game (`G_AppDataFolder` in `include/game.h`, `ui::UserData()`) and add their own files
  there (achievements, records, sandbox layouts). Settings: registry `HKCU\Software\PopCap\Insaniquarium`.

## The mods and the game functions each one uses
Addresses are in `include/game.h` (names below are its constants); field offsets are there too.

| Part | Game functions used (hooked or called) |
|---|---|
| **core** (`ddraw.dll`) | `Board_Update`, `Board_Draw`, `WidgetManager_DrawScreen` (hooked; assembly detour, `this` in EDI), `App_UpdateFrames` (hooked: the tick, also used for the window switch), `App_ProcessDeferredMessage` (input injection), `Graphics_*` (drawing; `draw_image_scaled` sets the `Graphics` scale fields +0x10..+0x1c read by `Graphics_DrawImageSrc`), `Board_PlaySample`, `App_SaveProfile`, app vtable (`DoDialog`, `GetDialog`, `KillDialog`, `SwitchScreenMode`, `Shutdown`), `Widget` `Resize` (vtable), `gApp`, fonts |
| core: native window (`core/display.cpp`) | none in the game: DirectDraw's `CreateSurface`/`Blt` (vtable slots, `IDirectDraw7` and `IDirectDrawSurface`), the window; the exe's imports of `GetCursorPos` and `SetCursor`; reads `App_mIsWindowed`, `App_mMouseIn`, `App_mCursorNum`, takes `App_mCursorImages` (the game's cursor pictures, `images/cursor_*.gif`); writes the registry `ScreenMode` |
| core: game-styled UI (`include/remodui.h`) | images `IMAGE_CENTERBUTTON`, `LEFTBUTTON`, `RIGHTBUTTON`, `MAINBUTTON`, `DIALOGBUTTON`, `FATBUTTON`, `BATTLETANKBUTTON(D)`, `MIDDLEBUTTON(D)`, `EDITBOX`; fonts `JUNGLEFEVER10/12/15OUTLINE` (font vtable: ascent, height, string width) |
| **settings** | `App_ButtonDepress`, `App_mGameSelector`, `IMAGE_SELECTORSCREEN` (the main menu's Remastered page), `IMAGE_CHECKED`/`IMAGE_UNCHECKED` |
| **achievements** | `Board_InitLevel`, `Board_BuyItem`, `Coin_MouseDown`, `Board_AddDeadFish`, `Fish_Eat`, `Board_BankCoins`, `Alien_Die`, `StoreScreen_Purchase`, `App_SaveProfile`, `App_ButtonDepress`, profile fields (pets owned +0x00, user index +0x40, shells +0x48, game finished +0x59) |
| **autosave** | `Board_Pause`, `Board_CanSaveOnQuit`, `Board_SaveOrDeleteGame`, `App_SaveProfile` |
| **continues** | `App_DoTimedDialog`, `App_ButtonDepress`, `App_ShowGameSelector`, `Board_SpawnGuppy`, `App_SaveProfile` |
| **mutators** | `Board_InitLevel`, `Board_StartLevel`, `Board_AddPet`, `Board_AddMoney`, `Board_UpdateMoneyLabel`, `Board_SpawnAlien`, `Board_BuyItem`, `Board_SpendMoney`, `Board_DropFood`, `Board_AddTextCoin`, `Board_AddDeadFish`, `Board_DropBonusCoins`, `Alien_ctor`, `Alien_Shoot`, `Fish_DropCoin`, `Fish_Eat`, `GameObject_TickHunger`, `GameObject_SetHungryFlash`, `GameObject_IsHungry`, `Coin_Update`, `Coin_MouseDown`, `Coin_GetValue`, `DeadFish_Update`, `DeadFish_Remove`, `DeadFish_vtable`, `Widget_MouseDown`, `MTRand_Next` and `App_mMTRand` (the game's random numbers) |
| **extramodes** | `App_StartGame`, `App_RemoveGameSelector`, `App_ShowGameSelector`, `App_DoTimedDialog`, `App_ButtonDepress`, `Board_StartLevel`, `Board_SpawnAlien`, `Board_SpawnGuppy`, `Board_BankCoins`, `Board_BuyItem`, `Board_UnlockStoreItem`, `Board_UpdateMoneyLabel`, `Board_Pause`, `Alien_ctor`, `HighScore_AddTimeTrial` |
| **sandbox** | `App_StartGame`, `App_RemoveGameSelector`, `App_DoTimedDialog`, `Board_Add*At` (guppy, breeder, oscar, ultra, gekko, penta, grubber), `Board_AddPet`, `Board_SpawnAlien`, `Board_SetBackdrop`, `Board_Pause`, `GameObject_RemoveFromGame`, `GameObject_TickHunger` |
| **alienplay** | `Alien_Think`, `Alien_TryEat`, `App_StartBoard`, `App_RemoveBoard`, `App_RemoveGameSelector`, `App_ShowGameSelector`, `App_DoTimedDialog`, `App_ButtonDepress`, `Board_Add*At`, `Board_AddPet`, `Board_SpawnAlien`, `Board_StartLevel`, `Board_BuyItem`, `Board_UnlockStoreItem`, `Board_UpdateMoneyLabel`, `Board_Pause`, `GameObject_RemoveFromGame` |
| **content** | `Board_SetupLevel` (levels.json is applied after it), `Board_StartLevel`, `Board_AddGuppyAt`, `Board_AddBreederAt`, `Board_SetBackdrop`, `Board_UnlockStoreItem`, `Board_UpdateMoneyLabel`, `GameObject_RemoveFromGame`; Windows' `CreateFileA`/`GetFileAttributesA` (asset overlays) |
| **coop** | `App_UpdateFrames` and `App_ProcessDeferredMessage` (through the core's input filter and injection), `App_StartGame`, `App_RemoveGameSelector`, `App_RemoveBoard`, `App_ShowGameSelector`, `App_LostFocus` (a background instance must keep running), `App_ButtonDepress`, `App_SaveProfile` (guests' saves blocked), `Board_AddMoney`, `Board_SpendMoney`, `Board_BuyItem`, `Board_BankCoins`, `Board_Pause`, `Board_SaveOrDeleteGame`, `Alien_Shoot`, `Alien_Think` and `Alien_TryEat` (versus steering; alien fields as in alienplay), `Coin_MouseDown`, `Coin_GetValue`, `Widget_MouseDown`, `GameObject_SetHungryFlash`, `App_mMTRand` (seeded and hashed; `MTRand_Next` hooked to list the RNG's callers when a desync is logged), `GameObject_PetSleepy` (pets' naps count from the last lockstep input), `Shot_ctorKind` (the game's uninitialised read cleared), split wallets: `Board_InitLevel`, `Board_AddMoney`, `Coin_ReceiveMoney`, `GameObject_RemoveFromGame`, `Board_UpdateMoneyLabel`, `Board_mFoodPrice`/`mHoldFeed`; snapshots (resync, joining): `Board_SaveOrDeleteGame` with `Board_mShouldSave`, `App_WriteBytesToFile` and `App_ReadBufferFromFile` (served from memory, no files), `Buffer_WriteByte`, `App_LoadBoardGame`, `App_DoContinueDialog`, the globals `G_*` in `game.h`, the game's queued messages (`App_mDeferredHead`/`Count`, freed with `Game_OperatorDelete`) and the widget manager's pressed state (`App_mWidgetManager`, `WM_m*`, `Widget_mIsDown`), both cleared at a session's start, the profile (copied from the host); avatars: the image globals `IMAGE_SMALLSWIM` and `IMAGE_SCL_*` (addresses in `coop.cpp`), `Image_mWidth`/`mHeight` |
| **hovercoins** | the board's coin list, `Widget_mMouseVisible`; collects through a click the game handles itself |
| **accessibility** | `GameObject_IsHungry`, `Coin_GetValue`; the board's object lists |
| **timecontrol** | `Board_Pause`; `App_mFrameTime`; `App_DoLostFocusDialog` (the pause dialog, when Space is moved to another key; the core then keeps Space's character from the game) |
| **fps** | nothing in the game (overlay) |
| **screenshot** | nothing in the game: the core's `capture_frame` (the game's frame surface), GDI as a fallback |
| **screensaver** | `App_SaveProfile`, the game window (`App` +0x350); starts the game's own exe with `-screensaver`, where the core hooks `App_ReadFromRegistry` (WinFishApp's: it clears `App_mIsWindowed` in screensaver mode) to keep that copy windowed |

Several mods hook the same function (e.g. `Board_BuyItem`, `Coin_MouseDown`); the core chains them.

## When the game is updated: checklist
1. **Do the mods still turn on?** Start the game and look at `mods/remastered-mod.log`. If it shows
   `code hash d24e61422309ae8c` and `N mod(s) loaded`, the game code didn't change (only the launcher or data did):
   **nothing to do.** Also play a level and open the settings page (F2) as a smoke test.
2. **If it says the mods stay off**, get the new game exe: on Windows, copy
   `C:\ProgramData\PopCap Games\Insaniquarium\popcapgame1.exe` while the game is running (the launcher may delete it
   on exit); on Linux, the same path inside the Proton prefix. Then run
   ```sh
   tools/check-game.py popcapgame1.exe
   ```
   It prints the new code hash and, for each function in `game.h`, whether its first 16 bytes still match
   `tools/signatures.txt` (recorded from the known-good build), plus whether the data addresses still fit the image.
3. **Read the result:**
   - *Different hash, every function and data address matches:* the change is elsewhere (a fix in code no mod touches).
     The addresses are probably still valid, but **field offsets and inlined behaviour aren't covered by the check**.
     Before trusting it, diff the old and new `.text` (any byte compare tool) to see where the changes are, and make
     sure none falls inside a function listed above or one they call. Then update `GAME_TEXT_HASH` (and
     `GAME_IMAGE_SIZE` if it changed) in `include/game.h`.
   - *Some functions show `X` (bytes differ):* code moved or changed. Each listed constant has to be found again in the
     new exe: load it in Ghidra (or any disassembler), find the function by its strings, callers or byte pattern (the
     old signature from `tools/signatures.txt` usually matches a shifted address), check its arguments and return
     (`RET n` gives the argument bytes) and update `game.h`. Then re-check the field offsets each mod reads (listed
     in `game.h`) against the new code.
   - *Data addresses outside the image:* the layout changed a lot; treat it as a new port of every address.
4. **Rebuild and test:** `./build.sh --tests`, then the headless runs in DEVELOPING.md ("Testing without Windows"),
   with `GAME=<a copy of the updated game>`: title, a level, the settings page, each mod's feature (testkit keys help:
   F9/K starve, F11 coins, B buy a guppy, E egg pieces, M money). Add the new hash to the README's supported list.
5. **Record the new signatures:** `tools/check-game.py --update popcapgame1.exe`, and commit `game.h` and
   `tools/signatures.txt` together.
6. **Supporting both builds** (players who don't update, e.g. the classic exe): `game.h` would become one address
   table per hash, chosen at start. The loader already refuses unknown hashes, so add the table, not a bypass.

## What can break without the hash changing
Nothing in the game itself: the hash covers all of its code. Outside it:
- **Wine/Proton or Windows updates** can change DirectDraw's behaviour; the native window (`core/display.cpp`) is the
  most exposed part (it relies on the game copying its frame to the primary surface with `Blt`). Test it after a big
  Proton update; `[display] window=normal` is the fallback.
- **Steam's launcher** could stop extracting `popcapgame1.exe` or put it elsewhere: then the loader isn't found next to
  the game, nothing loads, and the log isn't written. `install-steam.bat` / `install-steam.sh` and docs/STEAM.md
  describe the current layout.
- **Steam co-op** uses Valve's `steam_api.dll` (in `mods\coop\`) and the Spacewar test app (480) for lobbies and the
  relay. If Steam changes the flat API or retires Spacewar's lobby access, Steam co-op stops (co-op by address keeps
  working); `mods/remastered-mod.log` shows the Steam calls and their results.
- **Co-op between different builds** can't work: players' games must run the same code and the same mods
  (the loader only runs on the known build; the lobby compares the mod version and the list of mods), so after an
  update every player needs the updated mod.
