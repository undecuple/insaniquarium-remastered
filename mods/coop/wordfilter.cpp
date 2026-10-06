// See wordfilter.h. Checks on lower-cased words with leetspeak undone (0 o, 1 i, 3 e, 4 a, 5 s, 7 t, 8 b, @ a, $ s, ! i),
// spaced-out letters joined: words that are bad anywhere inside a word, short words that are only bad on their own (so
// "class" or "Scunthorpe" pass), and links.
#include "wordfilter.h"
#include <ctype.h>
#include <string.h>
#include <vector>

namespace wordfilter {
namespace {

// bad anywhere inside a word ("fuckface", "bullshit")
const char* const Anywhere[] = {
    "fuck", "shit", "nigg", "faggot", "bitch", "whore", "slut", "retard", "pussy", "asshole", "bastard", "motherf",
    "dickhead", "cocksuck", "wanker", "twat", "jizz", "dildo", "porn", "hentai", "hitler", "killyourself", "molest",
};
// bad only as a whole word (inside longer words they're innocent: "class", "Scunthorpe", "therapist", "torpedo")
const char* const Whole[] = {
    "ass", "arse", "fag", "fags", "cock", "dick", "dicks", "tit", "tits", "cum", "sex", "rape", "raped", "rapist", "nude",
    "nudes", "hoe", "hoes", "piss", "wank", "boob", "boobs", "penis", "vagina", "anal", "homo", "coon", "cunt", "cunts",
    "fuk", "fck", "kys", "kkk", "nazi", "nazis", "pedo", "pedos", "spic", "chink", "kike", "tranny",
};
// top-level domains that make "word.tld" a link
const char* const Tlds[] = {
    "com", "net", "org", "io", "gg", "xyz", "ru", "de", "uk", "co", "tv", "me", "ly", "app", "dev", "link", "site", "info",
    "biz", "us", "eu", "fr", "cn", "jp", "to", "cc", "be", "nl", "pl", "in", "top", "club", "online", "shop", "live",
};

char Unleet(char c)
{
    switch (c)
    {
        case '0': return 'o'; case '1': return 'i'; case '3': return 'e'; case '4': return 'a'; case '5': return 's';
        case '7': return 't'; case '8': return 'b'; case '@': return 'a'; case '$': return 's'; case '!': return 'i';
        default: return (char)tolower((unsigned char)c);
    }
}

bool Link(const std::string& low)
{
    if (low.find("http") != std::string::npos || low.find("www.") != std::string::npos || low.find("://") != std::string::npos) return true;
    // word.tld and "word dot tld" (also with spaces around the dot)
    std::string t;
    for (size_t i = 0; i < low.size(); i++)
    {
        if (low.compare(i, 5, " dot ") == 0) { t += '.'; i += 4; continue; }
        if (low[i] == ' ' && i + 1 < low.size() && low[i + 1] == '.') continue;
        if (low[i] == ' ' && i > 0 && low[i - 1] == '.') continue;
        t += low[i];
    }
    for (size_t dot = t.find('.'); dot != std::string::npos; dot = t.find('.', dot + 1))
    {
        if (dot == 0 || !isalnum((unsigned char)t[dot - 1])) continue;
        size_t e = dot + 1;
        while (e < t.size() && isalpha((unsigned char)t[e])) e++;
        std::string tld = t.substr(dot + 1, e - dot - 1);
        for (const char* d : Tlds) if (tld == d) return true;
    }
    return false;
}

}  // namespace

bool Bad(const std::string& text)
{
    std::string low;
    for (char c : text) low += (char)tolower((unsigned char)c);
    if (Link(low)) return true;
    // words of letters after undoing leetspeak ("sh1t" -> "shit"); runs of single letters joined ("f u c k", "a.s.s")
    std::vector<std::string> words;
    std::string cur;
    for (char c : low) { char u = Unleet(c); if (isalpha((unsigned char)u)) cur += u; else if (!cur.empty()) { words.push_back(cur); cur.clear(); } }
    if (!cur.empty()) words.push_back(cur);
    std::string run;
    size_t n = words.size();
    for (size_t i = 0; i < n; i++)
    {
        if (words[i].size() == 1) run += words[i];
        if (words[i].size() != 1 || i + 1 == n) { if (run.size() > 1) words.push_back(run); run.clear(); }
    }
    for (auto& w : words)
    {
        std::string sq;   // repeated letters squeezed ("fuuuck" -> "fuck", "asss" -> "as")
        for (char c : w) if (sq.empty() || sq.back() != c) sq += c;
        for (const char* b : Anywhere) if (w.find(b) != std::string::npos || sq.find(b) != std::string::npos) return true;
        for (const char* b : Whole) if (w == b || sq == b) return true;
    }
    return false;
}

}  // namespace wordfilter
