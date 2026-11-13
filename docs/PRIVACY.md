# Privacy: what goes online and what a server keeps

This describes what the mod and the co-op server software do. It is not legal advice. Whoever runs a server decides
how long to keep things and what to tell players; the software's own behaviour is below. Nothing here is sent unless
you host or join a co-op game.

## From the mod (on your computer)

- **Nothing goes online by itself.** No updates, no statistics. Your profiles, achievements and records stay on your
  computer.
- **Co-op** connects to the address you typed (the host opens TCP port 27615), to Steam's lobbies and relay, or to a
  co-op server (the Online tab). The server is set in `[coop]` in `mods/remastered-mod.ini` (`server_host`; empty means the mod's public server).
- **Your game's anonymous id.** Online co-op logs in with a random device id the game makes on first use
  (`[coop] device=`). There is no sign-up and no password.

## What a server sees and stores (the Nakama server in `server/`)

- **Account:** the anonymous device id, a display name, and when it was created and last used. No
  e-mail, no real name.
- **Listings:** each public match being hosted (room code, mod version, player count) while it is open.
- **Match traffic:** the game's messages go through the server inside the match. The server does not store them, except chat lines when the operator turns on chat logging (below).
- **IP addresses:** like any web server, it and the HTTPS proxy in front of it see the address of each connection. The
  proxy's access log is where addresses are kept and blocked.
- **Chat (optional):** with `modules/chatlog.lua` installed, each chat line is written to the server's log with the
  room, account, display name and text, for moderation. The sender's address is not part of that line unless the
  operator turns `LOG_IP` on. Without the module, chat is not logged.

## What an operator should do

- **Tell players** what the server logs, how long it keeps it, who runs it and how to reach them. If chat logging is on,
  say so.
- **Set a retention schedule and write it down.** The software ships a cleanup call (`modules/cleanup.lua`, see
  [SERVER.md](SERVER.md)) that removes accounts older than a number of days (30 by default) and listings left behind
  for a day. It only runs when the operator calls it, for example once a day from cron. Chat lines and proxy logs live
  in log files: rotate and delete them on a schedule too.
- **Limit abuse in the server,** not only at the proxy: a proxy limiter does not limit messages inside an open
  WebSocket connection.
- **Honour requests.** Nakama's console can export and delete an account.

## Using someone else's server

Check who runs it and what they say. With `server_host` empty the mod uses its public server; the **Server** and **Direct** tabs of the co-op screen
let you use your own server or none.

Questions about this page: open an issue. Security problems: [SECURITY.md](../SECURITY.md).
