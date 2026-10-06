# The Mods page: how a mod ties into it

Insaniquarium - Remastered Mod has a mod manager built in: the **Mods** page. Players open it from the main menu's
**Remastered** page (**Mods**, bottom row) or from the settings (**F2** → **Mods** tab). Every DLL in the game's
`mods\` folder is listed there, in load order. Each mod's row has:

- **its name**: the DLL's file name without `.dll` (`mods\fishcam.dll` → `fishcam`);
- **a checkbox** to switch it off or on. This writes `[mods] fishcam=0` or `1` to `mods\remastered-mod.ini` and takes
  effect when the game restarts. Once anything differs from what's running, a **Restart the game now** button appears;
- **its state**: *on*, *off*, *on/off after restart*, *didn't start (see the log)* (its `RemodInit` returned 0),
  *crashed: off until restart*, or *not a mod* (a DLL without `RemodInit`);
- **its description**, shown under the list while the pointer is over the row;
- **an Open button**, if the mod has a screen or settings of its own.

The page itself is the `settings` mod (`mods/settings/settings.cpp`). The list comes from the core: `api->mod_list`.

## What your mod provides

Nothing is required: any mod appears with its name, checkbox and state. Two optional exports make it look finished:

```c
/* one line about what the mod does: the Mods page's description (and the log) */
__declspec(dllexport) const char* RemodDescribe(void) { return "Shows a camera that follows the biggest fish."; }

/* the Open button: open your screen, your settings, or anything else. Called on the game's main thread after the
   settings dialog has closed */
__declspec(dllexport) void RemodOpen(void) { OpenMyScreen(); }
```

- `RemodDescribe` is called once, when the mod loads (the core keeps a copy of the text). Keep it short: the page shows
  it in up to two lines.
- `RemodOpen` only shows its button while the mod is running (*on*). A mod that's off or crashed gets no button.
- In C++, write both as `extern "C"` so the names aren't mangled.

Both exports are in the examples: [`examples/hello`](../examples/hello/hello.cpp) (Open switches its tag) and
[`examples/template`](../examples/template/template.cpp) (Open shows its totals). Among the shipped mods, `coop`,
`achievements` and `extramodes` open their screens from it.

## Settings of your own

Keep your mod's settings in `mods\remastered-mod.ini` under a section with your mod's name:

```c
int speed = api->config_int("fishcam", "speed", 2);             /* read each time: changes apply at once */
api->config_set("fishcam", "speed", "3");                       /* writes the ini (adds the section if needed) */
api->config_changed();                                          /* tells every mod (their on_config callbacks run) */
api->on_config(ReloadMySettings);                               /* yours runs when anyone changes settings */
```

The settings dialog's tabs are built into the `settings` mod. For anything more than a few switches, give your mod its
own screen and open it from `RemodOpen`. `include/remodui.h` draws in the game's own look:
- `ui::OpenDialog`, `ui::KillDialog`, `ui::GetDialog`: a game dialog to draw on;
- `ui::Button`, `ui::EditBox`, `ui::FitText`, `ui::WrapText`: controls and text;
- `ui::Tooltip` with `ui::DrawTooltip`: hover help.

The `coop` mod's screen is a complete example: a dialog, buttons, edit boxes, tabs and tooltips, all drawn in
`on_overlay` with clicks taken in `on_mouse`.

## The Remastered page

The **Remastered** button on the main menu turns the menu's button panel into the mod's own: **Co-op**, **Extra Modes**,
**Achievements**, **Mod Settings**, **About**, **Mods** and **Records**. These buttons are fixed. Each calls an
export of its mod (`CoopOpen`, `ExtraModesOpen`, `AchievementsOpen`, `RecordsOpen`) and is greyed out when that mod
isn't loaded. Other mods reach players through the **Mods** page's Open button above, or with their own key
(`api->on_key`).

## Checking another mod

A mod can check for, or call, another mod's exports. Look a mod up by its DLL name:

```c
if (ui::Has("coop.dll", "CoopPlaying")) { ... }   /* is it loaded, and does it export this? */
ui::Call("achievements.dll", "AchievementsOpen");  /* call a void(void) export; false if it isn't there */
```

Don't rely on load order: every mod's `RemodInit` runs before the game starts, so look other mods up when you need them,
not in your own `RemodInit`.
