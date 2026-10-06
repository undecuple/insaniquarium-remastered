// Co-op through your own Nakama server: see online.h. Nakama's REST API (login, storage) and its realtime WebSocket
// (JSON format) through WinHTTP. One worker thread per game logs in, opens the WebSocket, creates the match and sends;
// a second thread receives. The game's thread only queues messages and picks up what arrived (Frame/Receive).
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include "online.h"
#include "version.h"

namespace online {
namespace {

const char* const Collection = "remod_lobbies";       // listed games (public storage objects)
const char Alphabet[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";   // room codes: no 0/O, 1/I/L
enum { OpData = 1, OpHost = 2, OpPart = 3, OpClosed = 4 };  // match data op codes: a whole message or its last
const size_t Chunk = 1500;                             // piece, "I'm the host", a piece, "not taking players"
                                                       // (Nakama's default WebSocket message limit is 4 KB)

void (*logf)(const char*);
void Log(const char* fmt, ...)
{
    if (!logf) return;
    char b[400]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
    logf(b);
}

// ---- small helpers: JSON, base64, random -------------------------------------------------------------------------
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
bool Parse(const std::string& s, Json& j) { Parser ps{ s.c_str() }; return ps.value(j); }
std::string Quote(const std::string& s)
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

const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string ToB64(const std::string& in)
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
std::string FromB64(const std::string& in)
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

uint64_t RandomBits()
{
    static uint64_t s = 0;
    if (!s) { LARGE_INTEGER q; QueryPerformanceCounter(&q); s = (uint64_t)q.QuadPart ^ ((uint64_t)GetCurrentProcessId() << 32) ^ GetTickCount() ^ 0x9e3779b97f4a7c15ULL; }
    s ^= s << 13; s ^= s >> 7; s ^= s << 17;   // never the game's random numbers: those must stay in step
    return s;
}

// ---- HTTP through WinHTTP ----------------------------------------------------------------------------------------
std::wstring Wide(const std::string& s)
{
    std::wstring w(s.size() + 1, L'\0');
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], (int)w.size());
    w.resize(n > 0 ? n - 1 : 0);
    return w;
}
struct Url { bool https = false; std::string host, prefix; INTERNET_PORT port = 0; };
bool ParseUrl(std::string s, Url& u)
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
    if (colon != std::string::npos && hp.find(']') == std::string::npos) { u.port = (INTERNET_PORT)atoi(hp.c_str() + colon + 1); hp = hp.substr(0, colon); }
    u.host = hp;
    return !u.host.empty() && u.port;
}
struct Handle
{
    HINTERNET h = nullptr;
    Handle(HINTERNET x = nullptr) : h(x) {}
    ~Handle() { if (h) WinHttpCloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    operator HINTERNET() const { return h; }
};
// every request says what it is and which version (the server's proxy wants a User-Agent; a server that needs a newer
// mod can tell from it)
HINTERNET OpenSession() { return WinHttpOpen(L"InsaniquariumRemod/" REMOD_VERSION_W, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0); }

// too many requests: the server said when to come back (Retry-After); until then nothing is sent
std::mutex gateMutex;
DWORD retryUntil;
int RetryIn() { std::lock_guard<std::mutex> lock(gateMutex); long d = (long)(retryUntil - GetTickCount()); return d > 0 ? (int)((d + 999) / 1000) : 0; }
const int NoAnswer = 0, TlsError = -1;   // statuses besides HTTP's: no answer, a certificate problem

// one request; returns the HTTP status (or NoAnswer / TlsError) and the body
int Request(const Url& u, const wchar_t* verb, const std::string& path, const std::string& auth, const std::string& body, std::string& out)
{
    out.clear();
    if (RetryIn() > 0) return 429;
    Handle s(OpenSession());
    if (!s) return NoAnswer;
    WinHttpSetTimeouts(s, 8000, 8000, 10000, 10000);
    Handle c(WinHttpConnect(s, Wide(u.host).c_str(), u.port, 0));
    Handle r(c ? WinHttpOpenRequest(c, verb, Wide(u.prefix + path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, u.https ? WINHTTP_FLAG_SECURE : 0) : nullptr);
    if (!r) return NoAnswer;
    std::wstring headers = L"Content-Type: application/json\r\n";
    if (!auth.empty()) headers += L"Authorization: " + Wide(auth) + L"\r\n";
    if (!WinHttpSendRequest(r, headers.c_str(), (DWORD)-1, (void*)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0) || !WinHttpReceiveResponse(r, nullptr))
    {
        DWORD e = GetLastError();
        return e == ERROR_WINHTTP_SECURE_FAILURE || e == ERROR_WINHTTP_SECURE_INVALID_CA || e == ERROR_WINHTTP_SECURE_CERT_CN_INVALID ||
               e == ERROR_WINHTTP_SECURE_CERT_DATE_INVALID || e == ERROR_WINHTTP_SECURE_CHANNEL_ERROR ? TlsError : NoAnswer;
    }
    DWORD status = 0, len = sizeof status;
    WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
    if (status == 429)
    {
        DWORD after = 0; len = sizeof after;
        if (!WinHttpQueryHeaders(r, WINHTTP_QUERY_RETRY_AFTER | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &after, &len, WINHTTP_NO_HEADER_INDEX) || after == 0) after = 10;
        std::lock_guard<std::mutex> lock(gateMutex);
        retryUntil = GetTickCount() + std::min<DWORD>(after, 600) * 1000;
    }
    for (;;)
    {
        DWORD avail = 0, got = 0;
        if (!WinHttpQueryDataAvailable(r, &avail) || !avail || out.size() > (4u << 20)) break;
        size_t at = out.size();
        out.resize(at + avail);
        if (!WinHttpReadData(r, &out[at], avail, &got)) { out.resize(at); break; }
        out.resize(at + got);
    }
    return (int)status;
}

// what a failed request means, for the player
std::string Describe(int st)
{
    if (st == TlsError) return "certificate problem (for a server on your own network, try TLS off)";
    if (st == NoAnswer) return "can't reach the server";
    if (st == 401) return "wrong server key";
    if (st == 404) return "path not allowed, or not a Nakama server";
    if (st == 413) return "a message was too large for the server";
    if (st == 426) return "this server needs a newer version of the mod";
    if (st == 429) return "too many requests, retrying in " + std::to_string(std::max(RetryIn(), 1)) + " s";
    if (st >= 500) return "the server is down (" + std::to_string(st) + ")";
    return "the server answered " + std::to_string(st);
}

// the login token is kept for a day (the server's tokens last 24 h): one login, not one per request
std::mutex tokenMutex;
std::string tokenFor, tokenCached;
DWORD tokenAt;
std::string Login(const Url& u, const std::string& key, const std::string& device, std::string& why, bool fresh = false)
{
    std::string who = (u.https ? "s" : "") + u.host + ":" + std::to_string(u.port) + u.prefix + "|" + key + "|" + device;
    {
        std::lock_guard<std::mutex> lock(tokenMutex);
        if (!fresh && tokenFor == who && !tokenCached.empty() && GetTickCount() - tokenAt < 23u * 3600 * 1000) return tokenCached;
    }
    std::string out;
    int st = Request(u, L"POST", "/v2/account/authenticate/custom?create=true", "Basic " + ToB64(key + ":"), "{\"id\":" + Quote(device) + "}", out);
    Json j;
    if (st == 200 && Parse(out, j) && j["token"].kind == Json::Str)
    {
        std::lock_guard<std::mutex> lock(tokenMutex);
        tokenFor = who; tokenCached = j["token"].str; tokenAt = GetTickCount();
        return tokenCached;
    }
    why = Describe(st);
    return "";
}
void ForgetToken() { std::lock_guard<std::mutex> lock(tokenMutex); tokenCached.clear(); }
// a request with the login token; logs in again once if the token was refused
int Authed(const Url& u, const std::string& key, const std::string& device, const wchar_t* verb, const std::string& path, const std::string& body, std::string& out, std::string& why)
{
    std::string token = Login(u, key, device, why);
    if (token.empty()) return 401;
    int st = Request(u, verb, path, "Bearer " + token, body, out);
    if (st == 401)
    {
        token = Login(u, key, device, why, true);
        if (token.empty()) return 401;
        st = Request(u, verb, path, "Bearer " + token, body, out);
    }
    if (st != 200)
    {
        why = Describe(st);
        Json j;   // the server's own words for a refused request (storage rules: size, count, collection)
        if ((st == 400 || st == 403) && Parse(out, j) && !j["message"].str.empty()) why = j["message"].str + " (" + std::to_string(st) + ")";
    }
    return st;
}

// ---- one co-op game on the server ---------------------------------------------------------------------------------
struct Event { int type; std::string text; uint64_t id; };   // 0 created, 1 entered, 2 failed, 3 left, 4 reconnecting, 5 reconnected
const DWORD LeaveGrace = 30000;   // a player whose connection dropped has this long to come back before they count as gone
struct Session
{
    std::mutex m;
    Url url; std::string key, device, code, tag, name;
    bool host = false, listed = false;
    // shared, under m
    std::deque<Event> events;
    std::deque<std::pair<uint64_t, std::string>> inbox;
    std::deque<std::string> outbox;     // WebSocket frames to send
    bool stop = false, ready = false, everReady = false, failed = false, lost = false, joinable = true, listingChanged = true;
    int members = 1;
    std::string matchId, self;
    std::map<std::string, uint64_t> ids;        // user id -> our number for that player (the same after they reconnect)
    std::map<uint64_t, std::string> presences;  // our number -> their current presence, as JSON
    std::map<uint64_t, std::string> sessions;   // our number -> their current session id
    std::map<uint64_t, DWORD> leaving;          // dropped out: gone for good at this time unless they come back
    std::map<uint64_t, std::string> partial;    // pieces of a long message so far
    uint64_t nextId = 1, hostId = 0;
    DWORD readyAt = 0;
    HANDLE wake = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    ~Session() { CloseHandle(wake); }

    void Fail(const std::string& why) { if (!failed && !stop) { failed = true; events.push_back({ 2, why, 0 }); } }
    uint64_t IdFor(const Json& p)
    {
        std::string uid = p["user_id"].str, sid = p["session_id"].str;
        auto it = ids.find(uid);
        uint64_t id = it != ids.end() ? it->second : nextId++;
        ids[uid] = id;
        if (sessions[id] != sid)
        {
            sessions[id] = sid;
            presences[id] = "{\"user_id\":" + Quote(uid) + ",\"session_id\":" + Quote(sid) + ",\"username\":" + Quote(p["username"].str) + "}";
            partial.erase(id);
        }
        return id;
    }
    void SendOp(uint64_t to, int op, const std::string& data)
    {
        auto it = presences.find(to);
        if (it == presences.end() || matchId.empty() || lost) return;
        outbox.push_back("{\"match_data_send\":{\"match_id\":" + Quote(matchId) + ",\"op_code\":" + std::to_string(op) + ",\"data\":" + Quote(ToB64(data)) +
                         ",\"presences\":[" + it->second + "],\"reliable\":true}}");
        SetEvent(wake);
    }
    void Joined(const Json& p)
    {
        if (p["session_id"].str == self) return;
        uint64_t id = IdFor(p);
        if (leaving.erase(id)) Log("online: player %llu is back", (unsigned long long)id);
        if (host) SendOp(id, joinable ? OpHost : OpClosed, "");   // "I'm the host": the guest now talks to us
    }
    void OnMessage(const std::string& text)
    {
        Json j;
        if (!Parse(text, j)) return;
        std::lock_guard<std::mutex> lock(m);
        if (j.has("error")) { Fail("Server error: " + (j["error"]["message"].str.empty() ? text.substr(0, 120) : j["error"]["message"].str)); return; }
        if (j.has("match") && j["cid"].text() == "1")
        {
            const Json& mt = j["match"];
            matchId = mt["match_id"].str;
            self = mt["self"]["session_id"].str;
            ready = true; readyAt = GetTickCount();
            if (!everReady) { if (host) events.push_back({ 0, code, 0 }); }
            else events.push_back({ 5, "", 0 });
            everReady = true;
            for (auto& p : mt["presences"].arr) Joined(p);
            Log("online: in room %s as the %s", code.c_str(), host ? "host" : "guest");
            return;
        }
        if (j.has("match_presence_event"))
        {
            const Json& pe = j["match_presence_event"];
            for (auto& p : pe["joins"].arr) Joined(p);
            for (auto& p : pe["leaves"].arr)
            {
                auto it = ids.find(p["user_id"].str);
                if (it != ids.end() && sessions[it->second] == p["session_id"].str) leaving[it->second] = GetTickCount() + LeaveGrace;
            }
            return;
        }
        if (j.has("match_data"))
        {
            const Json& md = j["match_data"];
            uint64_t from = IdFor(md["presence"]);
            int op = (int)md["op_code"].integer();
            std::string data = FromB64(md["data"].str);
            if (op == OpHost)
            {
                if (host) { Fail("The room code " + code + " is already in use: host again for a new code"); return; }
                if (!hostId) { hostId = from; events.push_back({ 1, "", from }); }
            }
            else if (op == OpClosed) { if (!host) Fail("That game has already started or is full"); }
            else if (op == OpData || op == OpPart)
            {
                if (!host && from != hostId) return;     // a guest only listens to the host
                std::string& part = partial[from];
                part += data;
                if (op == OpData) { inbox.push_back({ from, part }); part.clear(); }
            }
        }
    }
};

std::shared_ptr<Session> cur;
Events ev;
std::string server, key, device;
// the "Find games" list: filled by a background thread
std::mutex listMutex;
std::vector<Lobby> lobbies, found;
bool listReady;
std::string listError;
DWORD lastRefresh;

void Receiver(std::shared_ptr<Session> s, HINTERNET ws)
{
    std::vector<char> buf(65536);
    std::string msg;
    for (;;)
    {
        DWORD got = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
        DWORD err = WinHttpWebSocketReceive(ws, buf.data(), (DWORD)buf.size(), &got, &type);
        if (err || type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
        {
            std::lock_guard<std::mutex> lock(s->m);
            s->lost = true;   // the worker reconnects (or gives up)
            SetEvent(s->wake);
            return;
        }
        msg.append(buf.data(), got);
        if (msg.size() > (1u << 20)) msg.clear();
        if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)
        {
            s->OnMessage(msg);
            msg.clear();
        }
    }
}

void Unlist(const Session& s)
{
    std::string out, why;
    Authed(s.url, s.key, s.device, L"PUT", "/v2/storage/delete",
           "{\"object_ids\":[{\"collection\":\"" + std::string(Collection) + "\",\"key\":" + Quote(s.code) + "}]}", out, why);
}
void List(const Session& s, int members)
{
    // the time goes in too: with no code on the server, nothing removes a listing left behind, so readers skip old ones
    std::string value = "{\"tag\":" + Quote(s.tag) + ",\"name\":" + Quote(s.name) + ",\"code\":" + Quote(s.code) +
                        ",\"members\":" + std::to_string(members) + ",\"time\":" + std::to_string((long long)time(nullptr)) + "}";
    std::string out, why;
    int st = Authed(s.url, s.key, s.device, L"PUT", "/v2/storage",
                    "{\"objects\":[{\"collection\":\"" + std::string(Collection) + "\",\"key\":" + Quote(s.code) + ",\"value\":" + Quote(value) +
                    ",\"permission_read\":2,\"permission_write\":1}]}", out, why);
    if (st != 200)
    {
        Log("online: couldn't list the game: %s", why.c_str());
        std::lock_guard<std::mutex> lock(const_cast<Session&>(s).m);
        const_cast<Session&>(s).events.push_back({ 4, "Couldn't list the game: " + why, 0 });   // shown in the co-op screen
    }
}

// the realtime connection: a WebSocket with the login token; nullptr and why on failure
HINTERNET OpenSocket(const Session& s, const std::string& token, std::string& why, int& status)
{
    status = 0;
    if (RetryIn() > 0) { status = 429; why = Describe(429); return nullptr; }
    Handle hs(OpenSession());
    Handle hc(hs ? WinHttpConnect(hs, Wide(s.url.host).c_str(), s.url.port, 0) : nullptr);
    Handle hr(hc ? WinHttpOpenRequest(hc, L"GET", Wide(s.url.prefix + "/ws?lang=en&status=false&format=json&token=" + token).c_str(), nullptr,
                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, s.url.https ? WINHTTP_FLAG_SECURE : 0) : nullptr);
    if (!hr) { why = Describe(NoAnswer); return nullptr; }
    WinHttpSetTimeouts(hr, 8000, 8000, 10000, 0);   // no receive timeout: the game can be quiet for a long time
    DWORD len = sizeof(DWORD), st = 0;
    if (!WinHttpSetOption(hr, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) || !WinHttpSendRequest(hr, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) ||
        !WinHttpReceiveResponse(hr, nullptr))
    {
        DWORD e = GetLastError();
        status = e == ERROR_WINHTTP_SECURE_FAILURE || e == ERROR_WINHTTP_SECURE_INVALID_CA ? TlsError : NoAnswer;
        why = Describe(status);
        return nullptr;
    }
    WinHttpQueryHeaders(hr, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &st, &len, WINHTTP_NO_HEADER_INDEX);
    status = (int)st;
    if (st != 101) { why = Describe((int)st); return nullptr; }
    HINTERNET ws = WinHttpWebSocketCompleteUpgrade(hr, 0);
    if (!ws) why = "couldn't open the game connection";
    return ws;
}

void Worker(std::shared_ptr<Session> s)
{
    std::string why;
    if (Login(s->url, s->key, s->device, why).empty()) { std::lock_guard<std::mutex> lock(s->m); s->Fail("Server: " + why); return; }
    DWORD lastList = 0, lostSince = 0;
    bool wasListed = false;
    int attempt = 0;
    for (;;)   // one connection each time round; a dropped one is opened again with growing waits
    {
        { std::lock_guard<std::mutex> lock(s->m); if (s->stop || s->failed) break; }
        int status = 0;
        std::string token = Login(s->url, s->key, s->device, why);
        HINTERNET ws = token.empty() ? nullptr : OpenSocket(*s, token, why, status);
        if (!ws && status == 401) { ForgetToken(); token = Login(s->url, s->key, s->device, why, true); ws = token.empty() ? nullptr : OpenSocket(*s, token, why, status); }
        if (!ws)
        {
            std::lock_guard<std::mutex> lock(s->m);
            bool hopeless = status == 401 || status == 404 || status == 426 || status == TlsError;
            if (!s->everReady || hopeless || (lostSince && GetTickCount() - lostSince > 120000)) { s->Fail("Server: " + why); break; }
        }
        else
        {
            {
                std::lock_guard<std::mutex> lock(s->m);
                s->lost = false; s->ready = false; s->outbox.clear();   // what was queued while away is stale: the game catches up by itself
                s->outbox.push_front("{\"cid\":\"1\",\"match_create\":{\"name\":" + Quote("insaniquarium-remastered-mod-" + s->code) + "}}");
            }
            attempt = 0; lostSince = 0;
            DWORD lastPing = GetTickCount();
            std::thread rx(Receiver, s, ws);
            bool stop = false, lost = false;
            for (;;)
            {
                WaitForSingleObject(s->wake, 1000);
                std::deque<std::string> out;
                bool list, change;
                int members;
                {
                    std::lock_guard<std::mutex> lock(s->m);
                    out.swap(s->outbox);
                    stop = s->stop || s->failed; lost = s->lost;
                    list = s->host && s->listed && s->ready && s->joinable;
                    change = s->listingChanged; s->listingChanged = false;
                    members = s->members;
                    // players who dropped out and didn't come back in time
                    for (auto it = s->leaving.begin(); it != s->leaving.end();)
                        if ((long)(GetTickCount() - it->second) >= 0) { s->events.push_back({ 3, "", it->first }); it = s->leaving.erase(it); } else ++it;
                }
                // a ping now and then (Nakama answers with a pong): traffic both ways, so neither the server nor a proxy
                // in between takes the connection for dead (some WebSocket clients, Wine's among them, don't answer the
                // server's protocol pings)
                if (!lost && GetTickCount() - lastPing > 10000) { out.push_back("{\"ping\":{}}"); lastPing = GetTickCount(); }
                if (!lost)
                    for (auto& f : out)
                        if (WinHttpWebSocketSend(ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, (void*)f.data(), (DWORD)f.size())) { lost = true; break; }
                // the listing: written while new players are welcome (and kept fresh), removed otherwise
                if (!stop && list && (change || GetTickCount() - lastList > 30000)) { List(*s, members); lastList = GetTickCount(); wasListed = true; }
                if (wasListed && (stop || !list)) { Unlist(*s); wasListed = false; }
                if (stop || lost) break;
            }
            if (stop)
            {
                std::string leave;
                { std::lock_guard<std::mutex> lock(s->m); if (!s->matchId.empty()) leave = "{\"match_leave\":{\"match_id\":" + Quote(s->matchId) + "}}"; }
                if (!leave.empty()) WinHttpWebSocketSend(ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, (void*)leave.data(), (DWORD)leave.size());
                WinHttpWebSocketShutdown(ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
                Sleep(300);
            }
            WinHttpCloseHandle(ws);   // ends the receiver's wait
            rx.join();
            if (stop) break;
            std::lock_guard<std::mutex> lock(s->m);
            s->lost = true; s->ready = false;
            if (!s->everReady) { s->Fail("Server: lost the connection"); break; }
            lostSince = GetTickCount();
            Log("online: lost the connection to the server: reconnecting");
        }
        // wait before trying again: 1, 2, 4, 8, 16, then 30 s (longer if the server asked for it)
        int wait = std::max(std::min(1 << std::min(attempt, 5), 30), RetryIn());
        attempt++;
        { std::lock_guard<std::mutex> lock(s->m); s->events.push_back({ 4, "Connection to the server lost: reconnecting in " + std::to_string(wait) + " s...", 0 }); }
        for (int t = 0; t < wait * 10; t++)
        {
            Sleep(100);
            std::lock_guard<std::mutex> lock(s->m);
            if (s->stop) break;
        }
    }
    if (wasListed) Unlist(*s);
}

void Begin(bool host, const std::string& code, bool listed, const std::string& name, const std::string& tag)
{
    Leave();
    auto s = std::make_shared<Session>();
    if (!ParseUrl(server, s->url)) { if (ev.failed) ev.failed("Server: the server address isn't valid (check the server settings)"); return; }
    s->key = key; s->device = device; s->code = code; s->host = host; s->listed = listed; s->name = name; s->tag = tag;
    cur = s;
    std::thread(Worker, s).detach();
}

}  // namespace

bool Configured() { return !server.empty() && !key.empty(); }
void Configure(const std::string& srv, const std::string& k, const std::string& dev, const Events& events)
{
    server = srv; key = k; device = dev; ev = events;
    while (!server.empty() && (server.back() == '/' || server.back() == ' ')) server.pop_back();
}
void SetLog(void (*log)(const char* line)) { logf = log; }

std::string CleanCode(const std::string& s)
{
    std::string r;
    for (char c : s) { char u = (char)toupper((unsigned char)c); if (strchr(Alphabet, u) && u) r += u; }
    return r;
}
bool IsCode(const std::string& s)
{
    if (s.size() != 5) return false;
    for (char c : s) { char u = (char)toupper((unsigned char)c); if (!u || !strchr(Alphabet, u)) return false; }
    return true;
}

void Host(bool listed, const std::string& name, const std::string& tag)
{
    std::string code;
    for (int i = 0; i < 5; i++) code += Alphabet[RandomBits() % (sizeof Alphabet - 1)];
    Begin(true, code, listed, name, tag);
}
void Join(const std::string& code) { Begin(false, CleanCode(code), false, "", ""); }

void Refresh(const std::string& tag)
{
    Url u;
    if (!ParseUrl(server, u)) { std::lock_guard<std::mutex> lock(listMutex); listError = "the server address isn't valid"; listReady = true; return; }
    {
        std::lock_guard<std::mutex> lock(listMutex);
        if (GetTickCount() - lastRefresh < 3000) { listError = "a moment, please: the list was just asked for"; listReady = true; return; }
        lastRefresh = GetTickCount();
    }
    std::string k = key, dev = device;
    Log("online: asking %s for its games", u.host.c_str());
    std::thread([u, k, dev, tag] {
        std::string why, out;
        std::vector<Lobby> ls;
        int st = Authed(u, k, dev, L"GET", std::string("/v2/storage/") + Collection + "?limit=100", "", out, why);
        Json j;
        if (st == 200 && Parse(out, j))
        {
            why.clear();
            long long now = (long long)time(nullptr);
            for (auto& o : j["objects"].arr)
            {
                Json v;
                if (!Parse(o["value"].str, v) || v["tag"].str != tag) continue;   // other mod sets, other games
                // hosts refresh their entry every 30 s; ones older than 2 minutes were left behind
                long long at = v["time"].integer();
                if (at <= 0)
                {
                    int Y, M, D, h, mi, sec;
                    if (sscanf(o["update_time"].str.c_str(), "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &mi, &sec) == 6)
                    {
                        struct tm t {}; t.tm_year = Y - 1900; t.tm_mon = M - 1; t.tm_mday = D; t.tm_hour = h; t.tm_min = mi; t.tm_sec = sec;
                        at = (long long)_mkgmtime(&t);
                    }
                }
                if (at > 0 && now - at > 120) continue;
                ls.push_back({ v["code"].str, v["name"].str, (int)v["members"].integer() });
            }
        }
        else if (why.empty()) why = Describe(st);
        Log("online: the list came back (%d): %d game(s)%s%s", st, (int)ls.size(), why.empty() ? "" : ", ", why.c_str());
        std::lock_guard<std::mutex> lock(listMutex);
        found = ls; listError = why; listReady = true;
    }).detach();
}
std::string ListError() { std::lock_guard<std::mutex> lock(listMutex); return listError; }

// the settings screen's test: is it a Nakama server (healthcheck), does the key work (a login; a server that needs a newer
// mod answers 426)
std::mutex testMutex;
std::string testResult;
bool testDone;
void Test()
{
    Url u;
    if (!ParseUrl(server, u)) { std::lock_guard<std::mutex> lock(testMutex); testResult = "The server address isn't valid"; testDone = true; return; }
    { std::lock_guard<std::mutex> lock(testMutex); testDone = false; testResult.clear(); }
    std::string k = key, dev = device;
    std::thread([u, k, dev] {
        std::string out, why, r;
        int st = Request(u, L"GET", "/healthcheck", "", "", out);
        std::string body = out;
        body.erase(std::remove_if(body.begin(), body.end(), [](char c) { return c == ' ' || c == '\r' || c == '\n'; }), body.end());
        if (st != 200) r = "Test failed: " + Describe(st);
        else if (body != "{}") r = "Test failed: that isn't a Nakama server (its healthcheck answered something else)";
        else if (Login(u, k, dev, why, true).empty()) r = "Test failed: " + why;
        else r = "Connection OK: " + u.host + " works";
        Log("online: test of %s: %s", u.host.c_str(), r.c_str());
        std::lock_guard<std::mutex> lock(testMutex);
        testResult = r; testDone = true;
    }).detach();
}
bool TestResult(std::string& result)
{
    std::lock_guard<std::mutex> lock(testMutex);
    if (!testDone) return false;
    testDone = false; result = testResult;
    return true;
}
std::string ServerName() { Url u; return ParseUrl(server, u) ? u.host + (u.port != (u.https ? 443 : 80) ? ":" + std::to_string(u.port) : "") : ""; }
const std::vector<Lobby>& Lobbies() { return lobbies; }

void Frame()
{
    bool listed = false;
    {
        std::lock_guard<std::mutex> lock(listMutex);
        if (listReady)
        {
            listReady = false;
            lobbies = found;
            if (!listError.empty()) Log("online: list: %s", listError.c_str());
            listed = true;
        }
    }
    if (listed && ev.listed) ev.listed();   // outside the lock: the callback reads ListError()
    auto s = cur;
    if (!s) return;
    std::deque<Event> evs;
    {
        std::lock_guard<std::mutex> lock(s->m);
        if (!s->host && s->ready && !s->hostId && GetTickCount() - s->readyAt > 8000) s->Fail("No game found with the code " + s->code);
        evs.swap(s->events);
    }
    for (auto& e : evs)
    {
        if (cur != s) break;   // a callback ended this game
        if (e.type == 0 && ev.created) ev.created(e.text);
        else if (e.type == 1 && ev.entered) ev.entered(e.id);
        else if (e.type == 2) { Leave(); if (ev.failed) ev.failed(e.text.c_str()); }
        else if (e.type == 3 && ev.left) ev.left(e.id);
        else if (e.type == 4 && ev.reconnecting) ev.reconnecting(e.text.c_str());
        else if (e.type == 5 && ev.reconnected) ev.reconnected();
    }
}

void Leave()
{
    auto s = cur;
    cur.reset();
    if (!s) return;
    std::lock_guard<std::mutex> lock(s->m);
    s->stop = true;   // the worker leaves the match, unlists the game and closes the connection by itself
    SetEvent(s->wake);
}

void SetJoinable(bool on, int members)
{
    auto s = cur;
    if (!s) return;
    std::lock_guard<std::mutex> lock(s->m);
    if (s->joinable != on || s->members != members) { s->joinable = on; s->members = members; s->listingChanged = true; SetEvent(s->wake); }
}

std::string Code() { auto s = cur; if (!s) return ""; std::lock_guard<std::mutex> lock(s->m); return s->ready ? s->code : ""; }

bool Send(uint64_t to, const std::string& data)
{
    auto s = cur;
    if (!s) return false;
    std::lock_guard<std::mutex> lock(s->m);
    if (!s->ready) return false;
    for (size_t off = 0;; off += Chunk)
    {
        bool last = off + Chunk >= data.size();
        s->SendOp(to, last ? OpData : OpPart, data.substr(off, Chunk));
        if (last) break;
    }
    return true;
}

bool Receive(uint64_t& from, std::string& data)
{
    auto s = cur;
    if (!s) return false;
    std::lock_guard<std::mutex> lock(s->m);
    if (s->inbox.empty()) return false;
    from = s->inbox.front().first; data = std::move(s->inbox.front().second);
    s->inbox.pop_front();
    return true;
}

}  // namespace online
