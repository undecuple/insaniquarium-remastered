// Insaniquarium - Remastered Mod: the release version and what's new in each (the release zip's CHANGELOG.txt and the
// release page). Newest first, short lines (about 50 characters);
// bump REMOD_VERSION with each release (DEVELOPING.md, Releasing).
#pragma once
#define REMOD_VERSION "0.2.0"
#define REMOD_WIDEN2(x) L##x
#define REMOD_WIDEN(x) REMOD_WIDEN2(x)
#define REMOD_VERSION_W REMOD_WIDEN(REMOD_VERSION)

static const struct { const char* version; const char* lines[16]; } RemodChangelog[] = {
    { "0.2.0", {
        "Keys tab: change every key, reset to defaults.",
        "  Space's pause dialog can move to another key.",
        "Window: 640x480 by default (also on Steam),",
        "  or large, borderless or fullscreen, with",
        "  the game's own cursor at the right size.",
        "Co-op chat in the lobby too; lines fade out.",
        "Screensaver runs in a window: no more closing",
        "  at once on Linux, the game waits and returns.",
        "Steam: saves backed up from the right folder,",
        "  achievements and records kept beside them.",
        "Updates keep your settings and tidy old files.",
        "Co-op: no more \"too many lobbies\" errors.",
        "The start-up banner no longer covers",
        "  \"Click here to play!\".",
        nullptr } },
    { "0.1.0", {
        "First release.",
        "Remastered page on the main menu; settings: F2.",
        "Mods page; every key can be changed (Keys tab).",
        "Window: 640x480, large, borderless or fullscreen.",
        "Co-op, 2-4 players: online with no setup,",
        "  Steam or address. Avatars, versus (a player",
        "  steers the aliens), split money, joining.",
        "Autosave, continue with shells.",
        "The game's screensaver from the menu (F11).",
        "Collect coins by hovering over them.",
        "15 mutators, 31 achievements with rewards.",
        "Extra Modes: daily challenge, boss rush,",
        "  endless, sandbox, play as the alien, records.",
        "Hungry-fish marker, coin values, time control,",
        "  frame counter (F3), screenshots (F12).",
        nullptr } },
};
