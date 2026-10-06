# Contributing

Thanks for helping with Insaniquarium - Remastered Mod.

- **Bugs:** open an issue with what happened, your system (Windows / Linux / Steam Deck) and `mods/remastered-mod.log`
  from the game folder. For co-op problems, attach every player's log: an out-of-sync log records what differed.
- **Your own mods** don't need to live here: a mod is a DLL players drop into `mods\`. Start from `examples/template`
  and read [DEVELOPING.md](DEVELOPING.md) and [docs/MOD-MANAGER.md](docs/MOD-MANAGER.md).
- **Pull requests:** keep them focused, build with `./build.sh --tests`, and test in the game (DEVELOPING.md has a
  headless Wine/Proton runner). Match the style of the surrounding code; game addresses go in `include/game.h` with the
  function's signature in a comment, and `tools/signatures.txt` for `tools/check-game.py`.
- **Gameplay changes** must keep co-op in step: see *Mods and co-op* in DEVELOPING.md.
- **No game files:** never commit the game's exe, images, sounds or music, or anything extracted from them.

By contributing you agree that your contribution is licensed under the MIT licence (LICENSE.txt).
