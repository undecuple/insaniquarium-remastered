# Instructions for AI coding agents

Read this before changing anything in this repository. It applies to every automated contributor (Claude Code, Codex,
Copilot, Cursor and the like) and to the people who run them. Human contributors: the same rules are in
[CONTRIBUTING.md](CONTRIBUTING.md).

## The project

Insaniquarium - Remastered Mod is an unofficial fan mod for *Insaniquarium! Deluxe* 1.1 (Steam). A `ddraw.dll` loader
puts a small hooking framework into the game, and one DLL per feature wraps the game's own functions at known
addresses. A C API lets anyone write a mod. The repository holds only the mod: each player needs their own licensed
copy of the game. More: [README.md](README.md), [DEVELOPING.md](DEVELOPING.md), [BUILDING.md](BUILDING.md),
[KNOWN_ISSUES.md](KNOWN_ISSUES.md), [docs/ROADMAP.md](docs/ROADMAP.md).

## Repository map

| Path | What it holds |
|---|---|
| `core/` | the `ddraw.dll` loader and the mod API's core (hooks, exe check, mod loading, the large window) |
| `mods/<name>/` | one folder per mod; `mods/remastered-mod.default.ini` holds the default settings |
| `include/` | `game.h` (addresses in the game's exe), `remod.h` (the mod API), `version.h` (version and changelog) |
| `hosttests/` | native tests of the pure logic (JSON, base64, URLs, version compare, word filter) |
| `tests/`, `examples/` | test-only mods; a template mod and a hello-world mod (never released) |
| `install/` | the installers for the Steam release |
| `server/` | the co-op server (Docker: Nakama and Postgres) |
| `tools/` | build, packaging, game check, art generator, host tests |
| `docs/` | install, features, keys, settings, privacy, roadmap, game updates |
| `third_party/` | downloaded by `tools/fetch-deps.sh`; not committed |

## Hard rules

- **No game material, ever.** Never commit, attach, paste or link to the game's exe, images, sounds, music, saves or
  anything extracted from them. Addresses, names and layouts recovered from the game are fine, in `include/game.h`
  and its comments. The game folder and `work/` stay out of git.
- **No circumvention.** Do not add cracks, key generators or any way around the game's own checks. The loader only
  runs on the known game build; the way to support a new build is a new signature table, not a bypass
  ([docs/GAME-UPDATES.md](docs/GAME-UPDATES.md)).
- **MinHook stays as downloaded.** `tools/fetch-deps.sh` fetches it and checks its hash. Do not copy it into the
  tree or edit it.
- **No code with an incompatible licence.** Other projects (WinFish, OpenInsaniquarium) may be read for behaviour.
  Never copy, translate or closely paraphrase their code.
- **Co-op stays in step.** A mod that changes the game must stand aside while co-op plays and act only from game
  input or ticks. Read *Mods and co-op* in [DEVELOPING.md](DEVELOPING.md) before touching gameplay.
- **Online is opt-in.** Nothing contacts a network unless the player hosts or joins a game or sets a server. A new
  connection is added to "What goes online" in the README and to [docs/PRIVACY.md](docs/PRIVACY.md) in the same
  change. Do not write claims about the public server's operator or how long it keeps data.
- **The mod never changes the game's files.** Settings, logs and screenshots go to the game's `mods` and
  `screenshots` folders, as described in the README.

## Licence, attribution and citation

- The code is MIT ([LICENSE.txt](LICENSE.txt)). Other components keep their terms, listed in [NOTICE.md](NOTICE.md).
- A new dependency, tool or font needs a compatible licence and a row in NOTICE.md in the same change.
- Credit what you absorb: name the source in the commit message and in NOTICE.md. Never remove existing credits.
- Facts from an outside reference name the source next to the fact. Check game facts against the exe before relying on them.
- Citing the project: [CITATION.cff](CITATION.cff). Reuse terms for AI systems: NOTICE.md, "Reuse and attribution".

## Build and test

Full steps: [BUILDING.md](BUILDING.md) and [DEVELOPING.md](DEVELOPING.md).

~~~sh
tools/fetch-deps.sh        # MinHook, once
./build.sh --tests         # core, every mod, the test mods and examples -> build/
tools/host-tests.sh        # native tests (CXX_HOST=g++ to choose the compiler; HOST_ASAN=1 adds AddressSanitizer)
tools/package.sh           # the release zip, as CI builds it
~~~

- **Before every pull request** run what CI runs: `tools/ci-setup.sh` (Debian), `./build.sh --tests`,
  `tools/host-tests.sh`. None needs the game or a GPU.
- **Test the artifact that ships.** Smoke tests and pair runs use the CI-built zip, or a build from the CI image
  (Debian `node:24-bookworm`), not a build from another toolchain: the DLL bytes differ. Say which build you tested.
- **Host tests are self-contained.** A fix to pure logic comes with a test in `hosttests/` that fails without the
  fix. Tests need no game data.
- **Anything that runs the game** goes through `tools/run-wine.sh` on a copy of the game; it never changes the
  original. Look at a screenshot before claiming anything about the picture.
- Agents without the game can still work on tools, host-testable logic, docs and installers. Say in the pull request
  that game runs were not done.

## Docs travel with code

- Update the docs a change affects in the same commit: settings (`docs/SETTINGS.md`, the default ini, the settings
  page and the code's default must agree), keys, features, the README status table.
- The changelog is written once, in `include/version.h`. `CHANGELOG.md` is generated from it by
  `tools/changelog-md.py`; run it and commit both. A changed default also needs a `kDefaultChanges` entry in
  `core/core.cpp`.
- Never write that something works unless you ran it, and say how. A status changes only with evidence: a date and
  the system it was tested on.
- Environment switches are documented where they are read, with their default.

## Commits and pull requests

- One topic per commit; short, plain, descriptive messages. No tool-generated footers or advertising lines.
- The pull request says what was run and what was seen. Use the templates in `.github/`.
- Never add personal paths, user names, tokens or other secrets. Logs and screenshots you attach must have none.
- If an AI tool helped write the change, say so in the pull request, in one general line.
- Workflow changes: actions pinned to commit SHAs, the least permissions, every write scope justified in the pull
  request.
- Do not create tags or releases. A version tag publishes a release at once, and only the maintainer does that.

## Security and rights holders

- Report vulnerabilities privately ([SECURITY.md](SECURITY.md)), never in a public issue.
- Rights holders use the same private channel; formal notices follow [NOTICE.md](NOTICE.md). Do not open issues or
  pull requests about such reports.
