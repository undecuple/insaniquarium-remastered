// The co-op code that needs no Windows: JSON, base64, URLs and room codes, shared by online.cpp and the host tests
// (hosttests/, run by tools/host-tests.sh). Moved here from online.cpp unchanged, except that Url::port is an unsigned
// short (INTERNET_PORT is the same type); nothing in it touches the network or the game.
#pragma once
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <utility>
#include <vector>

namespace remodcodec {

// a query-string value: letters, digits and -_.~ as they are, the rest %XX
inline std::string UrlEncode(const std::string& v)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : v)
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    return o;
}


struct Json
{
    enum Kind { Null, Bool, Num, Str, Arr, Obj } kind = Null;
    double num = 0; bool b = false; std::string str;
    std::vector<Json> arr; std::vector<std::pair<std::string, Json>> obj;
    const Json& operator[](const char* k) const
    {
        static const Json none;
        for (auto& p : obj) if (p.first == k) return p.second;
        return none;
    }
    bool has(const char* k) const { for (auto& p : obj) if (p.first == k) return true; return false; }
    std::string text() const { return kind == Str ? str : kind == Num ? std::to_string((long long)num) : ""; }
    long long integer() const { return kind == Num ? (long long)num : kind == Str ? atoll(str.c_str()) : 0; }
};
struct Parser
{
    const char* p;
    void ws() { while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++; }
    bool value(Json& j, int depth = 0)
    {
        if (depth > 32) return false;
        ws();
        if (*p == '{' || *p == '[')
        {
            bool isObj = *p++ == '{';
            j.kind = isObj ? Json::Obj : Json::Arr;
            for (;;)
            {
                ws();
                if (*p == (isObj ? '}' : ']')) { p++; return true; }
                Json k, v;
                if (isObj) { if (*p != '"' || !value(k, depth + 1)) return false; ws(); if (*p++ != ':') return false; }
                if (!value(v, depth + 1)) return false;
                if (isObj) j.obj.push_back({ k.str, std::move(v) }); else j.arr.push_back(std::move(v));
                ws();
                if (*p == ',') p++;
                else if (*p != (isObj ? '}' : ']')) return false;
            }
        }
        if (*p == '"')
        {
            p++; j.kind = Json::Str;
            while (*p && *p != '"')
            {
                if (*p != '\\') { j.str += *p++; continue; }
                char e = *++p;
                if (!e) return false;
                p++;
                switch (e)
                {
                    case 'n': j.str += '\n'; break;
                    case 't': j.str += '\t'; break;
                    case 'r': j.str += '\r'; break;
                    case 'b': j.str += '\b'; break;
                    case 'f': j.str += '\f'; break;
                    case 'u':
                    {
                        unsigned c = 0;
                        for (int i = 0; i < 4 && isxdigit((unsigned char)*p); i++, p++) c = c * 16 + (isdigit((unsigned char)*p) ? *p - '0' : (tolower(*p) - 'a' + 10));
                        if (c < 0x80) j.str += (char)c;                                   // UTF-8 for the rest
                        else if (c < 0x800) { j.str += (char)(0xc0 | c >> 6); j.str += (char)(0x80 | (c & 0x3f)); }
                        else { j.str += (char)(0xe0 | c >> 12); j.str += (char)(0x80 | ((c >> 6) & 0x3f)); j.str += (char)(0x80 | (c & 0x3f)); }
                        break;
                    }
                    default: j.str += e;   // \" \\ \/
                }
            }
            if (*p != '"') return false;
            p++; return true;
        }
        if (!strncmp(p, "true", 4)) { p += 4; j.kind = Json::Bool; j.b = true; return true; }
        if (!strncmp(p, "false", 5)) { p += 5; j.kind = Json::Bool; return true; }
        if (!strncmp(p, "null", 4)) { p += 4; return true; }
        char* end;
        j.num = strtod(p, &end);
        if (end == p) return false;
        p = end; j.kind = Json::Num; return true;
    }
};
inline bool Parse(const std::string& s, Json& j) { Parser ps{ s.c_str() }; return ps.value(j); }
inline std::string Quote(const std::string& s)
{
    std::string r = "\"";
    for (unsigned char c : s)
    {
        if (c == '"' || c == '\\') { r += '\\'; r += (char)c; }
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); r += b; }
        else r += (char)c;
    }
    return r + "\"";
}

inline constexpr char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
inline std::string ToB64(const std::string& in)
{
    std::string out;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3)
    {
        uint32_t v = (uint8_t)in[i] << 16 | (uint8_t)in[i + 1] << 8 | (uint8_t)in[i + 2];
        out += B64[v >> 18]; out += B64[(v >> 12) & 63]; out += B64[(v >> 6) & 63]; out += B64[v & 63];
    }
    if (i + 1 == in.size()) { uint32_t v = (uint8_t)in[i] << 16; out += B64[v >> 18]; out += B64[(v >> 12) & 63]; out += "=="; }
    else if (i + 2 == in.size()) { uint32_t v = (uint8_t)in[i] << 16 | (uint8_t)in[i + 1] << 8; out += B64[v >> 18]; out += B64[(v >> 12) & 63]; out += B64[(v >> 6) & 63]; out += '='; }
    return out;
}
inline std::string FromB64(const std::string& in)
{
    std::string out;
    uint32_t v = 0; int bits = 0;
    for (char c : in)
    {
        const char* k = c == '-' ? B64 + 62 : c == '_' ? B64 + 63 : strchr(B64, c);   // standard or URL-safe
        if (!c || !k) continue;
        v = v << 6 | (uint32_t)(k - B64); bits += 6;
        if (bits >= 8) { bits -= 8; out += (char)((v >> bits) & 0xff); }
    }
    return out;
}


struct Url { bool https = false; std::string host, prefix; unsigned short port = 0; };   // port: WinHTTP's INTERNET_PORT is the same type
inline bool ParseUrl(std::string s, Url& u)
{
    if (!s.compare(0, 8, "https://")) { u.https = true; s = s.substr(8); }
    else if (!s.compare(0, 7, "http://")) s = s.substr(7);
    else return false;
    size_t slash = s.find('/');
    std::string hp = s.substr(0, slash);
    u.prefix = slash == std::string::npos ? "" : s.substr(slash);
    while (!u.prefix.empty() && u.prefix.back() == '/') u.prefix.pop_back();
    size_t colon = hp.rfind(':');
    u.port = u.https ? 443 : 80;
    if (colon != std::string::npos && hp.find(']') == std::string::npos) { u.port = (unsigned short)atoi(hp.c_str() + colon + 1); hp = hp.substr(0, colon); }
    u.host = hp;
    return !u.host.empty() && u.port;
}

inline constexpr char Alphabet[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";   // room codes: no 0/O, 1/I/L
inline std::string CleanCode(const std::string& s)
{
    std::string r;
    for (char c : s) { char u = (char)toupper((unsigned char)c); if (strchr(Alphabet, u) && u) r += u; }
    return r;
}
inline bool IsCode(const std::string& s)
{
    if (s.size() != 5) return false;
    for (char c : s) { char u = (char)toupper((unsigned char)c); if (!u || !strchr(Alphabet, u)) return false; }
    return true;
}

} // namespace remodcodec
