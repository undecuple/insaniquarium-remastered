# Security Policy

## Supported versions

Only the latest release gets fixes. Please update before reporting.

| Version | Supported |
|---|---|
| 0.2.x (latest) | :white_check_mark: |
| < 0.2 | :x: |

## Reporting a vulnerability

Please report it privately: on this repository's **Security** tab, choose **Report a vulnerability**. Don't open a
public issue for it.

Include what you found, how to reproduce it (the steps, and `mods/remastered-mod.log` from the game folder if the game
was involved), and what someone could do with it. You'll get an answer within a week. If it's confirmed, the fix goes
into the next release and the release notes credit you (unless you'd rather not be named). If it isn't, you'll get
an explanation.

## What counts

The mod runs native code inside the game, so these matter most:

- **Co-op:** anything another player (or a server) can send that crashes your game, reads or writes memory it
  shouldn't, or runs code on your machine.
- **The loader and the mods:** loading code from anywhere but the game's `mods` folder, or writing outside the game's
  own folders.
- **The installers** (`install-steam.bat`, `install-steam.sh`): deleting or changing files that aren't the mod's.
- **The co-op server files** (`server/`): settings or modules that expose more than they should.

Not a vulnerability:

- The public server's **server key**: it ships with the game so players can log in (it keeps other games out,
  nothing more); the server's real secrets are elsewhere.
- Problems in the game itself, in Nakama, in Steam or in Wine/Proton: please report those to their makers.
- Reports from automated scanners without a way to trigger them: every one is reviewed, but a `memcpy` or a format
  call isn't a bug unless its sizes or format can actually go wrong.
- Cheating in a co-op game you host or join: players trust the host's game by design.
