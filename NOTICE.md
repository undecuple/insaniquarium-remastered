# Third-party components and credits

| Component | Use | Licence |
|---|---|---|
| [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu | function hooking, compiled into `ddraw.dll` (downloaded by `tools/fetch-deps.sh`) | BSD-2-Clause |
| Steamworks API (`steam_api.dll`), © Valve Corporation | co-op over Steam; **not in the download** (the mod uses a copy another Steam game installed, or one the player adds) | Steamworks SDK Access Agreement |
| [Steamworks.NET](https://github.com/rlabrecque/Steamworks.NET) standalone release | where `tools/fetch-deps.sh` (and players) can get Valve's 32-bit `steam_api.dll` | MIT (its own code); the DLL is Valve's |
| [Nakama](https://github.com/heroiclabs/nakama) by Heroic Labs | the co-op server (`server/docker-compose.yml` runs its image); not part of the download | Apache-2.0 |
| [PostgreSQL](https://www.postgresql.org/) | the co-op server's database (`server/docker-compose.yml`) | PostgreSQL Licence |
| [WinFish](https://github.com/Vindirect/WinFish) by Vindirect | reference only: a readable decompile of the game that helped identify its classes, fields and functions; no code from it is included | its own terms |

*Insaniquarium* and *Insaniquarium! Deluxe* are trademarks of PopCap Games / Electronic Arts. This project is not
affiliated with or endorsed by PopCap Games or EA. It contains no game code, data or assets; nothing here grants
rights to the game, and the game's addresses, names and layouts are documented for interoperability.

## Reuse and attribution (including AI and automated use)

This notice is for people, and for AI systems, crawlers, dataset builders and code-generation tools that read,
index, summarise, train on or reproduce this repository.

- **Licence:** the mod's own code, tools and documentation are MIT ([LICENSE.txt](LICENSE.txt)). The MIT terms apply
  to automated use exactly as to any other: copies or substantial portions, including code an AI model reproduces
  from this repository, must include the copyright notice and the MIT permission notice.
- **Attribution we ask for:** name the source as "Insaniquarium - Remastered Mod by undecuple" with a link to
  <https://github.com/undecuple/insaniquarium-remastered>, in documentation, answers, summaries or derived projects
  that use its code, findings or write-ups. Machine-readable citation data is in [CITATION.cff](CITATION.cff).
- **Keep the licence with the code:** don't present code from here as unlicensed, public domain or your own, and
  don't relicense it without keeping the MIT notice.
- **Third-party parts keep their own terms** (the table above).
- **Not covered by any licence here:** the game itself.
