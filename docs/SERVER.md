# Hosting your own co-op server (Nakama)

For whoever wants to run a co-op server for their group, or a public one. **Players don't need this:** online co-op
uses the mod's public server (`insanicoop.mychud.net`) out of the box. Your own server is for groups that prefer to
keep their games on a machine they control (or a LAN party without internet):

| | The public server | Your own server | Steam |
|---|---|---|---|
| Needs | Nothing | A machine that stays on (a home server, a NAS, a small VPS) | Steam running, a `steam_api.dll` |
| Who can join | Anyone with the room code, or from the list | Games set to your server and its key | Anyone on Steam with the lobby or its code |
| Lobby list | Every listed game on the public server | Only your group's games | Shared with every Spacewar (App ID 480) game, filtered |
| Upkeep | None | Updates, a domain and HTTPS if it's on the internet | None |

## What the server is
[Nakama](https://heroiclabs.com/nakama/) is an open-source game server (Apache-2.0). Co-op uses three of its features:
- **accounts:** each game logs in anonymously with a random device id; no sign-up, no passwords for players;
- **relayed matches:** one room per co-op game, joined with a 5-character room code; the server forwards each player's
  inputs to the others;
- **storage:** listed games are public objects in the `remod_lobbies` collection, which **Find games** reads; private
  games aren't listed. Each account has one listing, under the key `game` (the room code is in its value), overwritten
  by every game it hosts. It carries a timestamp and is refreshed every 30 s while the game has room (players can join
  a game in progress); it's removed when the room closes or fills, and readers skip entries older than 2 minutes
  (nothing on the server cleans up after a game that vanished). If the server says an account has too many listings
  (left by older versions), the game deletes that account's own and lists again.

The server runs no game code (no modules to install): all players' games simulate the same tank in lockstep. Traffic
is small: a few KB/s per player. A 1-CPU, 1 GB VPS is plenty.

## What you need
- A Linux machine (or Windows/macOS with Docker Desktop) with **Docker** and the **Compose** plugin
  (`docker compose version` should work).
- For players outside your home: either a **VPN** you all share (Tailscale, ZeroTier, WireGuard: simplest, no ports
  to open), or a **public address** with a **domain name** for HTTPS.
- The `server/` folder from this project: `docker-compose.yml`, `docker-compose.caddy.yml`, `.example.env`.

## Setting it up
1. Copy the `server/` folder to the machine, open a terminal in it.
2. Create the settings and fill in the secrets:
   ```sh
   cp .example.env .env
   for k in SERVER SESSION REFRESH HTTP; do echo "NAKAMA_${k}_KEY=$(openssl rand -hex 24)"; done
   ```
   Paste those four lines over the `change-me` ones in `.env`, and set your own `NAKAMA_CONSOLE_PASSWORD`,
   `NAKAMA_CONSOLE_SIGNING_KEY` and `POSTGRES_PASSWORD` (`openssl rand -hex 24` again). No `change-me` should remain.
3. Pick how players reach it:
   - **Home network or VPN:** `docker compose up -d`. The server listens on port **7350**. Its address is the
     machine's LAN or VPN IP, e.g. `http://192.168.1.20:7350` or `http://100.101.102.103:7350`.
   - **Internet, with HTTPS (recommended for the internet):** set `SERVER_DOMAIN` in `.env` (e.g.
     `coop.example.com`), point that name's DNS **A/AAAA record** at the machine, open ports **80** and **443**
     (router port-forward and/or firewall), then:
     ```sh
     docker compose -f docker-compose.yml -f docker-compose.caddy.yml up -d
     ```
     Caddy fetches a Let's Encrypt certificate by itself. Port 7350 is then closed and the address is
     `https://coop.example.com`.
   - **Internet, without a domain:** open/forward TCP **7350** and use `http://<public IP>:7350`. It works, but logins
     and game traffic aren't encrypted. Prefer a VPN or the HTTPS option.
   - **You already run a reverse proxy** (nginx, Traefik, Caddy): set `NAKAMA_API_BIND=127.0.0.1` (or join its Docker
     network), and proxy your domain to `nakama:7350` / `127.0.0.1:7350`. It must pass **WebSocket upgrades** (the
     game's live connection is `/ws`) and allow long-lived connections (timeouts of a few minutes or more).
4. Check that it answers (from another machine, with your address):
   ```sh
   curl http://192.168.1.20:7350/healthcheck          # or https://coop.example.com/healthcheck
   # {}  = running
   curl -u "YOUR_SERVER_KEY:" -H 'Content-Type: application/json' -d '{"id":"test-device-000000"}' \
     "http://192.168.1.20:7350/v2/account/authenticate/device?create=true"
   # {"created":true,"token":"..."}  = the server key is right and logins work
   ```
5. Give your players two things: the **address** and the **server key** (`NAKAMA_SERVER_KEY`). Keep everything else in
   `.env` to yourself.

## Connecting the game
In the game: co-op screen (**Co-op** on the Remastered page, or **F7**) → **Online** tab → **Server settings**:

| Field | Your server on the internet (with Caddy) | Your server on the LAN / VPN |
|---|---|---|
| Server | `coop.example.com` | `192.168.1.20` |
| Port | `443` | `7350` |
| TLS | on (https) | off (http) |
| Key | your `NAKAMA_SERVER_KEY` | your `NAKAMA_SERVER_KEY` |

**Test connection** checks it: the healthcheck, a login and the version (messages below). **Public server** puts the
mod's default back. The values are kept in `mods/remastered-mod.ini`, so you can also hand players these lines:
```ini
[coop]
server_host=coop.example.com
server_port=443
server_tls=1
key=the-server-key
```
**The server key isn't a password:** every player of that server has it in their settings. It keeps games that don't
know it out; nothing more. The secrets are the other keys in `.env`.

The Online tab then shows **Host: listed** (in everyone's **Find games**), **Host: private** (only for those with the
room code) and **Find games**. The lobby shows the room code (5 characters, e.g. `3RJNN`) and which server it belongs
to: codes only work on the server they were made on. Friends type or paste the code into the address box and press
Join. Each game logs in with an anonymous id of its own (`[coop] device=`, made on first use).

### What the game sends (for your own proxy)
Every request has a `User-Agent: InsaniquariumRemod/<version>` header. Paths the mod uses:

| Path | Method | What for |
|---|---|---|
| `/healthcheck` | GET | the connection test |
| `/v2/account/authenticate/custom?create=true` | POST | login (HTTP Basic: the server key as user, empty password; body `{"id": "<device id>"}`) |
| `/v2/storage` | PUT | listing a game (`remod_lobbies`, key `game`, public read, owner write) |
| `/v2/storage/delete` | PUT | unlisting it (or the account's old listings) |
| `/v2/storage/remod_lobbies?limit=100` | GET | **Find games** |
| `/v2/account` | GET | the account's own id, only when old listings must be cleared |
| `/v2/storage/remod_lobbies?user_id=...&limit=100` | GET | the account's own listings, only then |
| `/ws?lang=en&status=false&format=json&token=...` | GET (WebSocket) | the game connection: `match_create` (by room name), `match_data_send`, `match_leave`; it receives `match`, `match_presence_event`, `match_data` |

Messages are at most about 2.5 KB each (bigger ones, like the game state a joining player gets, are sent in pieces),
so Nakama's default limits are fine; the compose file allows 64 KB anyway. A login token is reused for a day; a dropped
WebSocket is reopened with growing waits (1 s to 30 s). A `429` with `Retry-After` is respected, so a rate limit in your
proxy (a few hundred requests per minute per IP is plenty) only slows abusers. If you put the server behind Cloudflare:
WebSockets work on every plan, but turn off bot challenges for this hostname (the game can't solve them).

### Requiring a newer mod
To turn away old versions (after a change that needs one), have your proxy answer `426 Upgrade Required` to requests
whose `User-Agent` has an older `InsaniquariumRemod/<version>`. The game then says "this server needs a newer version
of the mod" instead of failing.

### Rules on the public server (recommended for yours)
The public server enforces these with a server module; the game follows them, so yours can too:
- **No chat channels:** `channel_join` and `channel_message_*` close the socket. The game's chat goes inside the match
  (player to player), not through Nakama's chat.
- **Storage:** only the `remod_lobbies` collection; each value a JSON object of at most 4 KB; at most 2 listings per
  account (the game overwrites its own and deletes it when the room closes). Anything else is refused with `400` or
  `403` and a message, which the game shows.
- **Text other players see** is filtered: lobby names with profanity or links are stored as `***`, a bad username at
  login is replaced with a random one, `PUT /v2/account` refuses bad usernames and any avatar URL. Names and chat lines
  inside match data aren't seen by the server, so the game filters those itself with the same kind of rules (profanity,
  also as leetspeak or spaced letters, and links such as `x.com`, `www.`, `http://`, `x dot com`): they show as `***`.

The module is a Lua file in `server/modules/` (mounted at `/nakama/data/modules`, loaded by Nakama at start); see
`server/modules/README.md`.

## Running it
- **Logs:** `docker compose logs -f nakama`
- **Admin console:** `http://127.0.0.1:7351` on the server (accounts, live matches, the `remod_lobbies` storage). From your
  own computer: `ssh -L 7351:127.0.0.1:7351 you@server`, then open `http://127.0.0.1:7351`. Don't expose port 7351.
- **Stop / start:** `docker compose down` / `docker compose up -d` (add the `-f` files if you use Caddy). Containers
  restart by themselves after a reboot.
- **Updating Nakama:** change the `heroiclabs/nakama:` version in `docker-compose.yml` (read its release notes first),
  then `docker compose pull && docker compose up -d`. The database is migrated at start.
- **Backups:** optional. The database only holds anonymous accounts and the lobby list. Nothing is lost if it's
  reset: `docker compose down -v` wipes it, and the next start makes a fresh one.
- **Tidying up (optional):** nothing a player keeps lives on the server (profiles, achievements and records stay on
  their computer), so old accounts can go. With `modules/cleanup.lua` in place, run this on the server once a day
  (cron: `0 5 * * *`); it removes accounts created more than 30 days ago and listings left behind for a day:
  ```sh
  curl -s -X POST "http://127.0.0.1:7350/v2/rpc/remod_cleanup?http_key=$NAKAMA_HTTP_KEY&unwrap" -d '{"days":30}'
  # {"accounts_failed":0,"accounts_removed":12,"days":30,"listings_removed":0}
  ```
  A game whose account is gone makes a new one the next time it logs in; a player online at that moment is reconnected
  as a new account and rejoins its game (hence a quiet hour). Players' sessions can't call it.
- **Chat log (optional):** the game's chat goes player to player inside the match, so the server stores none of it.
  With `modules/chatlog.lua` in place, each line is written to Nakama's log for moderation (room, account, username,
  text; `docker compose logs nakama | grep remod_chat`). It hooks every match message (`MatchDataSend`), changes
  nothing and costs little; Nakama allows one such hook, so don't load another module that hooks `MatchDataSend`.
  The sender's IP is off (`LOG_IP` in the file): your HTTPS proxy's access log already has addresses, and blocking one
  is done there (a banned account can always make a new one). Tell your players if you log chat.
- **Changing the server key:** edit `.env`, `docker compose up -d`, and give players the new key. Games with the old
  key can no longer log in.

## Troubleshooting
| Problem | What to check |
|---|---|
| `curl .../healthcheck` hangs or is refused | Containers running (`docker compose ps`)? Firewall/router port (7350, or 80+443 with Caddy)? Using the machine's LAN/VPN/public IP, not `127.0.0.1`, from another machine? |
| **Test connection** says "wrong server key" | The key in the game doesn't match `NAKAMA_SERVER_KEY` exactly. |
| "path not allowed, or not a Nakama server" | The address points at something else (a website, a proxy that blocks the path): check the host and port, and the paths above. |
| "certificate problem" | HTTPS on a server without a certificate (on a LAN, use TLS off and port 7350), or Caddy has no certificate yet. |
| "too many requests, retrying in N s" | A rate limit in your proxy; the game waits by itself. |
| Caddy has no certificate | DNS record not pointing at the machine yet, or port 80 blocked (Let's Encrypt checks through it): `docker compose logs caddy`. |
| Joins work, then drop after a minute behind your own proxy | The proxy isn't passing WebSockets or times them out: enable upgrades on `/ws` and raise its read timeout. |
| "No game found with the code ..." | The host's game isn't up, the code is from another server, or a typo. |
| `nakama` keeps restarting | `docker compose logs nakama`: usually a wrong `POSTGRES_PASSWORD` after changing it (the database keeps the first one; `docker compose down -v` resets it). |

## Security notes
- Players are anonymous: the server stores a random device id per game, a display name, and listed lobbies. No
  emails, no passwords.
- The server key keeps other games out but is shared with every player: it isn't a secret. Change it if your group
  wants strangers out again.
- Keep the session, refresh, HTTP and console keys and the database password private, and the console on
  `127.0.0.1`.
- Update the images now and then (`docker compose pull`), and the host's OS.
