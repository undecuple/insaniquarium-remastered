# Other Insaniquarium projects

As of 2026-11-12. This page describes what the projects' own pages say; we have not run them together with this mod.

## OpenInsaniquarium

[OpenInsaniquarium](https://github.com/elgreams/OpenInsaniquarium) by elgreams is an open-source reimplementation of
*Insaniquarium! Deluxe* 1.1 in Rust (MIT or Apache-2.0). It is a separate program, not affiliated with this mod. Its
page describes it as an early preview (0.1.0) that reads the player's own installed game files and contains no game
assets.

How it relates to Insaniquarium - Remastered Mod:

- **This mod does not run on it.** The mod is a `ddraw.dll` loader for the original game's `Insaniquarium.exe` (Steam,
  version 1.1): it checks that exe's code when it starts and hooks its functions at known addresses. OpenInsaniquarium
  is a different program with no such code, so there is nothing for the loader to hook.
- **Its mods do not run here.** Its page documents no mod interface, and this mod's API (`include/remod.h`) is written
  for the original game's functions and objects.
- **Same game files.** Both need your own copy of the game; neither includes it. Installing one does not touch the
  other: this mod adds files to the game's folder and OpenInsaniquarium reads that folder.
- **Content packs.** The mod's content packs replace the game's images, sounds and music or change its levels
  ([DEVELOPING.md](../DEVELOPING.md#content-packs)). Whether OpenInsaniquarium reads a modified game folder the same
  way is not something we have checked.

If that changes (for example a mod interface in OpenInsaniquarium), an issue here is the place to say so.
