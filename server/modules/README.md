# Server modules

Nakama loads the Lua files in this folder at start (mounted at `/nakama/data/modules` by `docker-compose.yml`). The
game itself needs no modules: these only enforce the rules the public server uses, so a server open to strangers stays
tidy:

- **no chat channels** (`channel_join`, `channel_message_*` close the socket; the game's chat goes inside the match);
- **storage:** only the `remod_lobbies` collection, values that are JSON objects of at most 4 KB, at most 2 listings
  per account;
- **text filter:** lobby strings with profanity or links are stored as `***`, bad usernames at login are replaced,
  `PUT /v2/account` refuses bad usernames and avatar URLs.

`chatlog.lua` (included, optional) writes every co-op chat line to Nakama's log (`remod_chat`) for moderation; it hooks
`MatchDataSend`, so no other loaded module may hook that message. See *Running it* in the server guide.

`cleanup.lua` (included) adds `remod_cleanup`, an RPC for the server's owner that removes accounts older than a number of
days and listings left behind: see *Running it* in [../../docs/SERVER.md](../../docs/SERVER.md). It registers no hooks,
so it works next to `wordfilter.lua`.

`wordfilter.lua` is the public server's module (its word lists and the hooks above). Put it here and restart the
server (`docker compose up -d --force-recreate nakama`); the log then says it was loaded. Without it the server works
the same, just without these rules: fine for a group of friends.
