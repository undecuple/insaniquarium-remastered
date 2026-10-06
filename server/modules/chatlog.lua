-- Insaniquarium - Remastered Mod: optional chat log for the co-op server. The game's chat goes inside the match (player
-- to player through the relay), so the server never stores it; with this module each chat line is written to Nakama's
-- log, for moderation:
--   docker compose logs nakama | grep remod_chat
-- {"msg":"remod_chat","match":"…","user_id":"…","username":"…","slot":1,"text":"hello"}
-- It looks at every match message (a hook on MatchDataSend, cheap: one byte compare for the ones that aren't chat) and
-- never changes or drops any. Tell your players if you turn it on (the public server's README says so).
--
-- The game's message inside the match data: 2-byte length, 1-byte type (14 = chat), then the payload. A guest's line to
-- the host is the text itself; the host's copy to the others starts with the speaker's slot byte (0 = the host). Lines
-- the host relays are logged only when the host itself spoke (slot 0), so each line is logged once per receiver of the
-- host's own lines at most.
local nk = require("nakama")

local OP_DATA = 1     -- a whole message (OpData in mods/coop/online.cpp); bigger ones come in parts and are never chat
local TYPE_CHAT = 14  -- MChat in mods/coop/coop.cpp
-- the sender's IP address with each line: off. An IP is personal data (tell players, keep logs briefly); the HTTPS
-- proxy in front of the server already logs every connection's address, and is where an address is blocked.
local LOG_IP = false

local function on_match_data(context, envelope)
  local m = envelope.match_data_send
  if not m or tonumber(m.op_code) ~= OP_DATA then return envelope end
  if type(m.data) ~= "string" or #m.data < 8 or #m.data > 400 then return envelope end   -- chat lines are short
  local ok, data = pcall(nk.base64_decode, m.data)   -- the hook gets the data base64-encoded, as on the wire
  if not ok or #data < 4 or data:byte(3) ~= TYPE_CHAT then return envelope end
  local first = data:byte(4)
  local slot, text
  if first < 32 then
    slot, text = first, data:sub(5)
    if slot ~= 0 then return envelope end   -- the host passing a guest's line on: logged when the guest sent it
  else
    slot, text = nil, data:sub(4)
  end
  nk.logger_info(nk.json_encode({ msg = "remod_chat", match = m.match_id, user_id = context.user_id,
                                  username = context.username, slot = slot, text = text,
                                  ip = LOG_IP and context.client_ip or nil }))
  return envelope
end

nk.register_rt_before(on_match_data, "MatchDataSend")
