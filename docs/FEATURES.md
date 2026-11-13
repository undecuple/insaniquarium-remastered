# Features

Open everything from the **Remastered** button on the main menu (bottom left), or with [the keys](KEYS.md).

| Mod | What it does |
|---|---|
| **coop** | Co-op for 2-4 players: online with no setup, over Steam, or by address (see below) |
| **extramodes** | **Daily Challenge** (the same tank and mutators for everyone that day), **Boss Rush**, **Endless**, and **Records** |
| **alienplay** | **Play as Alien:** 20 stages where you steer the alien while a computer keeper shoots back |
| **sandbox** | The game's hidden sandbox as a mode: place fish, aliens and pets, backdrops, three save slots |
| **mutators** | 15 optional rule changes (hungry fish, double trouble, glass cannon, tiny wallet, tank events...) |
| **achievements** | 31 achievements with shell rewards, a banner when one unlocks, progress saved per profile |
| **hovercoins** | Coins are collected when the pointer passes over them |
| **autosave** | Saves the level and your profile every 2 minutes and whenever the game pauses |
| **continues** | When all your fish die, spend shells (500, then 1,000...) for two guppies and keep going |
| **accessibility** | A red **!** over hungry fish, and each coin's value under it (off at first: the Gameplay tab) |
| **timecontrol** | Pause, or play at 0.75x or 2x, in any tank |
| **screenshot**, **fps** | A screenshot key (PNG in the game folder's `screenshots` folder) and a frame counter |
| **screensaver** | Your Virtual Tank as the game's own screensaver, from the main menu |
| **content** | Content packs: replace the game's images, sounds and music, or change its levels ([DEVELOPING.md](../DEVELOPING.md#content-packs)) |
| **settings** | The settings page, the Remastered page and the Mods page (switch mods off or on) |

**Co-op.** The **Online** tab is the default: host a **Public Match** (listed for anyone) or a **Private Match**
(friends join with its room code), or join from the list, through the mod's public server: no ports to open, nothing
to set up. The list shows each match's mod version, and greys the ones you can't join. The other tabs: **Steam** (the same through Steam lobbies), **Direct** (by address; the host opens TCP port
27615) and **Server** (your own server: [SERVER.md](SERVER.md)). Everyone plays the host's profile (your own
saves are never touched; shells you earn come home with you), with roles, chat, pings (middle click), avatars, tougher
tanks for more players, split money, joining a game in progress, and **Versus** (one player steers the aliens). The
host is in charge of pausing, speed and menus. Everyone needs the same mods. Steam co-op needs Valve's
`steam_api.dll`, which the mod usually finds in another Steam game by itself ([STEAM.md](STEAM.md)).

**Window.** A **Large** window by default (as large as the screen allows, resizable, sharp scaling, the game's own
cursor at the same size), the game's own **640x480**, or **Borderless** (fills the screen). The game's Fullscreen box
and Alt+Enter switch to Borderless and back. Tiling desktops (niri, sway...) size the window themselves.

**Safety nets.** Hold **Shift** while the game starts to play without mods. A mod that crashes is switched off with a
message and the game keeps going. Your profiles are copied to `userdata-before-remastered-mod` before the first run.
