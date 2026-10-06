// Text other players see that the server doesn't check (names and chat lines inside the game's own messages): replaced
// with "***" when it contains profanity (also as leetspeak or with spaced or dotted letters) or a link (x.com, www.,
// http://, "x dot com"), the same rule the co-op server applies to lobby names.
#pragma once
#include <string>

namespace wordfilter {
bool Bad(const std::string& text);
inline std::string Clean(const std::string& text) { return Bad(text) ? "***" : text; }
}
