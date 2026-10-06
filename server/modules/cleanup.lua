-- Insaniquarium - Remastered Mod: tidying the co-op server. Nothing the game keeps lives on the server (profiles,
-- achievements and records stay on each player's computer); the server only has a login account per game copy and the
-- listings of games being hosted. This removes what's left over:
--   - accounts created more than `days` days ago (default 30): a game whose account is gone simply makes a new one the
--     next time it logs in (a player online at that moment is reconnected as a new account and rejoins its game);
--   - listings not refreshed for a day (a hosting game refreshes its own every 30 s and removes it when it closes).
-- An RPC for the server's owner only: it refuses calls from players' sessions, and the paths a proxy lets through don't
-- include /v2/rpc. Call it on the server once a day, e.g. from cron:
--   curl -s -X POST "http://127.0.0.1:7350/v2/rpc/remod_cleanup?http_key=$NAKAMA_HTTP_KEY&unwrap" -d '{"days":30}'
-- Accounts are deleted through Nakama (nk.account_delete_id), which also ends their sessions and removes their storage.
local nk = require("nakama")

local function cleanup(context, payload)
  if context.user_id and context.user_id ~= "" then
    error({ "server only", 7 })   -- PERMISSION_DENIED for a player's session
  end
  local days = 30
  if payload and payload ~= "" then
    local ok, args = pcall(nk.json_decode, payload)
    if ok and type(args) == "table" and tonumber(args.days) then days = math.floor(tonumber(args.days)) end
  end
  if days < 1 then days = 1 end

  local removed, failed = 0, 0
  while true do   -- in batches, so a big backlog doesn't hold one long query
    local rows = nk.sql_query([[SELECT id FROM users WHERE id <> '00000000-0000-0000-0000-000000000000'
                                AND create_time < now() - make_interval(days => $1) LIMIT 500]], { days })
    if #rows == 0 then break end
    for _, row in ipairs(rows) do
      local ok = pcall(nk.account_delete_id, row.id, false)
      if ok then removed = removed + 1 else failed = failed + 1 end
    end
    if failed > 0 and #rows == failed then break end   -- nothing more can go: don't loop on the same rows
  end

  local stale = nk.sql_exec([[DELETE FROM storage WHERE collection = 'remod_lobbies'
                               AND update_time < now() - interval '1 day']], {})

  local result = { accounts_removed = removed, accounts_failed = failed, listings_removed = stale, days = days }
  nk.logger_info(("remod_cleanup: %d accounts older than %d days removed (%d failed), %d old listings removed")
    :format(removed, days, failed, stale))
  return nk.json_encode(result)
end

nk.register_rpc(cleanup, "remod_cleanup")
