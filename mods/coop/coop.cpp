// coop: co-op for 2-4 players over the network: a direct connection (the host shares its address and port, the others
// join it), Steam (steam.cpp: lobbies and Valve's relay) or your own Nakama server (online.cpp: relayed matches). Lockstep: every player's game runs the same update ticks with the same inputs, so all tanks stay identical.
// Each player's mouse and keys are sent to the host instead of the game; the host puts everyone's input into one
// bundle per tick and sends it to all players; each game applies a tick's bundle (through the game's own input queue)
// and then runs the tick, and waits when the bundle hasn't arrived yet. Everyone plays the host's profile (copied into
// memory at the start; a guest's own profile is put back afterwards and never saved over). A state hash every 64
// ticks catches divergence. Lobby: "Co-op" on the main menu's Remastered page, or F7. [coop] port=27615, name, address, server, key.
#include <winsock2.h>
#include <ws2tcpip.h>
#include "remod.h"
#include "game.h"
#include "remodui.h"
#include "version.h"
#include "steam.h"
#include "online.h"
#include "wordfilter.h"
#include <windows.h>
#include <stdio.h>
#include <math.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <deque>
#include <algorithm>

using namespace game;
static const RemodApi* api;

// ---- the session ---------------------------------------------------------------------------------------------------
enum State { Off, Lobby, Playing };
static State state;
static bool host;
static int localSlot;                       // 0 = host
static const int MaxPlayers = 4, Delay = 3, MaxAhead = 20, HashEvery = 64;
static int port = 27615;
struct Ev { uint8_t slot, kind; int16_t x, y; uint16_t v; };
static const unsigned Colors[4] = { 0xffffe040, 0xff40e0ff, 0xffff70c0, 0xff90ff60 };
enum { EvMove = 1, EvDown, EvUp, EvDbl, EvRDown, EvRUp, EvMDown, EvMUp, EvKeyDown, EvKeyUp, EvChar, EvWheel, EvLeave, EvSpeed };   // EvSpeed: the host's game speed (x = ms per update, y = paused)   // EvLeave: the host's note that a player left (at a lockstep tick)
struct Peer { SOCKET s = INVALID_SOCKET; uint64_t sid = 0; std::string in, name; int ack = -1; bool used = false; };
enum Net { NetTcp, NetSteam, NetServer };
static Net net;                              // how the session runs: TCP, Steam (lobby + relayed messages), your own server
static Peer peers[MaxPlayers];               // host: 1..3 = guests; guest: [0] = the host
static SOCKET listener = INVALID_SOCKET;
static std::string names[MaxPlayers];
static std::map<int, std::vector<Ev>> bundles;   // tick -> events
static std::vector<Ev> pending;              // host: events waiting for the next bundle; guest: to send
static int tick, nextBundle;
static std::map<int, uint64_t> hostHashes;
static bool desyncLogged;                     // the first desync of a session logs what differed
static std::string status;
static int lastX[MaxPlayers], lastY[MaxPlayers];
static int lastInputTick;                     // the session tick of the last lockstep input (pets' naps)
static bool inLockstep;                       // inside a lockstep tick (the game's own pause calls are allowed)
static int actingSlot = -1;                   // the player whose input the game is handling right now (the same everywhere)
enum Role { RoleNone, Feeder, Gunner, Collector, Shopper, RoleCount };
static int roles[MaxPlayers];
static const char* RoleNames[RoleCount] = { "No role", "Feeder", "Gunner", "Collector", "Shopper" };
static const char* RoleHelp[RoleCount] = { "", "Your food costs half", "Your laser hits as if 2 levels stronger", "Coins you pick up pay 25% more", "The store is 15% cheaper for you" };
static bool scaling = true;                   // host: prices and aliens tougher with more players
static bool splitMoney;                       // host: every player has their own money (split wallets)
static int wallets[MaxPlayers], walletSlot, feedSlot, moneyOwner = -1;   // split wallets (see UseWallet)
static bool walletsReset;
static std::map<void*, int> coinOwner;   // coins on their way up: who clicked them (erased when they pay or go)
static void UseWallet(int s);
// resync: the host's snapshot (the board through the game's own save, the RNG, globals), loaded by every machine at
// the same tick (snapTick); after a desync, or every resyncTest ticks for testing
static std::string snapData;
static int snapTick = -1, snapParts, snapGot, resyncTest, lastResync = -100000, resyncFrom;
static bool resyncWanted;
static std::string TakeSnapshot();
static void NoteLeave(int slot);
static std::string StartMessage();
static bool LoadSnapshot(const std::string& s);
static void ResetWallets();
static bool Split();
static int WalletOf(int s);
// versus: the first guest steers the aliens (their input never reaches the tank); decided from lockstep input, so every
// machine steers the same way
static bool versus;
static const int VersusSlot = 1;
static int vsDash, vsCooldown;
static bool vsFire, vsOver;   // vsOver: this game's winner was announced
// who is in the game, as the simulation sees it: changes only at lockstep ticks (the start, a snapshot that brings a
// joiner in, a player's EvLeave), never when a message happens to arrive, so every machine agrees at every tick
static bool inGame[MaxPlayers], joiningSlot[MaxPlayers], joining;
static bool Versus() { return state == Playing && versus && inGame[VersusSlot]; }
// avatars: one of the game's own animated portraits on each player's pointer and in the lobby (display only)
static int avatars[MaxPlayers];
static const char* AvatarNames[] = { "None", "Guppy", "Stinky", "Niko", "Itchy", "Prego", "Zorf", "Clyde", "Vert", "Rufus",
    "Meryl", "Wadsworth", "Seymour", "Shrapnel", "Gumbo", "Blip", "Rhubarb", "Nimbus", "Amp", "Gash", "Angie", "Presto",
    "Brinkley", "Nostradamus", "Stanley", "Walter", "Carnivore", "Grubber", "Starcatcher", "Gekko", "Breeder", "Ultravore" };
static const int AvatarCount = sizeof AvatarNames / sizeof AvatarNames[0];
// the images (Image** globals): IMAGE_SMALLSWIM (row 0 = the guppy), then IMAGE_SCL_* (the pet chooser's portraits)
static const uintptr_t AvatarImages[AvatarCount] = { 0, 0x5e8ab8,
    0x5e8a84, 0x5e8ed0, 0x5e8cb0, 0x5e8ee0, 0x5e8d20, 0x5e8b2c, 0x5e8ac8, 0x5e8eb0, 0x5e8bf0, 0x5e8a0c, 0x5e8db4, 0x5e8dcc,
    0x5e8b94, 0x5e8ccc, 0x5e8cdc, 0x5e8d24, 0x5e8dfc, 0x5e8b7c, 0x5e8d30, 0x5e8c88, 0x5e8ae0, 0x5e8afc, 0x5e8abc, 0x5e8a54,
    0x5e8b80, 0x5e8bac, 0x5e8ac0, 0x5e8ce0, 0x5e8c90, 0x5e8dc8 };
static int ClampAvatar(int a) { return a >= 0 && a < AvatarCount ? a : 0; }
static int startShells = -1;                  // a guest: the shells of the host's profile at the start (what was earned goes home)
struct ChatLine { std::string text; unsigned color; DWORD at; };
static std::deque<ChatLine> chat;
static bool chatting;
static std::string chatBuf;
struct Mark { int slot, x, y; DWORD at; };
static std::vector<Mark> marks;
// the start: seed, mode, tank, mutators, the host's profile
struct Start { uint32_t seed; uint8_t mode, tank; int32_t mutators; std::string profile; };
static Start start;
static bool startPending;
static std::string savedProfile;             // a guest's own profile, put back after the session
static const int ProfileBytes = 0x130;

static void Log(const char* fmt, ...)
{
    char b[512]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
    api->log("coop: %s", b);
}

static void Achievement(const char* id)
{
    HMODULE m = GetModuleHandleA("achievements.dll");
    if (auto f = m ? reinterpret_cast<void (*)(const char*)>(GetProcAddress(m, "AchievementUnlock")) : nullptr) f(id);
}

// ---- the network --------------------------------------------------------------------------------------------------
static void SetNonBlocking(SOCKET s) { u_long one = 1; ioctlsocket(s, FIONBIO, &one); int nd = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nd, sizeof nd); }
static void Send(Peer& p, uint8_t type, const std::string& payload)
{
    std::string m;
    uint16_t len = (uint16_t)(payload.size() + 1);
    m.append((const char*)&len, 2);
    m += (char)type;
    m += payload;
    if (net == NetSteam) { if (p.sid) steam::Send(p.sid, m); return; }
    if (net == NetServer) { if (p.sid) online::Send(p.sid, m); return; }
    if (p.s == INVALID_SOCKET) return;
    size_t off = 0;
    for (int guard = 0; off < m.size() && guard < 1000; guard++)   // small messages: a short blocking loop is fine
    {
        int n = send(p.s, m.data() + off, (int)(m.size() - off), 0);
        if (n > 0) off += n;
        else if (WSAGetLastError() == WSAEWOULDBLOCK) Sleep(1);
        else { closesocket(p.s); p.s = INVALID_SOCKET; return; }
    }
}
static void Broadcast(uint8_t type, const std::string& payload) { for (int i = 1; i < MaxPlayers; i++) if (peers[i].used) Send(peers[i], type, payload); }
template <typename T> static void Put(std::string& s, T v) { s.append((const char*)&v, sizeof v); }
template <typename T> static T Get(const std::string& s, size_t& o) { T v{}; if (o + sizeof v <= s.size()) memcpy(&v, s.data() + o, sizeof v); o += sizeof v; return v; }

enum { MHello = 1, MWelcome, MLobby, MStart, MInput, MBundle, MAck, MHash, MDesync, MEnd, MBye, MRefuse, MRole, MChat, MMark, MAvatar, MSnap, MJoin, MRejoin };

static std::string Fingerprint()
{
    // the same mods (and versions) on every machine: otherwise the games can't stay identical
    std::string f = REMOD_VERSION;
    for (int i = 0;; i++)
    {
        const char *n, *d;
        int st = api->mod_list(i, &n, &d);
        if (st < 0) break;
        if (st == REMOD_MOD_ON && strcmp(n, "testkit") && strcmp(n, "crashtest") && strcmp(n, "hello")) { f += "|"; f += n; }
    }
    if (HMODULE m = GetModuleHandleA("content.dll"))
        if (auto fp = reinterpret_cast<const char* (*)()>(GetProcAddress(m, "ContentFingerprint"))) { f += "|content:"; f += fp(); }
    return f;
}

static std::string MyName()
{
    char n[64];
    api->config_string("coop", "name", "", n, sizeof n);
    if (n[0]) return n;
    void* a = api->app();
    void* p = a ? at<void*>(a, App_mProfile) : nullptr;
    if (!p) return "Player";
    char* s = static_cast<char*>(p) + 0x24;
    return at<uint32_t>(s, 0x18) >= 16 ? at<const char*>(s, 4) : s + 4;
}

static int MyAvatar() { return ClampAvatar(api->config_int("coop", "avatar", 1)); }

static void SendLobby()
{
    if (host) avatars[0] = MyAvatar();
    std::string s;
    for (int i = 0; i < MaxPlayers; i++) { s += (i == 0 || peers[i].used) ? names[i] : ""; s += '\n'; }
    for (int i = 0; i < MaxPlayers; i++) s += (char)('0' + roles[i]);
    s += versus ? '1' : '0';
    for (int i = 0; i < MaxPlayers; i++) s += (char)('0' + avatars[i]);
    s += splitMoney ? '1' : '0';
    Broadcast(MLobby, s);
    if (net == NetServer && host && state != Off)   // listed while there's room, in the lobby or in a game (joiners come in through a snapshot)
    {
        int n = 1;
        for (int i = 1; i < MaxPlayers; i++) n += peers[i].used ? 1 : 0;
        online::SetJoinable(n < MaxPlayers, n);
    }
}

static void Close()
{
    if (net == NetSteam) steam::Leave();
    if (net == NetServer) online::Leave();
    net = NetTcp;
    for (auto& p : peers) { if (p.s != INVALID_SOCKET) closesocket(p.s); p = Peer(); }
    if (listener != INVALID_SOCKET) closesocket(listener);
    listener = INVALID_SOCKET;
}

static void RestoreProfile();
static void EndSession(const char* why)
{
    bool wasPlaying = state == Playing;
    if (host) { Broadcast(MBye, ""); }
    else if (peers[0].used) Send(peers[0], MBye, "");
    Close();
    state = Off;
    bundles.clear(); pending.clear();
    RestoreProfile();
    status = why;
    if (why && *why) { api->toast(why); Log("%s", why); }
    void* a = api->app();
    if (wasPlaying && a && !host && api->board())   // the guest's tank was the host's: back to the menu
    {
        reinterpret_cast<void(__thiscall*)(void*)>(App_RemoveBoard)(a);
        reinterpret_cast<void(__thiscall*)(void*)>(App_ShowGameSelector)(a);
    }
    api->set_input_filter(nullptr);
}

static int InputFilter(unsigned msg, unsigned wp, long lp);

// ---- Steam ------------------------------------------------------------------------------------------------------------
static std::string MatchTag()
{
    uint32_t h = 2166136261u;
    for (char c : Fingerprint()) { h ^= (unsigned char)c; h *= 16777619u; }
    char t[64];
    snprintf(t, sizeof t, "insaniquarium-remastered-mod-%08x", h);   // only games with the same mods are listed
    return t;
}
static std::string steamCode;
static void SteamCreated(uint64_t lobby) { steamCode = steam::Code(lobby); status = "Steam game ready. Code: " + steamCode; Log("steam lobby %s", steamCode.c_str()); }
static void SteamEntered(uint64_t owner)
{
    peers[0] = Peer(); peers[0].used = true; peers[0].sid = owner;
    Send(peers[0], MHello, Fingerprint() + "\n" + MyName());
    status = "Joined on Steam: waiting for the host";
}
static void SteamFailed(const char* why) { if (!host) EndSession(why); else { status = why; EndSession(why); } }
static void SteamLeft(uint64_t user)
{
    for (int i = 0; i < MaxPlayers; i++)
        if (peers[i].used && peers[i].sid == user)
        {
            if (!host) { EndSession("The host left the game"); return; }
            api->toast((names[i] + " left the game").c_str());
            NoteLeave(i);
            peers[i] = Peer(); names[i].clear(); roles[i] = 0;
            SendLobby();
        }
}
static void SteamListed() { if (status.rfind("Looking for", 0) == 0) status.clear(); api->redraw(); }   // the list shows the result
static const steam::Events SteamEvents = { SteamCreated, SteamEntered, SteamFailed, SteamLeft, SteamListed };
static void SteamLog(const char* line) { api->log("coop: %s", line); }
static bool SteamUp()
{
    steam::SetLog(SteamLog);
    char saved[MAX_PATH] = "";
    api->config_string("coop", "steam_api", "", saved, sizeof saved);
    std::string err, hint = saved;
    bool ok = steam::Start(SteamEvents, err, hint);
    // remember a steam_api.dll found in another game's folder, so the search runs only once
    std::string from = steam::LoadedFrom();
    if (!from.empty() && from != hint && from.find("\\mods\\coop\\steam_api.dll") == std::string::npos) api->config_set("coop", "steam_api", from.c_str());
    if (!ok) status = "Steam: " + err;
    return ok;
}

static bool HostSteam(bool listed)
{
    if (!SteamUp()) return false;
    net = NetSteam; host = true; localSlot = 0; state = Lobby;
    names[0] = MyName();
    for (int i = 1; i < MaxPlayers; i++) names[i].clear();
    status = "Making the Steam game...";
    steam::Host(listed, names[0] + "'s tank", MatchTag());
    return true;
}
static bool JoinSteam(uint64_t lobby)
{
    if (!SteamUp()) return false;
    net = NetSteam; host = false; state = Lobby;
    status = "Joining on Steam...";
    steam::Join(lobby);
    return true;
}

// ---- your own server (Nakama) -----------------------------------------------------------------------------------------
static void ServerCreated(const std::string& code) { status = "Game ready. Room code: " + code; Log("server room %s", code.c_str()); api->redraw(); }
static void ServerEntered(uint64_t hostId)
{
    peers[0] = Peer(); peers[0].used = true; peers[0].sid = hostId;
    Send(peers[0], MHello, Fingerprint() + "\n" + MyName());
    status = "Joined: waiting for the host";
}
static void ServerListed() { std::string e = online::ListError(); status = e.empty() ? "" : "Couldn't list games: " + e; api->redraw(); }
static void ServerReconnecting(const char* why) { status = why; Log("%s", why); api->redraw(); }
static void ServerReconnected()
{
    status = state == Playing ? "" : "Reconnected";
    api->toast("Reconnected to the server");
    Log("reconnected to the server");
    if (state == Playing)
    {   // messages sent while away are lost: everyone catches up from a snapshot of the host's game
        if (host) { resyncWanted = true; lastResync = -100000; }
        else Send(peers[0], MRejoin, "");
    }
    else if (state == Lobby)
    {
        if (host) SendLobby();
        else Send(peers[0], MHello, Fingerprint() + "\n" + MyName());
    }
}
static const online::Events ServerEvents = { ServerCreated, ServerEntered, SteamFailed, SteamLeft, ServerListed, ServerReconnecting, ServerReconnected };
// the server for online games: the mod's public server unless the player set their own ([coop] server_host, server_port,
// server_tls, key; an older single server= URL is taken over once)
static const char* const PublicHost = "insanicoop.mychud.net";
static const char* const PublicKey = "00898878a8ecbd459d1a49e8c940148a4c483752ac5f6e01";   // public by design (every game has it)
static const int PublicPort = 443;
struct ServerConf { std::string host, port, key; bool tls; };
static ServerConf LoadServer()
{
    char h[256], p[16], k[160], t[8], old[256];
    api->config_string("coop", "server_host", "", h, sizeof h);
    api->config_string("coop", "server", "", old, sizeof old);
    api->config_string("coop", "key", "", k, sizeof k);
    if (!h[0] && old[0])   // the old setting: https://host:port
    {
        std::string u = old; bool tls = u.rfind("https://", 0) == 0;
        if (u.rfind("http", 0) == 0) u = u.substr(u.find("//") + 2);
        while (!u.empty() && u.back() == '/') u.pop_back();
        size_t c = u.rfind(':');
        std::string host = c == std::string::npos ? u : u.substr(0, c), port = c == std::string::npos ? (tls ? "443" : "80") : u.substr(c + 1);
        api->config_set("coop", "server_host", host.c_str());
        api->config_set("coop", "server_port", port.c_str());
        api->config_set("coop", "server_tls", tls ? "1" : "0");
        api->config_set("coop", "server", "");
        return { host, port, k, tls };
    }
    if (!h[0]) return { PublicHost, std::to_string(PublicPort), PublicKey, true };
    api->config_string("coop", "server_port", "443", p, sizeof p);
    api->config_string("coop", "server_tls", "1", t, sizeof t);
    return { h, p, k, t[0] != '0' };
}
static void SaveServer(const ServerConf& c)
{
    bool pub = c.host == PublicHost && c.port == std::to_string(PublicPort) && c.tls && c.key == PublicKey;
    api->config_set("coop", "server_host", pub ? "" : c.host.c_str());   // empty: the public server (follows the mod's default)
    api->config_set("coop", "server_port", c.port.c_str());
    api->config_set("coop", "server_tls", c.tls ? "1" : "0");
    api->config_set("coop", "key", pub ? "" : c.key.c_str());
}
static std::string ServerUrl(const ServerConf& c) { return (c.tls ? "https://" : "http://") + c.host + ":" + c.port; }
static bool ServerUp()
{
    char dev[64];
    api->config_string("coop", "device", "", dev, sizeof dev);
    if (!dev[0])   // this computer's anonymous id on the server
    {
        LARGE_INTEGER q; QueryPerformanceCounter(&q);
        uint64_t r = (uint64_t)q.QuadPart * 6364136223846793005ULL ^ ((uint64_t)GetCurrentProcessId() << 20) ^ GetTickCount();
        snprintf(dev, sizeof dev, "remod-%016llx%08lx", (unsigned long long)r, (unsigned long)(GetTickCount() * 2654435761u));
        api->config_set("coop", "device", dev);
    }
    ServerConf c = LoadServer();
    online::SetLog(SteamLog);
    online::Configure(ServerUrl(c), c.key, dev, ServerEvents);
    if (c.host.empty() || c.key.empty()) { status = "Set the server's address and key (Server settings)"; return false; }
    return true;
}
static bool HostServer(bool listed)
{
    if (!ServerUp()) return false;
    net = NetServer; host = true; localSlot = 0; state = Lobby;
    names[0] = MyName();
    for (int i = 1; i < MaxPlayers; i++) names[i].clear();
    status = "Connecting to your server...";
    online::Host(listed, names[0] + "'s tank", MatchTag());
    return true;
}
static bool JoinServer(const std::string& code)
{
    if (!ServerUp()) return false;
    net = NetServer; host = false; state = Lobby;
    status = "Joining " + online::CleanCode(code) + "...";
    online::Join(code);
    return true;
}

static bool Host()
{
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons((u_short)port); a.sin_addr.s_addr = INADDR_ANY;
    int yes = 1; setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
    if (bind(listener, (sockaddr*)&a, sizeof a) || listen(listener, 4)) { status = "Couldn't open port " + std::to_string(port); Close(); return false; }
    SetNonBlocking(listener);
    host = true; localSlot = 0; state = Lobby;
    names[0] = MyName();
    for (int i = 1; i < MaxPlayers; i++) names[i].clear();
    status = "Hosting on port " + std::to_string(port) + ": share your address";
    Log("hosting on port %d", port);
    return true;
}

static bool Join(const std::string& address)
{
    if (uint64_t lobby = steam::FromCode(address)) { api->config_set("coop", "address", address.c_str()); return JoinSteam(lobby); }
    if (online::IsCode(address) && address.find('.') == std::string::npos) { api->config_set("coop", "address", address.c_str()); return JoinServer(address); }
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
    std::string h = address;
    int p = port;
    size_t c = h.rfind(':');
    if (c != std::string::npos) { p = atoi(h.c_str() + c + 1); h = h.substr(0, c); }
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(h.c_str(), std::to_string(p).c_str(), &hints, &res) || !res) { status = "Unknown address: " + address; return false; }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    // a short blocking connect (the game freezes at most ~3 s)
    DWORD to = 3000; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof to);
    if (connect(s, res->ai_addr, (int)res->ai_addrlen)) { freeaddrinfo(res); closesocket(s); status = "No game at " + address; return false; }
    freeaddrinfo(res);
    SetNonBlocking(s);
    peers[0].s = s; peers[0].used = true;
    host = false; state = Lobby;
    Send(peers[0], MHello, Fingerprint() + "\n" + MyName());
    status = "Connected: waiting for the host";
    api->config_set("coop", "address", address.c_str());
    Log("joined %s", address.c_str());
    return true;
}

// ---- profile copying ---------------------------------------------------------------------------------------------
static void* Profile() { void* a = api->app(); return a ? at<void*>(a, App_mProfile) : nullptr; }
// the profile's plain data, without its name (std::string at +0x24) and user index (+0x40, +0x44)
static std::string ProfileData()
{
    void* p = Profile();
    if (!p) return std::string(ProfileBytes, '\0');
    return std::string(static_cast<char*>(p), ProfileBytes);
}
static void SetProfileData(const std::string& d)
{
    void* p = Profile();
    if (!p || d.size() < (size_t)ProfileBytes) return;
    char* b = static_cast<char*>(p);
    memcpy(b, d.data(), 0x24);
    memcpy(b + 0x48, d.data() + 0x48, ProfileBytes - 0x48);
}
static void ResetScaling()
{
    if (HMODULE m = GetModuleHandleA("mutators.dll"))
        if (auto f = reinterpret_cast<void (*)(double, double)>(GetProcAddress(m, "MutatorsSetCoopScale"))) f(1, 1);
}
static bool(__thiscall* oSaveProfile)(void*);
static void RestoreProfile()
{
    ResetScaling();
    if (savedProfile.empty()) return;
    void* p = Profile();
    int earned = p && startShells >= 0 ? at<int>(p, Profile_mShells) - startShells : 0;
    SetProfileData(savedProfile);
    savedProfile.clear();
    startShells = -1;
    if (p && earned > 0)   // shells earned in the co-op game go to the guest's own profile
    {
        at<int>(p, Profile_mShells) = std::min(at<int>(p, Profile_mShells) + earned, 9999999);
        if (oSaveProfile) oSaveProfile(api->app());
        api->toast(("+" + std::to_string(earned) + " shells from the co-op game").c_str());
    }
    Log("own profile back");
}

// ---- hashing (for divergence) ---------------------------------------------------------------------------------------
// what went into each recent hash, logged on both sides when they differ (to see which part diverged)
static std::map<int, std::string> hashParts;
static std::string HashParts()
{
    char b[400];
    void* a = api->app();
    uint32_t* mt = at<uint32_t*>(a, App_mMTRand);
    int n = snprintf(b, sizeof b, "rng %u", mt ? mt[624] : 0);
    if (void* bd = api->board())
    {
        n += snprintf(b + n, sizeof b - n, " money %d tick %d alien %d", at<int>(bd, Board_mMoney), at<int>(bd, Board_mTick), at<int>(bd, Board_mAlienTimer));
        const int lists[] = { Board_mGuppies, Board_mOscars, Board_mCoins, Board_mFood, Board_mAliens, Board_mFishPets, 0xb0 };
        const char* names2[] = { "gup", "osc", "coin", "food", "alien", "pet", "b0" };
        for (int k = 0; k < 7 && n < (int)sizeof b - 40; k++)
        {
            long sx = 0, sy = 0;
            for (int i = 0; i < Count(bd, lists[k]); i++) { void* o = Item(bd, lists[k], i); sx += at<int>(o, Widget_mX); sy += at<int>(o, Widget_mY); }
            n += snprintf(b + n, sizeof b - n, " %s %d@%ld,%ld", names2[k], Count(bd, lists[k]), sx, sy);
        }
    }
    else snprintf(b + n, sizeof b - n, " no board");
    return b;
}
// who called the game's RNG in each hash interval (caller address: count), logged with the parts: the side with more
// calls names the code that ran on one machine only. A call outside a lockstep tick (drawing, a mod's timer) is always
// a bug and is logged once per caller
static std::map<uintptr_t, int> rngCalls;
static std::map<int, std::string> rngParts;
static unsigned(__thiscall* oMTNext)(void*);
static unsigned __fastcall MTNext(void* mt, void*)
{
    if (state == Playing && mt == at<void*>(api->app(), App_mMTRand))
    {
        uintptr_t from = (uintptr_t)__builtin_return_address(0);
        if (inLockstep) rngCalls[from]++;
        else { static std::set<uintptr_t> seen; if (seen.insert(from).second) Log("game RNG used outside a lockstep tick, from %08x", (unsigned)from); }
    }
    return oMTNext(mt);
}
// each applied press in the interval: tick, slot, position and the widget the game gave it to (its vtable: the class)
// and the open dialogs; a press handled differently on one machine shows here
static std::string inputLog;
static void NotePress(const Ev& e)
{
    void* a = api->app();
    char* wm = a ? at<char*>(a, App_mWidgetManager) : nullptr;
    char* w = wm ? at<char*>(wm, WM_mLastDownWidget) : nullptr;
    char b[96];
    snprintf(b, sizeof b, " %d:s%d:k%d:%d,%d:%x:d%d", tick, e.slot, e.kind, e.x, e.y, w ? *reinterpret_cast<unsigned*>(w) : 0, a ? at<int>(a, App_mDialogCount) : -1);
    if (inputLog.size() < 1500) inputLog += b;
}
static std::string RngParts()
{
    std::string r;
    char b[32];
    for (auto& c : rngCalls) { snprintf(b, sizeof b, " %x:%d", (unsigned)c.first, c.second); r += b; }
    rngCalls.clear();
    if (r.empty()) r = " none";
    r += "; presses:" + (inputLog.empty() ? std::string(" none") : inputLog);
    inputLog.clear();
    return r;
}
static void LogHashParts(int t)
{
    auto it = hashParts.find(t);
    Log("tick %d parts: %s", t, it == hashParts.end() ? "(gone)" : it->second.c_str());
    auto r = rngParts.find(t);
    api->log("coop: tick %d rng calls since the last hash:%s", t, r == rngParts.end() ? " (gone)" : r->second.c_str());   // long: not through Log's buffer
}

static uint64_t Hash()
{
    uint64_t h = 0xcbf29ce484222325ULL;
    auto mix = [&](uint32_t v) { for (int i = 0; i < 4; i++) { h ^= (v >> (i * 8)) & 0xff; h *= 0x100000001b3ULL; } };
    void* a = api->app();
    uint32_t* mt = at<uint32_t*>(a, App_mMTRand);
    if (mt) { mix(mt[624]); mix(mt[0]); mix(mt[623]); }
    if (void* b = api->board())
    {
        mix(at<int>(b, Board_mMoney)); mix(at<int>(b, Board_mTick)); mix(at<int>(b, Board_mAlienTimer));
        if (Split()) for (int i = 0; i < MaxPlayers; i++) mix(WalletOf(i));
        const int lists[] = { Board_mGuppies, Board_mOscars, Board_mCoins, Board_mFood, Board_mAliens, Board_mFishPets, 0xb0 };
        for (int l : lists)
        {
            mix(Count(b, l));
            for (int i = 0; i < Count(b, l); i++) { void* o = Item(b, l, i); mix(at<int>(o, Widget_mX)); mix(at<int>(o, Widget_mY)); }
        }
    }
    return h;
}

// ---- the host decides: what a guest may do in the host's game ---------------------------------------------------------
// The host builds every input bundle, so its rules are the same on every machine. Guests play with the mouse: their keys
// (game shortcuts, cheat codes) don't count, nor clicks on the tank's Menu button or anything while a dialog is open
// (the menu, game over, continues): only the host pauses, quits or answers those. Moves always pass (pointers, versus).
static bool GuestMayDo(const Ev& e)
{
    if (e.kind == EvMove || e.kind == EvWheel) return true;
    if (e.kind == EvKeyDown || e.kind == EvKeyUp || e.kind == EvChar || e.kind == EvLeave || e.kind == EvSpeed) return false;
    void* a = api->app();
    if (a && at<int>(a, App_mDialogCount) > 0) return e.kind == EvUp || e.kind == EvRUp || e.kind == EvMUp;   // releases keep buttons from sticking
    if (api->board() && e.x >= 520 && e.y < 40 && (e.kind == EvDown || e.kind == EvDbl)) return false;            // the tank's Menu button
    return true;
}
// the host's time control (timecontrol.dll asks): sent to everyone as an input event, applied at the same tick
static int coopFrameTime = 28;
static bool coopPaused;
extern "C" __declspec(dllexport) int CoopIsHost() { return state != Off && host ? 1 : 0; }
extern "C" __declspec(dllexport) void CoopRequestSpeed(int frameTime, int paused)
{
    if (state != Playing || !host) return;
    pending.push_back({ 0, EvSpeed, (int16_t)std::min(std::max(frameTime, 10), 100), (int16_t)(paused ? 1 : 0), 0 });
}
extern "C" __declspec(dllexport) void CoopSpeed(int* frameTime, int* paused) { if (frameTime) *frameTime = coopFrameTime; if (paused) *paused = coopPaused; }
static void ApplySpeed(int frameTime, bool paused);

// ---- receiving --------------------------------------------------------------------------------------------------------
// host: a player left during the game: out of the simulation at the next bundle's tick, the same on every machine
static void NoteLeave(int slot)
{
    joiningSlot[slot] = false;
    if (state == Playing && host && slot > 0 && slot < MaxPlayers) pending.push_back({ (uint8_t)slot, EvLeave, 0, 0, 0 });
}
static void Handle(int from, uint8_t type, const std::string& m);

static void ProcessInput(int i);
static void PollPeer(int i)
{
    Peer& p = peers[i];
    if (p.s == INVALID_SOCKET) return;
    char buf[4096];
    for (;;)
    {
        int n = recv(p.s, buf, sizeof buf, 0);
        if (n > 0) { p.in.append(buf, n); continue; }
        if (n == 0 || WSAGetLastError() != WSAEWOULDBLOCK)
        {
            closesocket(p.s); p.s = INVALID_SOCKET;
            if (host) { p.used = false; Log("%s left", names[i].c_str()); api->toast((names[i] + " left the game").c_str()); NoteLeave(i); names[i].clear(); SendLobby(); }
            else { EndSession("The host left the game"); }
            return;
        }
        break;
    }
    ProcessInput(i);
}

static void ProcessInput(int i)
{
    Peer& p = peers[i];
    while (p.in.size() >= 3)
    {
        uint16_t len; memcpy(&len, p.in.data(), 2);
        if (p.in.size() < (size_t)len + 2) break;
        uint8_t type = (uint8_t)p.in[2];
        std::string m = p.in.substr(3, len - 1);
        p.in.erase(0, len + 2);
        Handle(i, type, m);
        if (state == Off) return;
    }
}

static void Poll()
{
    if (state == Off)   // not in a game yet: the "Find games" lists still arrive through these
    {
        if (steam::Running()) steam::Frame();
        online::Frame();
        return;
    }
    if (net != NetTcp)
    {
        if (net == NetSteam) steam::Frame(); else online::Frame();
        if (state == Off || net == NetTcp) return;
        uint64_t from;
        std::string data;
        while (net == NetSteam ? steam::Receive(from, data) : online::Receive(from, data))
        {
            int slot = -1;
            for (int i = 0; i < MaxPlayers; i++) if (peers[i].used && peers[i].sid == from) slot = i;
            if (slot < 0 && host && (state == Lobby || state == Playing))
                for (int i = 1; i < MaxPlayers; i++) if (!peers[i].used) { slot = i; peers[i] = Peer(); peers[i].used = true; peers[i].sid = from; break; }
            if (slot < 0) continue;
            peers[slot].in += data;
            ProcessInput(slot);
            if (state == Off) return;
        }
        return;
    }
    if (host && listener != INVALID_SOCKET)
    {
        SOCKET s = accept(listener, nullptr, nullptr);
        if (s != INVALID_SOCKET)
        {
            int slot = -1;
            for (int i = 1; i < MaxPlayers; i++) if (!peers[i].used) { slot = i; break; }
            if (slot < 0) { Peer t; t.s = s; Send(t, MRefuse, "The game is full"); closesocket(s); }
            else { SetNonBlocking(s); peers[slot].s = s; peers[slot].used = true; peers[slot].in.clear(); }
        }
    }
    for (int i = 0; i < MaxPlayers; i++) if (peers[i].used) PollPeer(i);
}

static void Handle(int from, uint8_t type, const std::string& m)
{
    size_t o = 0;
    if (host)
    {
        switch (type)
        {
            case MHello:
            {
                size_t nl = m.find('\n');
                std::string fp = m.substr(0, nl), name = nl == std::string::npos ? "Player" : wordfilter::Clean(m.substr(nl + 1, 24));   // the server doesn't check names inside game messages
                if (fp != Fingerprint())
                {
                    Send(peers[from], MRefuse, "Different mods or versions: the host has " + Fingerprint());
                    Log("refused %s: fingerprint %s", name.c_str(), fp.c_str());
                    closesocket(peers[from].s); peers[from] = Peer();
                    return;
                }
                names[from] = name;
                std::string w; Put<uint8_t>(w, (uint8_t)from);
                Send(peers[from], MWelcome, w);
                SendLobby();
                api->toast((name + " joined").c_str());
                Log("%s joined (slot %d)", name.c_str(), from);
                if (state == Playing)
                {   // a game in progress: the joiner gets how it was started, then everyone loads a snapshot that includes them
                    std::string j = StartMessage();
                    Send(peers[from], MJoin, j);
                    peers[from].ack = tick;
                    joiningSlot[from] = true;
                    resyncWanted = true; lastResync = -100000;
                    Log("%s joins the game in progress", name.c_str());
                }
                break;
            }
            case MInput:
                while (o + sizeof(Ev) <= m.size()) { Ev e = Get<Ev>(m, o); e.slot = (uint8_t)from; if (GuestMayDo(e)) pending.push_back(e); }
                break;
            case MAck: peers[from].ack = Get<int32_t>(m, o); break;
            case MRejoin: if (state == Playing) { peers[from].ack = tick; resyncWanted = true; lastResync = -100000; Log("%s reconnected: resync", names[from].c_str()); } break;
            case MHash:
            {
                int t = Get<int32_t>(m, o);
                uint64_t h = Get<uint64_t>(m, o);
                auto it = hostHashes.find(t);
                if (it != hostHashes.end() && it->second != h && t >= resyncFrom)
                {
                    if (tick - lastResync > 256) resyncWanted = true;   // everyone reloads the host's state
                    std::string d; Put<int32_t>(d, t);
                    Broadcast(MDesync, d);
                    Log("OUT OF SYNC with %s at tick %d", names[from].c_str(), t);
                    if (!desyncLogged) { desyncLogged = true; LogHashParts(t - HashEvery); LogHashParts(t); }
                    api->toast(("Out of sync with " + names[from] + "!").c_str());
                }
                break;
            }
            case MBye: NoteLeave(from); closesocket(peers[from].s); peers[from] = Peer(); names[from].clear(); roles[from] = 0; avatars[from] = 0; SendLobby(); break;
            case MAvatar: if (!m.empty()) { avatars[from] = ClampAvatar((uint8_t)m[0]); SendLobby(); } break;
            case MRole: if (state == Lobby && !m.empty()) { roles[from] = std::min(std::max((int)(uint8_t)m[0], 0), RoleCount - 1); SendLobby(); } break;
            case MChat: if (from < 0) break; { std::string c; Put<uint8_t>(c, (uint8_t)from); c += wordfilter::Clean(m.substr(0, 120)); Broadcast(MChat, c); Handle(-1, MChat, c); break; }
            case MMark: if (from < 0) break; { std::string c; Put<uint8_t>(c, (uint8_t)from); c += m.substr(0, 4); Broadcast(MMark, c); Handle(-1, MMark, c); break; }
        }
        if (from >= 0) return;
    }
    if (type == MChat || type == MMark)
    {
        int slot = m.empty() ? 0 : (uint8_t)m[0];
        if (slot >= MaxPlayers) return;
        if (type == MChat) { chat.push_back({ names[slot] + ": " + wordfilter::Clean(m.substr(1)), Colors[slot], GetTickCount() }); if (chat.size() > 6) chat.pop_front(); api->redraw(); }
        else { size_t o2 = 1; int16_t x = Get<int16_t>(m, o2), y = Get<int16_t>(m, o2); marks.push_back({ slot, x, y, GetTickCount() }); }
        return;
    }
    if (host) return;
    switch (type)
    {
        case MWelcome:
        {
            localSlot = Get<uint8_t>(m, o); status = "In the lobby: waiting for the host to start";
            std::string a; a += (char)MyAvatar(); Send(peers[0], MAvatar, a);
            break;
        }
        case MLobby:
        {
            size_t p = 0;
            for (int i = 0; i < MaxPlayers; i++) { size_t nl = m.find('\n', p); names[i] = wordfilter::Clean(m.substr(p, nl - p)); p = nl == std::string::npos ? m.size() : nl + 1; }
            for (int i = 0; i < MaxPlayers && p + i < m.size(); i++) roles[i] = std::min(std::max(m[p + i] - '0', 0), RoleCount - 1);
            if (p + MaxPlayers < m.size()) versus = m[p + MaxPlayers] == '1';
            for (int i = 0; i < MaxPlayers && p + MaxPlayers + 1 + i < m.size(); i++) avatars[i] = ClampAvatar(m[p + MaxPlayers + 1 + i] - '0');
            if (p + 2 * MaxPlayers + 1 < m.size()) splitMoney = m[p + 2 * MaxPlayers + 1] == '1';
            break;
        }
        case MRefuse: EndSession(("Couldn't join: " + m).c_str()); break;
        case MStart:
        case MJoin:
            start.seed = Get<uint32_t>(m, o); start.mode = Get<uint8_t>(m, o); start.tank = Get<uint8_t>(m, o); start.mutators = Get<int32_t>(m, o);
            for (int i = 0; i < MaxPlayers; i++) roles[i] = Get<uint8_t>(m, o);
            scaling = Get<uint8_t>(m, o) != 0;
            versus = Get<uint8_t>(m, o) != 0;
            splitMoney = Get<uint8_t>(m, o) != 0;
            start.profile = m.substr(o);
            joining = type == MJoin;   // joining: no start of our own; the host's next snapshot brings us in
            startPending = !joining;
            vsDash = vsCooldown = 0; vsFire = false; vsOver = false;
            snapTick = -1; snapData.clear(); resyncFrom = 0;
            state = Playing; tick = joining ? -1 : 0; bundles.clear(); pending.clear(); hashParts.clear(); rngParts.clear(); rngCalls.clear(); inputLog.clear(); desyncLogged = false;
            api->set_input_filter(InputFilter);
            Log(joining ? "joining the game in progress (mode %d, tank %d)" : "the host starts (mode %d, tank %d)", start.mode, start.tank);
            break;
        case MSnap:
        {
            int t = Get<int32_t>(m, o), idx = Get<uint16_t>(m, o), count = Get<uint16_t>(m, o);
            if (idx == 0) { snapData.clear(); snapTick = t; snapParts = count; snapGot = 0; }
            if (idx == 0 && joining && tick < 0) { tick = t; bundles.erase(bundles.begin(), bundles.lower_bound(t)); }
            if (t == snapTick && idx == snapGot) { snapData += m.substr(o); snapGot++; }
            break;
        }
        case MBundle:
        {
            int t = Get<int32_t>(m, o);
            std::vector<Ev> evs;
            while (o + sizeof(Ev) <= m.size()) evs.push_back(Get<Ev>(m, o));
            bundles[t] = evs;
            break;
        }
        case MDesync:
        {
            int t = Get<int32_t>(m, o);
            Log("OUT OF SYNC at tick %d", t);
            if (!desyncLogged) { desyncLogged = true; LogHashParts(t - HashEvery); LogHashParts(t); }
            api->toast("Out of sync with the host!");
            break;
        }
        case MEnd: state = Lobby; RestoreProfile(); api->set_input_filter(nullptr); status = "Back in the lobby"; break;
        case MBye: EndSession("The host left the game"); break;
    }
}

// ---- local input: to the host instead of the game ---------------------------------------------------------------------
static int pendingMoveX = -1, pendingMoveY = -1;
static void SendChat(const std::string& text)
{
    if (text.empty()) return;
    if (host) { std::string c; Put<uint8_t>(c, 0); c += text; Broadcast(MChat, c); Handle(-1, MChat, c); }
    else Send(peers[0], MChat, text);
}

// keys that only do something on this machine (screenshot, frame counter): never sent to the others
static bool LocalKey(int vk) { return vk == api->config_int("screenshot", "key", VK_F12) || vk == api->config_int("fps", "key", VK_F3); }

static int InputFilter(unsigned msg, unsigned wp, long lp)
{
    if (state != Playing) return 0;
    // chat: Enter opens a line, Enter sends it, Esc drops it; while typing, keys don't go to the game
    if (chatting)
    {
        if (msg == WM_KEYDOWN && wp == VK_RETURN) { chatting = false; SendChat(chatBuf); chatBuf.clear(); api->redraw(); }
        else if (msg == WM_KEYDOWN && wp == VK_ESCAPE) { chatting = false; chatBuf.clear(); api->redraw(); }
        else if (msg == WM_KEYDOWN && wp == VK_BACK) { if (!chatBuf.empty()) chatBuf.pop_back(); api->redraw(); }
        else if (msg == WM_CHAR && wp >= 32 && wp < 127 && chatBuf.size() < 80) { chatBuf += (char)wp; api->redraw(); }
        if (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) return 1;
    }
    else if (msg == WM_KEYDOWN && wp == VK_RETURN) { chatting = true; chatBuf.clear(); api->redraw(); return 1; }
    // a ping: middle click shows a ring where you point, for everyone (display only)
    if (msg == WM_MBUTTONDOWN)
    {
        std::string c;
        Put<int16_t>(c, (int16_t)(short)LOWORD(lp)); Put<int16_t>(c, (int16_t)(short)HIWORD(lp));
        if (host) { std::string h; Put<uint8_t>(h, 0); h += c; Broadcast(MMark, h); Handle(-1, MMark, h); }
        else Send(peers[0], MMark, c);
        return 1;
    }
    if (msg == WM_MBUTTONUP) return 1;
    int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
    Ev e{ (uint8_t)localSlot, 0, (int16_t)x, (int16_t)y, 0 };
    switch (msg)
    {
        case WM_MOUSEMOVE: pendingMoveX = x; pendingMoveY = y; return 1;   // the latest position, once a tick
        case WM_LBUTTONDOWN: e.kind = EvDown; break;
        case WM_LBUTTONUP: e.kind = EvUp; break;
        case WM_LBUTTONDBLCLK: e.kind = EvDbl; break;
        case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: e.kind = EvRDown; break;
        case WM_RBUTTONUP: e.kind = EvRUp; break;
        case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: e.kind = EvMDown; break;
        case WM_MBUTTONUP: e.kind = EvMUp; break;
        case WM_MOUSEWHEEL: e.kind = EvWheel; e.v = (uint16_t)HIWORD(wp); e.x = e.y = 0; break;
        case WM_KEYDOWN: case WM_SYSKEYDOWN: if (LocalKey((int)wp)) return 0; e.kind = EvKeyDown; e.v = (uint16_t)wp; e.x = e.y = 0; break;
        case WM_KEYUP: case WM_SYSKEYUP: if (LocalKey((int)wp)) return 0; e.kind = EvKeyUp; e.v = (uint16_t)wp; e.x = e.y = 0; break;
        case WM_CHAR: e.kind = EvChar; e.v = (uint16_t)wp; e.x = e.y = 0; break;
        default: return 1;
    }
    if (pendingMoveX >= 0 && e.kind <= EvWheel && e.kind != EvWheel) { Ev mv{ (uint8_t)localSlot, EvMove, (int16_t)x, (int16_t)y, 0 }; pending.push_back(mv); pendingMoveX = -1; }
    pending.push_back(e);
    return 1;
}

static void FlushLocal()
{
    if (pendingMoveX >= 0) { pending.push_back({ (uint8_t)localSlot, EvMove, (int16_t)pendingMoveX, (int16_t)pendingMoveY, 0 }); pendingMoveX = -1; }
    if (host || pending.empty()) return;
    std::string s;
    for (auto& e : pending) Put(s, e);
    pending.clear();
    Send(peers[0], MInput, s);
}

static void Apply(const std::vector<Ev>& evs)
{
    for (auto& e : evs)
    {
        if (e.slot >= MaxPlayers) continue;
        if (e.kind == EvLeave) { inGame[e.slot] = false; Log("slot %d left at tick %d", e.slot, tick); continue; }
        if (e.kind == EvSpeed) { if (e.slot == 0) ApplySpeed(e.x, e.y != 0); continue; }
        if (!inGame[e.slot]) continue;   // a joiner's input before the snapshot that brings them in
        long lp = MAKELPARAM(e.x, e.y);
        if (e.slot < MaxPlayers && e.kind <= EvMUp) { lastX[e.slot] = e.x; lastY[e.slot] = e.y; }
        if (e.slot == VersusSlot && Versus())   // the aliens' player: steering (lastX/Y) and a click to dash or fire
        {
            void* b = api->board();
            if (e.kind == EvDown && vsCooldown == 0 && b && Count(b, Board_mAliens) > 0) { vsDash = 40; vsFire = true; vsCooldown = 250; }
            continue;
        }
        lastInputTick = tick;
        UseWallet(e.slot);
        actingSlot = e.slot;
        switch (e.kind)
        {
            case EvMove: api->inject_input(WM_MOUSEMOVE, 0, lp); break;
            case EvDown: api->inject_input(WM_MOUSEMOVE, 0, lp); api->inject_input(WM_LBUTTONDOWN, MK_LBUTTON, lp); break;
            case EvUp: api->inject_input(WM_LBUTTONUP, 0, lp); break;
            case EvDbl: api->inject_input(WM_LBUTTONDBLCLK, MK_LBUTTON, lp); break;
            case EvRDown: api->inject_input(WM_MOUSEMOVE, 0, lp); api->inject_input(WM_RBUTTONDOWN, MK_RBUTTON, lp); break;
            case EvRUp: api->inject_input(WM_RBUTTONUP, 0, lp); break;
            case EvMDown: api->inject_input(WM_MBUTTONDOWN, MK_MBUTTON, lp); break;
            case EvMUp: api->inject_input(WM_MBUTTONUP, 0, lp); break;
            case EvWheel: api->inject_input(WM_MOUSEWHEEL, (unsigned)e.v << 16, 0); break;
            case EvKeyDown: api->inject_input(WM_KEYDOWN, e.v, 1); break;
            case EvKeyUp: api->inject_input(WM_KEYUP, e.v, 0xc0000001); break;
            case EvChar: api->inject_input(WM_CHAR, e.v, 1); break;
        }
        if (e.kind == EvDown || e.kind == EvDbl || e.kind == EvRDown) NotePress(e);
        actingSlot = -1;
    }
    UseWallet(0);   // split wallets: the game's own update runs with the host's money
}

// ---- the start (on every machine, at tick 0 of the session) ------------------------------------------------------------
// input still waiting in the game's own message queue from before the session (the click on Start, a click in the
// menu) would be handled inside the co-op game on this machine only: dropped (other queued messages stay)
static void DropQueuedInput(void* app)
{
    char* head = at<char*>(app, App_mDeferredHead);
    if (!head) return;
    int dropped = 0;
    for (char* n = *reinterpret_cast<char**>(head); n != head && dropped < 100000;)
    {
        char* next = *reinterpret_cast<char**>(n);
        UINT msg = reinterpret_cast<MSG*>(n + 8)->message;
        if ((msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || (msg >= WM_KEYFIRST && msg <= WM_KEYLAST))
        {
            char* prev = *reinterpret_cast<char**>(n + 4);
            *reinterpret_cast<char**>(prev) = next;          // unlink, as the game's own pop does
            *reinterpret_cast<char**>(next + 4) = prev;
            reinterpret_cast<void(__cdecl*)(void*)>(Game_OperatorDelete)(n);
            at<uint32_t>(app, App_mDeferredCount)--;
            dropped++;
        }
        n = next;
    }
    if (dropped) Log("dropped %d queued input message(s) from before the session", dropped);
}

// a button held when the session starts leaves the game's widget manager in a pressed state on this machine only (its
// release goes into the session's input): the game would send this machine's clicks to the old widget, and its moves
// would be drags. Cleared on every machine, so all start with no button down
static void ReleaseButtons(void* app)
{
    char* wm = at<char*>(app, App_mWidgetManager);
    if (!wm) return;
    char* w = at<char*>(wm, WM_mLastDownWidget);
    if (!w && !at<uint32_t>(wm, WM_mDownButtons) && !at<uint32_t>(wm, WM_mActualDownButtons)) return;
    if (w) at<bool>(w, Widget_mIsDown) = false;
    at<char*>(wm, WM_mLastDownWidget) = nullptr;
    at<uint32_t>(wm, WM_mDownButtons) = 0;
    at<uint32_t>(wm, WM_mActualDownButtons) = 0;
    ReleaseCapture();
    Log("released a mouse button held from before the session");
}

static void DoStart(void* app)
{
    startPending = false;
    DropQueuedInput(app);
    ReleaseButtons(app);
    lastInputTick = 0;
    coopFrameTime = 28; coopPaused = false; at<int>(app, App_mFrameTime) = 28;   // every game starts at normal speed
    for (int i = 0; i < MaxPlayers; i++) { inGame[i] = !names[i].empty(); joiningSlot[i] = false; }
    walletSlot = 0; walletsReset = true; coinOwner.clear();
    if (!host) { savedProfile = ProfileData(); SetProfileData(start.profile); }
    ui::KillDialog(api, 0x4e);
    uint32_t* mt = at<uint32_t*>(app, App_mMTRand);
    mt[0] = start.seed ? start.seed : 4357;
    for (int i = 1; i < 624; i++) mt[i] = 1812433253u * (mt[i - 1] ^ (mt[i - 1] >> 30)) + i;
    mt[624] = 624;
    if (HMODULE m = GetModuleHandleA("mutators.dll"))
        if (auto f = reinterpret_cast<void (*)(int)>(GetProcAddress(m, "MutatorsForce"))) f(start.mutators);
    at<bool>(app, 0x880) = false;
    reinterpret_cast<void(__thiscall*)(void*)>(App_RemoveGameSelector)(app);
    at<int>(app, App_mGameMode) = start.mode;
    at<int>(app, 0x888) = start.tank;
    reinterpret_cast<void(__thiscall*)(void*, bool, bool)>(App_StartGame)(app, false, false);
    int players = 0;
    for (int i = 0; i < MaxPlayers; i++) if (!names[i].empty()) players++;
    if (HMODULE m = GetModuleHandleA("mutators.dll"))
        if (auto f = reinterpret_cast<void (*)(double, double)>(GetProcAddress(m, "MutatorsSetCoopScale")))
            f(scaling ? 1 + 0.25 * (players - 1) : 1, scaling ? 1 + 0.5 * (players - 1) : 1);
    if (players >= 4) Achievement("coop_full");
    if (void* p = Profile()) startShells = !host ? at<int>(p, Profile_mShells) : -1;
    Log("session started: mode %d tank %d, %d players", start.mode, start.tank, players);
}

// how the game was started (the start, and for a player joining later)
static std::string StartMessage()
{
    std::string s;
    Put(s, start.seed); Put(s, start.mode); Put(s, start.tank); Put(s, start.mutators);
    for (int i = 0; i < MaxPlayers; i++) Put<uint8_t>(s, (uint8_t)roles[i]);
    Put<uint8_t>(s, scaling ? 1 : 0);
    Put<uint8_t>(s, versus ? 1 : 0);
    Put<uint8_t>(s, splitMoney ? 1 : 0);
    s += start.profile;
    return s;
}

static void HostStart(int mode, int tank)
{
    start.seed = GetTickCount() | 1;
    start.mode = (uint8_t)mode; start.tank = (uint8_t)tank;
    int mut = 0;
    if (HMODULE m = GetModuleHandleA("mutators.dll"))
        if (auto f = reinterpret_cast<int (*)(int)>(GetProcAddress(m, "MutatorsFromSettings"))) mut = f(0);
    start.mutators = mut;
    start.profile = ProfileData();
    Broadcast(MStart, StartMessage());
    snapTick = -1; snapData.clear(); resyncWanted = false; resyncFrom = 0; lastResync = -100000;
    state = Playing; tick = 0; nextBundle = 0; bundles.clear(); pending.clear(); hostHashes.clear(); hashParts.clear(); rngParts.clear(); rngCalls.clear(); inputLog.clear(); desyncLogged = false;
    for (auto& p : peers) p.ack = -1;   // (still joinable while there's room: a joiner comes in through a snapshot)
    vsDash = vsCooldown = 0; vsFire = false; vsOver = false;
    startPending = true;
    api->set_input_filter(InputFilter);
}

// ---- the tick ---------------------------------------------------------------------------------------------------------------
static void(__thiscall* oUpdateFrames)(void*);
static void __fastcall UpdateFrames(void* app, void*)
{
    Poll();
    if (state != Playing) { oUpdateFrames(app); return; }
    FlushLocal();
    if (host)
    {
        int minAck = tick;
        for (int i = 1; i < MaxPlayers; i++) if (peers[i].used && peers[i].ack + 1 < minAck) minAck = peers[i].ack + 1;
        while (nextBundle <= tick + Delay && nextBundle - minAck < MaxAhead)
        {
            std::string s; Put<int32_t>(s, nextBundle);
            for (auto& e : pending) Put(s, e);
            bundles[nextBundle] = pending;
            pending.clear();
            Broadcast(MBundle, s);
            nextBundle++;
        }
    }
    int backlog = 0;
    for (auto it = bundles.lower_bound(tick); it != bundles.end() && it->first == tick + backlog; ++it) backlog++;
    int runs = backlog > Delay + 2 ? std::min(backlog - Delay, 4) : 1;   // behind: catch up a little each frame
    for (int r = 0; r < runs; r++)
    {
        auto it = bundles.find(tick);
        if (it == bundles.end() && snapParts > 0 && snapGot == snapParts && snapTick > tick && !host)
        {   // ticks lost while the connection was down: straight to the host's snapshot
            Log("resync: skipping ticks %d-%d (lost while disconnected)", tick, snapTick - 1);
            tick = snapTick;
            bundles.erase(bundles.begin(), bundles.lower_bound(tick));
            it = bundles.find(tick);
        }
        if (it == bundles.end()) return;   // waiting for the host (the game doesn't advance this tick)
        inLockstep = true;
        if (tick == 0 && startPending) DoStart(app);
        if (tick == snapTick)
        {
            if (snapGot < snapParts) { inLockstep = false; return; }   // the rest of the snapshot is on its way
            bool ok = LoadSnapshot(snapData);
            Log("resync: %s the host's snapshot (%u bytes) at tick %d", ok ? "loaded" : "COULDN'T LOAD", (unsigned)snapData.size(), tick);
            if (desyncLogged && ok) api->toast("Back in step with the host");
            desyncLogged = false; resyncFrom = tick;
            snapTick = -1; snapData.clear();
        }
        ResetWallets();
        Apply(it->second);
        bundles.erase(it);
        oUpdateFrames(app);
        ResetWallets();
        if (Split()) if (void* b = api->board()) reinterpret_cast<void(__thiscall*)(void*)>(Board_UpdateMoneyLabel)(b);   // your own money
        if (Versus() && !vsOver)   // the aliens win when every fish is gone (the same tick everywhere)
            if (void* b = api->board())
            {
                int fish = 0;
                for (int l : { Board_mGuppies, Board_mBreeders, Board_mOscars, Board_mUltras, Board_mGekkos, Board_mPentas, Board_mGrubbers }) fish += Count(b, l);
                if (fish == 0 && tick > 100)
                {
                    vsOver = true;
                    api->toast((names[VersusSlot] + " and the aliens win!").c_str());
                    Log("versus: the aliens won at tick %d", tick);
                }
            }
        vsFire = false;
        if (vsDash > 0) vsDash--;
        if (vsCooldown > 0) vsCooldown--;
        inLockstep = false;
        if (tick % HashEvery == 0)
        {
            uint64_t h = Hash();
            hashParts[tick] = HashParts();
            while (hashParts.size() > 32) hashParts.erase(hashParts.begin());
            rngParts[tick] = RngParts();
            while (rngParts.size() > 32) rngParts.erase(rngParts.begin());
            if (tick % (HashEvery * 8) == 0) Log("tick %d hash %016llx", tick, (unsigned long long)h);
            if (host) hostHashes[tick] = h;
            else { std::string s; Put<int32_t>(s, tick); Put<uint64_t>(s, h); Send(peers[0], MHash, s); }
        }
        if (!host && tick % 4 == 0) { std::string s; Put<int32_t>(s, tick); Send(peers[0], MAck, s); }
        tick++;
        if (state != Playing) return;
        // host: a snapshot of this state, loaded by everyone at the first tick whose input isn't sent yet (it goes
        // out before that bundle, so every guest has it in time; the ticks in between are undone alike everywhere)
        if (host && snapTick < 0 && api->board() && (resyncWanted || (resyncTest > 0 && tick % resyncTest == 0)))
        {
            resyncWanted = false; lastResync = tick;
            std::string snap = TakeSnapshot();
            if (snap.empty()) { Log("resync: no snapshot (the game didn't save)"); continue; }
            const size_t Part = 60000;
            int count = (int)((snap.size() + Part - 1) / Part);
            for (int i = 0; i < count; i++)
            {
                std::string m; Put<int32_t>(m, nextBundle); Put<uint16_t>(m, (uint16_t)i); Put<uint16_t>(m, (uint16_t)count);
                m += snap.substr(i * Part, Part);
                Broadcast(MSnap, m);
            }
            snapData = snap; snapTick = nextBundle; snapParts = snapGot = count;
        }
    }
}

// pauses only from the game's own reaction to everyone's input (not a window losing focus on one machine)
static void(__thiscall* oPause)(void*, bool);
static void __fastcall PauseHook(void* b, void*, bool on)
{
    if (state == Playing && !inLockstep) return;
    oPause(b, on);
}
static void ApplySpeed(int frameTime, bool paused)
{
    coopFrameTime = frameTime; coopPaused = paused;
    if (void* a = api->app()) at<int>(a, App_mFrameTime) = frameTime;
    if (void* b = api->board()) oPause(b, paused);
}
static void(__thiscall* oLostFocus)(void*);
static void __fastcall LostFocus(void* a, void*) { if (state != Playing) oLostFocus(a); }

// guests never save (they play the host's profile)
static bool __fastcall SaveProfile(void* a, void*) { if ((state != Off) && !host) return true; return oSaveProfile(a); }
static void(__thiscall* oSaveGame)(void*);
static void __fastcall SaveGame(void* b, void*) { if (state == Playing && !host) return; oSaveGame(b); }

// in a co-op lobby, no game of your own: when the host starts, every game must begin from the same clean state
// (a guest who was in a tank of their own carried its state into the co-op game and fell out of step)
static bool reopenScreen;
static void(__thiscall* oStartGame)(void*, bool, bool);
static void(__thiscall* oStartBoard)(void*);
static void __fastcall StartBoard(void* a, void*)   // the modes that start their own board (Play as the Alien)
{
    if (state == Lobby && !inLockstep)
    {
        api->toast("You're in a co-op lobby: leave it (F7) to play on your own");
        reopenScreen = true;
        return;
    }
    oStartBoard(a);
}
static void __fastcall StartGame(void* a, void*, bool checkContinue, bool showHelp)
{
    if (state == Lobby && !inLockstep)
    {
        api->toast("You're in a co-op lobby: leave it (F7) to play on your own");
        reopenScreen = true;
        return;
    }
    oStartGame(a, checkContinue, showHelp);
}

// the main menu again: the game is over, back to the lobby
static void(__thiscall* oSelector)(void*);
static void __fastcall Selector(void* a, void*)
{
    oSelector(a);
    if (state == Playing && inLockstep)
    {
        state = Lobby;
        RestoreProfile();
        api->set_input_filter(nullptr);
        status = host ? "Back in the lobby: start another game" : "Back in the lobby";
        if (net == NetSteam && host) steam::SetJoinable(true);
        if (net == NetServer && host) SendLobby();   // listed again, with the player count
        reopenScreen = true;   // the lobby screen comes back (next frame, on the main menu)
        Log("back to the lobby at tick %d", tick);
    }
}

// ---- a bug in the game: Shot's constructor (@004ec660) tests its kind (+0x160) before setting it, and draws a random
// number unless the leftover heap memory there is 0 or 2, so machines whose heaps differ fell out of step when an alien
// died. In a session the field is cleared first (as for fresh memory: no draw)
static void*(__thiscall* oShotCtor)(void*, int, int, int);
static void* __fastcall ShotCtor(void* t, void*, int x, int y, int kind)
{
    if (state == Playing) at<int>(t, 0x160) = 0;
    return oShotCtor(t, x, y, kind);
}

// ---- split wallets (host's choice): every player has their own money. The board's mMoney holds one player's at a time:
// the acting player's while their input is applied, the host's (slot 0) during the game's own update. Coins pay
// whoever clicked them, hold-to-feed charges whoever dropped the first pellet, anything else (pets' pickups) is shared
// out evenly. All decided from lockstep input, so every machine keeps the same wallets. Not in the Virtual Tank
static bool Split() { void* a = api->app(); return state == Playing && splitMoney && api->board() && a && at<int>(a, App_mGameMode) != 5; }
static bool HasWallet(int s) { return s >= 0 && s < MaxPlayers && inGame[s] && !(s == VersusSlot && Versus()); }
static int WalletOf(int s) { void* b = api->board(); return s == walletSlot && b ? at<int>(b, Board_mMoney) : wallets[s]; }
static void UseWallet(int s)
{
    void* b = api->board();
    if (!Split() || !b || s == walletSlot || s < 0 || s >= MaxPlayers) return;
    wallets[walletSlot] = at<int>(b, Board_mMoney);
    at<int>(b, Board_mMoney) = wallets[s];
    walletSlot = s;
}
// a level starts (Board::InitLevel, after every mod's changes to the starting money): everyone gets the same
static void ResetWallets()
{
    if (!walletsReset) return;
    walletsReset = false;
    walletSlot = 0; feedSlot = 0; coinOwner.clear();
    if (void* b = api->board()) for (auto& w : wallets) w = at<int>(b, Board_mMoney);
}
static void(__thiscall* oInitLevel)(void*);
static void __fastcall InitLevel(void* b, void*) { oInitLevel(b); walletsReset = true; }
static void(__thiscall* oAddMoney)(void*, int);
static void __fastcall AddMoney(void* b, void*, int v)
{
    if (!Split() || b != api->board()) { oAddMoney(b, v); return; }
    int to = moneyOwner >= 0 ? moneyOwner : actingSlot, keep = walletSlot;
    if (HasWallet(to)) { UseWallet(to); oAddMoney(b, v); UseWallet(keep); return; }
    int n = 0, k = 0;
    for (int s = 0; s < MaxPlayers; s++) n += HasWallet(s) ? 1 : 0;
    if (n == 0 || v <= 0) { oAddMoney(b, v); return; }
    for (int s = 0; s < MaxPlayers; s++)
        if (HasWallet(s)) { int part = v / n + (k++ < v % n ? 1 : 0); if (part) { UseWallet(s); oAddMoney(b, part); } }   // the remainder to the first players
    UseWallet(keep);
}
static void(__thiscall* oReceiveMoney)(void*);
static void __fastcall ReceiveMoney(void* c, void*)
{
    auto it = coinOwner.find(c);
    int keep = moneyOwner;
    moneyOwner = it != coinOwner.end() ? it->second : -1;
    if (it != coinOwner.end()) coinOwner.erase(it);
    oReceiveMoney(c);
    moneyOwner = keep;
}
static void(__thiscall* oRemoveFromGame)(void*, bool);
static void __fastcall RemoveFromGame(void* o, void*, bool del) { coinOwner.erase(o); oRemoveFromGame(o, del); }
// the money shown is your own
static void(__thiscall* oMoneyLabel)(void*);
static void __fastcall MoneyLabel(void* b, void*)
{
    if (!Split() || localSlot == walletSlot || !HasWallet(localSlot) || b != api->board()) { oMoneyLabel(b); return; }
    int& m = at<int>(b, Board_mMoney);
    int saved = m;
    m = wallets[localSlot];
    oMoneyLabel(b);
    m = saved;
}

// ---- roles: the player whose input the game is handling gets the bonus (decided from lockstep input: the same everywhere)
static int ActingRole() { return state == Playing && actingSlot >= 0 && actingSlot < MaxPlayers ? roles[actingSlot] : RoleNone; }
static bool(__thiscall* oSpendMoney)(void*, int, bool);
static bool __fastcall SpendMoney(void* b, void*, int amount, bool loud)
{
    if (Split() && actingSlot < 0 && at<bool>(b, Board_mHoldFeed) && amount == at<int>(b, Board_mFoodPrice) && HasWallet(feedSlot))
    {   // hold-to-feed during the game's own update: whoever dropped the first pellet pays (and gets their role's price)
        int keep = walletSlot;
        UseWallet(feedSlot); actingSlot = feedSlot;
        bool r = SpendMoney(b, nullptr, amount, loud);
        actingSlot = -1; UseWallet(keep);
        return r;
    }
    if (actingSlot >= 0 && amount == at<int>(b, Board_mFoodPrice)) feedSlot = actingSlot;
    if (ActingRole() == Feeder && amount == at<int>(b, Board_mFoodPrice)) amount = std::max(1, amount / 2);   // mFoodPrice: a pellet
    return oSpendMoney(b, amount, loud);
}
static bool(__thiscall* oShoot)(void*, int, int);
static bool __fastcall Shoot(void* al, void*, int x, int y)
{
    void* b = api->board();
    if (ActingRole() != Gunner || !b) return oShoot(al, x, y);
    int& level = at<int>(b, Board_mWeaponLevel);
    int saved = level;
    level += 2;
    bool r = oShoot(al, x, y);
    if (api->board() == b) level = saved;
    return r;
}
static void(__thiscall* oCoinMouseDown)(void*, int, int, int);
static void __fastcall CoinMouseDown(void* c, void*, int x, int y, int clicks)
{
    bool was = at<bool>(c, Coin_mCollected);
    int role = ActingRole();
    oCoinMouseDown(c, x, y, clicks);
    void* b = api->board();
    if (!was && at<bool>(c, Coin_mCollected) && actingSlot >= 0) coinOwner[c] = actingSlot;   // split wallets: it pays them when it arrives
    if (role == Collector && b && !was && at<bool>(c, Coin_mCollected) && at<int>(c, Coin_mCoinType) < 0xf)
    {
        int v = reinterpret_cast<int(__thiscall*)(void*)>(Coin_GetValue)(c);
        if (v >= 4) reinterpret_cast<void(__thiscall*)(void*, int)>(Board_AddMoney)(b, v / 4);
    }
}
static void(__thiscall* oBuyItem)(void*, int);
static void __fastcall BuyItem(void* b, void*, int item)
{
    if (ActingRole() != Shopper || item < 0 || item >= 12) { oBuyItem(b, item); return; }
    int& price = (&at<int>(b, Board_mPrice))[item];
    int full = price, cheap = std::max(0, (int)(price * 0.85 / 5 + 0.5) * 5);
    price = cheap;
    oBuyItem(b, item);
    if (api->board() == b && price == cheap) price = full;   // the game didn't move it on: back to the normal price
}

// ---- pets nap after 6480 updates without input: the game counts from the last input this machine's widget manager saw,
// which local-only events move (the cursor leaving the window, keys the filter passes); in a session, from the last
// lockstep input instead, the same everywhere
static void(__thiscall* oPetSleepy)(void*, bool*);
static void __fastcall PetSleepy(void* o, void*, bool* sleepy)
{
    char* wm = at<char*>(api->app(), App_mWidgetManager);
    if (state != Playing || !wm) { oPetSleepy(o, sleepy); return; }
    int& last = at<int>(wm, WM_mLastInputUpdateCnt), saved = last;
    last = at<int>(wm, WM_mUpdateCnt) - (tick - lastInputTick);
    oPetSleepy(o, sleepy);
    last = saved;
}

// ---- rescue: two different players click a starving fish within 100 ticks -----------------------------------------------
static void* rescueFish;
static int rescueSlot = -1, rescueTick;
static bool Starving(void* f) { int t = at<int>(f, GameObject_mHungerTimer); return t >= 1 && t < 200; }
static void(__thiscall* oWidgetMouseDown)(void*, int, int, int);
static void __fastcall WidgetMouseDown(void* w, void*, int x, int y, int clicks)
{
    void* b = api->board();
    if (state == Playing && actingSlot >= 0 && b)
    {
        bool fish = false;
        for (int l : { Board_mGuppies, Board_mBreeders, Board_mOscars, Board_mUltras })
            for (int i = 0; i < Count(b, l) && !fish; i++) if (Item(b, l, i) == w) fish = true;
        if (fish && Starving(w))
        {
            int t = at<int>(b, Board_mTick);
            if (rescueFish == w && rescueSlot != actingSlot && t - rescueTick <= 100)
            {
                at<int>(w, GameObject_mHungerTimer) = 1000;
                reinterpret_cast<void(__thiscall*)(void*, bool)>(GameObject_SetHungryFlash)(w, false);
                api->toast("Rescued! Teamwork saves the day!");
                Achievement("teamwork");
                rescueFish = nullptr; rescueSlot = -1;
            }
            else { rescueFish = w; rescueSlot = actingSlot; rescueTick = t; }
        }
    }
    oWidgetMouseDown(w, x, y, clicks);
}

// ---- snapshots: the board through the game's own save (its bytes caught on their way to the file: nothing is written)
// and loaded through its own load (the read served from memory: no file is read), plus what the save leaves out
static bool capturing, loadingSnap;
static std::string captured;
static bool(__thiscall* oWriteBytes)(void*, const void*, const void*, size_t);
static bool __fastcall WriteBytes(void* app, void*, const void* path, const void* data, size_t len)
{
    if (!capturing) return oWriteBytes(app, path, data, len);
    captured.assign(static_cast<const char*>(data), len);
    return true;
}
static bool(__thiscall* oReadBuffer)(void*, const void*, void*, bool);
static bool __fastcall ReadBuffer(void* app, void*, const void* path, void* buf, bool noDemo)
{
    if (!loadingSnap) return oReadBuffer(app, path, buf, noDemo);
    auto writeByte = reinterpret_cast<void(__stdcall*)(void*, unsigned char)>(Buffer_WriteByte);
    for (unsigned char c : captured) writeByte(buf, c);
    return true;
}
static void(__thiscall* oContinueDialog)(void*);
static void __fastcall ContinueDialog(void* app, void*) { if (!loadingSnap) oContinueDialog(app); }
template <typename T> static T& G(uintptr_t a) { return *reinterpret_cast<T*>(a); }

static std::string TakeSnapshot()
{
    void* a = api->app(); void* b = api->board();
    std::string s;
    Put<uint32_t>(s, 3);
    uint32_t* mt = at<uint32_t*>(a, App_mMTRand);
    for (int i = 0; i < 625; i++) Put<uint32_t>(s, mt[i]);
    Put<int32_t>(s, G<int>(G_FoodQuality)); Put<int32_t>(s, G<int>(G_FoodQuantity)); Put<int32_t>(s, G<int>(G_Unk89c0));
    Put<uint8_t>(s, G<uint8_t>(G_FastCoins)); Put<uint8_t>(s, G<uint8_t>(G_BonesMode));
    Put<int32_t>(s, G<int>(G_WadsworthHiding)); Put<int32_t>(s, G<int>(G_WadsworthX)); Put<int32_t>(s, G<int>(G_WadsworthY));
    for (int i = 0; i < MaxPlayers; i++) Put<int32_t>(s, WalletOf(i));
    Put<int32_t>(s, feedSlot);
    // co-op's own simulation state (unchanged for the players already in; a joiner needs it), joiners now in the game
    for (int i = 0; i < MaxPlayers; i++) { Put<uint8_t>(s, inGame[i] || joiningSlot[i]); joiningSlot[i] = false; }
    Put<int32_t>(s, tick - lastInputTick); Put<int32_t>(s, vsDash); Put<int32_t>(s, vsCooldown); Put<uint8_t>(s, vsFire); Put<uint8_t>(s, vsOver);
    for (int i = 0; i < MaxPlayers; i++) { Put<int16_t>(s, (int16_t)lastX[i]); Put<int16_t>(s, (int16_t)lastY[i]); }
    // other mods' state for this level
    char mb[256]; int ml = 0;
    if (HMODULE m = GetModuleHandleA("mutators.dll"))
        if (auto f = reinterpret_cast<int (*)(void*, int)>(GetProcAddress(m, "MutatorsSave"))) ml = f(mb, sizeof mb);
    Put<uint32_t>(s, (uint32_t)ml); s.append(mb, ml);
    int cb = 0;
    if (HMODULE m = GetModuleHandleA("continues.dll"))
        if (auto f = reinterpret_cast<int (*)()>(GetProcAddress(m, "ContinuesBought"))) cb = f();
    Put<int32_t>(s, cb);
    Put<int32_t>(s, coopFrameTime); Put<uint8_t>(s, coopPaused);
    std::string prof = ProfileData();   // the host's profile now (a joiner plays it; the others' copies already match)
    Put<uint32_t>(s, (uint32_t)prof.size()); s += prof;
    bool& should = at<bool>(b, Board_mShouldSave);
    bool keep = should;
    should = true;   // else the game erases the save file instead of saving
    capturing = true; captured.clear();
    oSaveGame(b);
    capturing = false; should = keep;
    if (captured.empty()) return "";
    Put<uint32_t>(s, (uint32_t)captured.size()); s += captured;
    captured.clear();
    return s;
}

static bool LoadSnapshot(const std::string& s)
{
    void* a = api->app();
    size_t o = 0;
    if (Get<uint32_t>(s, o) != 3) return false;
    uint32_t* mt = at<uint32_t*>(a, App_mMTRand);
    for (int i = 0; i < 625; i++) mt[i] = Get<uint32_t>(s, o);
    G<int>(G_FoodQuality) = Get<int32_t>(s, o); G<int>(G_FoodQuantity) = Get<int32_t>(s, o); G<int>(G_Unk89c0) = Get<int32_t>(s, o);
    G<uint8_t>(G_FastCoins) = Get<uint8_t>(s, o); G<uint8_t>(G_BonesMode) = Get<uint8_t>(s, o);
    G<int>(G_WadsworthHiding) = Get<int32_t>(s, o); G<int>(G_WadsworthX) = Get<int32_t>(s, o); G<int>(G_WadsworthY) = Get<int32_t>(s, o);
    int w[MaxPlayers];
    for (int i = 0; i < MaxPlayers; i++) w[i] = Get<int32_t>(s, o);
    feedSlot = Get<int32_t>(s, o);
    for (int i = 0; i < MaxPlayers; i++) inGame[i] = Get<uint8_t>(s, o) != 0;
    lastInputTick = tick - Get<int32_t>(s, o); vsDash = Get<int32_t>(s, o); vsCooldown = Get<int32_t>(s, o);
    vsFire = Get<uint8_t>(s, o) != 0; vsOver = Get<uint8_t>(s, o) != 0;
    for (int i = 0; i < MaxPlayers; i++) { lastX[i] = Get<int16_t>(s, o); lastY[i] = Get<int16_t>(s, o); }
    uint32_t ml = Get<uint32_t>(s, o);
    if (o + ml > s.size()) return false;
    if (HMODULE m = GetModuleHandleA("mutators.dll"))
        if (auto f = reinterpret_cast<void (*)(const void*, int)>(GetProcAddress(m, "MutatorsLoad"))) f(s.data() + o, (int)ml);
    o += ml;
    int cb = Get<int32_t>(s, o);
    if (HMODULE m = GetModuleHandleA("continues.dll"))
        if (auto f = reinterpret_cast<void (*)(int)>(GetProcAddress(m, "ContinuesSetBought"))) f(cb);
    int ft = Get<int32_t>(s, o); bool pz = Get<uint8_t>(s, o) != 0;
    uint32_t pl = Get<uint32_t>(s, o);
    if (o + pl > s.size()) return false;
    std::string prof = s.substr(o, pl);
    o += pl;
    uint32_t n = Get<uint32_t>(s, o);
    if (o + n > s.size()) return false;
    captured = s.substr(o, n);
    if (joining)
    {   // a joiner: from the main menu into the game, as the start does (but the board comes from the snapshot)
        DropQueuedInput(a);
        savedProfile = ProfileData(); SetProfileData(prof);
        ui::KillDialog(api, 0x4e);
        at<bool>(a, 0x880) = false;
        reinterpret_cast<void(__thiscall*)(void*)>(App_RemoveGameSelector)(a);
        at<int>(a, App_mGameMode) = start.mode;
        at<int>(a, 0x888) = start.tank;
        if (void* p = Profile()) startShells = at<int>(p, Profile_mShells);
    }
    loadingSnap = true;
    bool ok = reinterpret_cast<bool(__thiscall*)(void*)>(App_LoadBoardGame)(a);
    loadingSnap = false; captured.clear();
    void* b = api->board();
    if (!ok || !b) return false;
    // what pointed at the old objects is gone, on every machine alike
    coinOwner.clear(); rescueFish = nullptr; rescueSlot = -1;
    if (HMODULE m = GetModuleHandleA("mutators.dll"))
        if (auto f = reinterpret_cast<void (*)(void*)>(GetProcAddress(m, "MutatorsBoardReloaded"))) f(b);
    ReleaseButtons(a);
    walletsReset = false; walletSlot = 0;
    for (int i = 0; i < MaxPlayers; i++) wallets[i] = w[i];
    if (Split()) at<int>(b, Board_mMoney) = w[0];
    reinterpret_cast<void(__thiscall*)(void*)>(Board_UpdateMoneyLabel)(b);
    ApplySpeed(ft, pz);   // the host's speed (the reloaded board starts unpaused)
    if (joining) { joining = false; Log("joined the game in progress at tick %d", tick); }
    return true;
}

// the level is won (a co-op achievement)
static void(__thiscall* oBank)(void*);
static void __fastcall Bank(void* b, void*)
{
    if (state == Playing && at<int>(b, 0x43c) >= 4) Achievement("coop_level");
    if (Versus() && !vsOver && at<int>(b, 0x43c) >= 4) { vsOver = true; api->toast("The keepers win: the tank is safe!"); Log("versus: the keepers won at tick %d", tick); }
    oBank(b);
}

// versus: every alien follows the aliens' player's pointer (as Play as the Alien steers its alien), dashes on a click;
// the floor walkers (types 5 and 6) fire instead
static bool(__thiscall* oThink)(void*);
static bool __fastcall Think(void* al, void*)
{
    if (!Versus() || !inLockstep) return oThink(al);
    double speedDiv = at<double>(al, Alien_mSpeedDiv);
    int t = at<int>(al, Alien_mAlienType);
    bool walker = t == 5 || t == 6, dashing = vsDash > 0 && !walker;
    double screen = std::min(std::max(1.4 * sqrt(1.6 / speedDiv), 0.9), 1.6) * (dashing ? 2.2 : 1.0);
    double maxV = screen * speedDiv, acc = (dashing ? 0.5 : 0.15) * speedDiv;
    double& vx = at<double>(al, 0x170), &vy = at<double>(al, 0x178);
    double dx = lastX[VersusSlot] - (at<double>(al, 0x160) + 80.0), dy = lastY[VersusSlot] - (at<double>(al, 0x168) + 80.0);
    if (dx > 6 && vx < maxV) vx += acc; else if (dx < -6 && vx > -maxV) vx -= acc; else if (fabs(dx) <= 6) vx *= 0.85;
    if (!walker) { if (dy > 6 && vy < maxV) vy += acc; else if (dy < -6 && vy > -maxV) vy -= acc; else if (fabs(dy) <= 6) vy *= 0.85; }
    if (!dashing) { vx = std::min(std::max(vx, -maxV), maxV); vy = std::min(std::max(vy, -maxV), maxV); }
    if (vsFire && walker) at<int>(al, 0x1a4) = at<int>(al, 0x1a8);   // mFireTimer = mFireDelay: fire now
    if (at<int>(al, 0x1a0) < 1 || t == 4) reinterpret_cast<void(__thiscall*)(void*)>(Alien_TryEat)(al);   // mInvuln
    return true;
}

// each player's pointer as the game last applied it (the same on every machine): for hover coins
extern "C" __declspec(dllexport) int CoopPointer(int slot, int* x, int* y)
{
    if (state != Playing || slot < 0 || slot >= MaxPlayers || !inGame[slot] || (slot == VersusSlot && Versus())) return 0;
    *x = lastX[slot]; *y = lastY[slot];
    return 1;
}

extern "C" __declspec(dllexport) int CoopActive() { return state != Off ? 1 : 0; }
extern "C" __declspec(dllexport) int CoopPlaying() { return state == Playing ? 1 : 0; }

// ---- the lobby screen ------------------------------------------------------------------------------------------------------
static const int DialogId = 0x4e, DX = 30, DY = 20, DW = 580, DH = 440, CX = DX + 40, CW = DW - 80;
static bool screen, editing;
static std::string* editField;   // the box being typed into (the address, or a server setting)
// the server settings view (Online tab): edited here, saved on Test and Done
static bool serverView, srvTls;
static std::string srvHost, srvPort, srvKey, srvTest;
static std::string address;
static int pickMode = 0, pickTank = 1;   // what the host starts: 0 Adventure, 1 Time Trial, 4 Challenge
// not in a game yet: by address on top, Steam below
static RECT HostBtn() { return { CX, DY + 90, CX + 140, DY + 119 }; }
static RECT AddrBox() { return { CX + 150, DY + 84, CX + CW - 110, DY + 124 }; }
static RECT JoinBtn() { return { CX + CW - 100, DY + 90, CX + CW, DY + 119 }; }
static RECT SteamTab() { return { CX, DY + 166, CX + 150, DY + 192 }; }    // tabs when a server is set: Steam / yours
static RECT ServerTab() { return { CX + 156, DY + 166, CX + 306, DY + 192 }; }
static RECT SteamPub() { return { CX, DY + 198, CX + 160, DY + 227 }; }
static RECT SteamPriv() { return { CX + 170, DY + 198, CX + 330, DY + 227 }; }
static RECT SteamRefresh() { return { CX + CW - 160, DY + 198, CX + CW, DY + 227 }; }
static RECT SteamRow(int i) { return { CX, DY + 234 + i * 26, CX + CW, DY + 258 + i * 26 }; }
// in the lobby
static RECT LeaveBtn() { return { CX, DY + 80, CX + 120, DY + 109 }; }
static RECT CopyBtn() { return { CX + 360, DY + 80, CX + 440, DY + 109 }; }
static RECT RoleBtn() { return { CX + CW - 220, DY + 124, CX + CW, DY + 153 }; }
static RECT AvatarBtn() { return { CX + CW - 220, DY + 196, CX + CW, DY + 225 }; }
static RECT VersusBtn() { return { CX + CW - 220, DY + 232, CX + CW, DY + 261 }; }
static RECT WalletBtn() { return { CX, DY + 258, CX + 150, DY + 287 }; }
static RECT ScaleBtn() { return { CX, DY + 298, CX + 150, DY + 327 }; }
static RECT ModeBtn() { return { CX + 160, DY + 298, CX + 330, DY + 327 }; }
static RECT TankBtn() { return { CX + 336, DY + 298, CX + 410, DY + 327 }; }
static RECT StartBtn() { return { CX + CW - 84, DY + 298, CX + CW, DY + 327 }; }
static RECT ServerBtn() { return { CX + CW - 160, DY + 166, CX + CW, DY + 192 }; }   // Online tab: the server settings
static RECT SrvHostBox() { return { CX + 80, DY + 196, CX + CW, DY + 228 }; }
static RECT SrvPortBox() { return { CX + 80, DY + 236, CX + 180, DY + 268 }; }
static RECT SrvTlsBtn() { return { CX + 200, DY + 238, CX + 330, DY + 266 }; }
static RECT SrvKeyBox() { return { CX + 80, DY + 276, CX + CW, DY + 308 }; }
static RECT SrvPublicBtn() { return { CX, DY + 318, CX + 150, DY + 347 }; }
static RECT SrvTestBtn() { return { CX + 160, DY + 318, CX + 330, DY + 347 }; }
static RECT SrvDoneBtn() { return { CX + CW - 110, DY + 318, CX + CW, DY + 347 }; }
static bool steamListed, serverListed, serverTab;
static std::string ShownCode()
{
    if (net == NetSteam) return steam::CurrentLobby() ? steam::Code(steam::CurrentLobby()) : "";
    return net == NetServer ? online::Code() : "";
}

// the clipboard: the Steam code goes there; Ctrl+V pastes into the address box
static void CopyText(const std::string& t)
{
    void* a = api->app();
    if (!a || !OpenClipboard(at<HWND>(a, 0x350))) return;
    EmptyClipboard();
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, t.size() + 1))
    {
        memcpy(GlobalLock(h), t.c_str(), t.size() + 1);
        GlobalUnlock(h);
        SetClipboardData(CF_TEXT, h);
    }
    CloseClipboard();
}
static std::string PasteText()
{
    std::string r;
    void* a = api->app();
    if (!a || !OpenClipboard(at<HWND>(a, 0x350))) return r;
    if (HANDLE h = GetClipboardData(CF_TEXT)) { if (const char* p = (const char*)GlobalLock(h)) { r = p; GlobalUnlock(h); } }
    CloseClipboard();
    std::string clean;
    for (char c : r) if (isalnum((unsigned char)c) || c == '.' || c == ':' || c == '-') clean += c;
    return clean.substr(0, 60);
}

static const char* ModeName(int m) { return m == 0 ? "Adventure" : m == 1 ? "Time Trial" : "Challenge"; }

// avatar a, animated, fitted into a size x size box at (x, y)
static void DrawAvatar(void* g, int a, int x, int y, int size)
{
    a = ClampAvatar(a);
    if (a == 0 || api->version < 7) return;
    void* img = *reinterpret_cast<void**>(AvatarImages[a]);
    if (!img) return;
    int rows = a == 1 ? 5 : 1, cw = at<int>(img, Image_mWidth) / 10, ch = at<int>(img, Image_mHeight) / rows;
    if (cw <= 0 || ch <= 0) return;
    int frame = (int)(GetTickCount() / 90) % 10;
    float scale = (float)size / cw;
    if (a == 1) { scale *= 1.8f; x -= size * 2 / 5; y -= size * 2 / 5; }   // the guppy's cel is mostly water
    api->draw_image_scaled(g, img, x, y, frame * cw, 0, cw, ch, scale);
}

static void OpenScreen()
{
    if (screen || ui::DialogCount(api) > 0) return;
    char a[128];
    api->config_string("coop", "address", "", a, sizeof a);
    address = a;
    serverTab = true;   // online games first (the mod's public server unless you set your own); Steam on the other tab
    serverView = false;
    if (ui::OpenDialog(api, DialogId, "CO-OP", "CLOSE", DX, DY, DW, DH)) screen = true;
}

static bool ServerSet() { return true; }   // there is always one: the public server by default
static bool UseServer() { return serverTab && ServerSet(); }

static void(__thiscall* oButton)(void*, int);
static void __fastcall ButtonHook(void* a, void*, int id)
{
    if (id == DialogId + 2000 || id == DialogId + 3000) { ui::KillDialog(api, DialogId); screen = false; editing = false; return; }
    oButton(a, id);
}

extern "C" __declspec(dllexport) void CoopOpen() { OpenScreen(); }   // from the main menu's Remastered page
extern "C" __declspec(dllexport) void RemodOpen() { OpenScreen(); }  // the Open button on the settings' Mods tab

static int Mouse(int x, int y, int button, int down)
{
    if (state == Playing || !screen) return 0;
    if (!ui::GetDialog(api, DialogId)) { screen = false; return 0; }
    if (y >= DY + DH - 70) return 0;
    if (!down || button != 0) return 1;
    editing = false; editField = nullptr;
    auto current = [] { std::string port = srvPort.empty() ? (srvTls ? "443" : "80") : srvPort; return ServerConf{ srvHost, port, srvKey, srvTls }; };
    if (state == Off && serverView && serverTab && y >= DY + 160)
    {   // the server settings
        if (ui::In(SrvHostBox(), x, y)) { editing = true; editField = &srvHost; }
        else if (ui::In(SrvPortBox(), x, y)) { editing = true; editField = &srvPort; }
        else if (ui::In(SrvKeyBox(), x, y)) { editing = true; editField = &srvKey; }
        else if (ui::In(SrvTlsBtn(), x, y)) srvTls = !srvTls;
        else if (ui::In(SrvPublicBtn(), x, y))
        {
            srvHost = PublicHost; srvPort = std::to_string(PublicPort); srvTls = true; srvKey = PublicKey;
            SaveServer(current()); srvTest = "Using the mod's public server";
        }
        else if (ui::In(SrvTestBtn(), x, y)) { SaveServer(current()); if (ServerUp()) { online::Test(); srvTest = "Testing..."; } else srvTest = status; }
        else if (ui::In(SrvDoneBtn(), x, y)) { SaveServer(current()); serverView = false; serverListed = false; status.clear(); }
        else if (ui::In(SteamTab(), x, y)) { SaveServer(current()); serverView = false; serverTab = false; }
        api->redraw();
        return 1;
    }
    if (state == Off)
    {
        if (ui::In(HostBtn(), x, y)) Host();
        else if (ui::In(JoinBtn(), x, y) && !address.empty()) Join(address);
        else if (ui::In(AddrBox(), x, y)) { editing = true; editField = &address; }
        else if (UseServer() && ui::In(ServerBtn(), x, y))
        {
            ServerConf c = LoadServer();
            srvHost = c.host; srvPort = c.port; srvKey = c.key; srvTls = c.tls; srvTest.clear();
            serverView = true;
        }
        else if (ServerSet() && ui::In(SteamTab(), x, y)) serverTab = false;
        else if (ServerSet() && ui::In(ServerTab(), x, y)) serverTab = true;
        else if (ui::In(SteamPub(), x, y)) { if (UseServer()) HostServer(true); else HostSteam(true); }
        else if (ui::In(SteamPriv(), x, y)) { if (UseServer()) HostServer(false); else HostSteam(false); }
        else if (ui::In(SteamRefresh(), x, y))
        {
            if (UseServer()) { if (ServerUp()) { online::Refresh(MatchTag()); serverListed = true; status = "Looking for games on " + online::ServerName() + "..."; } }
            else if (SteamUp()) { steam::Refresh(MatchTag()); steamListed = true; status = "Looking for Steam games..."; }
        }
        else if (UseServer())
        {
            for (int i = 0; i < (int)online::Lobbies().size() && i < 4; i++)
                if (serverListed && ui::In(SteamRow(i), x, y)) { JoinServer(online::Lobbies()[i].code); break; }
        }
        else
            for (int i = 0; i < (int)steam::Lobbies().size() && i < 4; i++)
                if (steamListed && ui::In(SteamRow(i), x, y)) { JoinSteam(steam::Lobbies()[i].id); break; }
    }
    else
    {
        if (ui::In(LeaveBtn(), x, y)) EndSession("You left the co-op game");
        else if (!ShownCode().empty() && ui::In(CopyBtn(), x, y)) { CopyText(ShownCode()); status = "Code copied: paste it to your friends"; }
        else if (ui::In(RoleBtn(), x, y))
        {
            int r2 = (roles[localSlot] + 1) % RoleCount;
            if (host) { roles[0] = r2; SendLobby(); }
            else { std::string m2; m2 += (char)r2; Send(peers[0], MRole, m2); roles[localSlot] = r2; }
        }
        else if (ui::In(AvatarBtn(), x, y))
        {
            int a = (MyAvatar() + 1) % AvatarCount;
            api->config_set("coop", "avatar", std::to_string(a).c_str());
            avatars[localSlot] = a;
            if (host) SendLobby(); else { std::string m2; m2 += (char)a; Send(peers[0], MAvatar, m2); }
        }
        else if (host && ui::In(VersusBtn(), x, y)) { versus = !versus; SendLobby(); }
        else if (host && ui::In(WalletBtn(), x, y)) { splitMoney = !splitMoney; SendLobby(); }
        else if (host && ui::In(ScaleBtn(), x, y)) scaling = !scaling;
        else if (host && ui::In(ModeBtn(), x, y)) pickMode = pickMode == 0 ? 1 : pickMode == 1 ? 4 : 0;
        else if (host && pickMode != 0 && ui::In(TankBtn(), x, y)) pickTank = pickTank % 4 + 1;
        else if (host && ui::In(StartBtn(), x, y))
        {
            int players = 1;
            for (int i = 1; i < MaxPlayers; i++) if (peers[i].used && !names[i].empty()) players++;
            if (players < 2) status = "Nobody has joined yet";
            else { screen = false; HostStart(pickMode, pickTank); }
        }
    }
    api->redraw();
    return 1;
}

static int Key(int vk, int down)
{
    if (!down || state == Playing) return 0;
    int k = api->config_int("coop", "open_key", VK_F7);   // the Keys tab of the settings
    if (k && vk == k && !screen && ui::OnMainMenu(api)) { OpenScreen(); return 1; }
    if (!screen || !editing) return 0;
    std::string& t = editField ? *editField : address;
    bool digitsOnly = &t == &srvPort;
    if (vk == 'V' && (GetKeyState(VK_CONTROL) & 0x8000))
    {
        for (char c : PasteText()) if (c > ' ' && c < 127 && (!digitsOnly || (c >= '0' && c <= '9'))) t += c;
    }
    else if (vk == VK_BACK) { if (!t.empty()) t.pop_back(); }
    else if (vk == VK_RETURN || vk == VK_TAB)
    {
        editing = false;
        if (&t == &address && vk == VK_RETURN && state == Off && !address.empty()) Join(address);
        if (vk == VK_TAB && serverView) { editing = true; editField = &t == &srvHost ? &srvPort : &t == &srvPort ? &srvKey : &srvHost; }
    }
    else if (vk >= '0' && vk <= '9') t += (char)vk;
    else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) t += (char)('0' + vk - VK_NUMPAD0);
    else if (digitsOnly) return 1;
    else if (vk >= 'A' && vk <= 'Z') t += (char)(vk - 'A' + 'a');
    else if (vk == VK_OEM_PERIOD || vk == VK_DECIMAL) t += '.';
    else if (vk == VK_OEM_1) t += ':';
    else if (vk == VK_OEM_MINUS) t += (GetKeyState(VK_SHIFT) & 0x8000) ? '_' : '-';
    else return 0;
    if (t.size() > (&t == &srvKey ? 128u : &t == &srvPort ? 5u : 60u)) t.resize(&t == &srvKey ? 128 : &t == &srvPort ? 5 : 60);
    api->redraw();
    return 1;
}

static void Overlay(void* g)
{
    void* f12 = ui::Font(FONT_JUNGLEFEVER12OUTLINE), *f10 = ui::Font(FONT_JUNGLEFEVER10OUTLINE);
    if (state == Playing)
    {
        // everyone else's pointer: a coloured marker and the name
        for (int i = 0; i < MaxPlayers; i++)
        {
            if (i == localSlot || names[i].empty()) continue;
            int x = lastX[i], y = lastY[i];
            api->fill_rect(g, x - 1, y - 6, 3, 13, 0xc0000000);
            api->fill_rect(g, x - 6, y - 1, 13, 3, 0xc0000000);
            api->fill_rect(g, x, y - 5, 1, 11, Colors[i]);
            api->fill_rect(g, x - 5, y, 11, 1, Colors[i]);
            api->draw_text_font(g, f10, (names[i] + (i == VersusSlot && Versus() ? " (aliens)" : "")).c_str(), x + 8, y + 16, Colors[i]);
            DrawAvatar(g, avatars[i], x + 6, y - 32, 28);
        }
        // split wallets: the others' money under your own
        if (Split())
        {
            int row = 0;
            for (int i = 0; i < MaxPlayers; i++)
            {
                if (i == localSlot || !HasWallet(i)) continue;
                std::string t = names[i] + " $" + std::to_string(WalletOf(i));
                int w = api->text_width_font(f10, t.c_str());
                api->fill_rect(g, 626 - w - 6, 80 + row * 16, w + 10, 16, 0x90000000);
                api->draw_text_font(g, f10, t.c_str(), 626 - w - 1, 93 + row * 16, Colors[i]);
                row++;
            }
        }
        // versus: the aliens' player sees their dash and the next wave
        if (Versus() && localSlot == VersusSlot)
            if (void* b = api->board())
            {
                std::string t;
                if (Count(b, Board_mAliens) > 0) t = vsCooldown == 0 ? "Aliens: steer with the pointer, click to dash (ready)" : "Aliens: dash recharging...";
                else t = "Next alien wave in " + std::to_string(std::max(0, at<int>(b, Board_mAlienTimer)) * 28 / 1000 + 1) + " s";
                int w = api->text_width_font(f10, t.c_str());
                api->fill_rect(g, 320 - w / 2 - 6, 44, w + 12, 18, 0xa0000000);
                api->draw_text_font(g, f10, t.c_str(), 320 - w / 2, 58, 0xffff9090);
            }
        DWORD now = GetTickCount();
        // pings: a growing square ring for 3 s
        for (auto it = marks.begin(); it != marks.end();)
        {
            DWORD age = now - it->at;
            if (age > 3000) { it = marks.erase(it); api->redraw(); continue; }
            int r2 = 8 + (int)(age / 60) % 24;
            unsigned c = Colors[it->slot];
            api->fill_rect(g, it->x - r2, it->y - r2, 2 * r2, 2, c); api->fill_rect(g, it->x - r2, it->y + r2, 2 * r2, 2, c);
            api->fill_rect(g, it->x - r2, it->y - r2, 2, 2 * r2, c); api->fill_rect(g, it->x + r2, it->y - r2, 2, 2 * r2 + 2, c);
            api->draw_text_font(g, f10, names[it->slot].c_str(), it->x + r2 + 4, it->y, c);
            ++it;
        }
        // SOS: starving fish (two players clicking one saves it)
        if (void* b = api->board())
            for (int l : { Board_mGuppies, Board_mBreeders, Board_mOscars, Board_mUltras })
                for (int i = 0; i < Count(b, l); i++)
                {
                    void* f = Item(b, l, i);
                    if (!Starving(f)) continue;
                    int cx = at<int>(f, Widget_mX) + at<int>(f, Widget_mWidth) / 2, cy = at<int>(f, Widget_mY) - 4;
                    const char* t = rescueFish == f ? "SOS 1/2" : "SOS";
                    api->draw_text_font(g, f10, t, cx - api->text_width_font(f10, t) / 2, cy, (now / 250) % 2 ? 0xffff4040 : 0xffffd0d0);
                }
        // chat
        int y = 446;
        if (chatting)
        {
            std::string t = "Say: " + chatBuf + ((now / 400) % 2 ? "_" : "");
            api->fill_rect(g, 6, y - 14, api->text_width_font(f10, t.c_str()) + 10, 18, 0xb0000000);
            api->draw_text_font(g, f10, t.c_str(), 11, y, 0xffffffff);
            y -= 20;
        }
        for (int i = (int)chat.size() - 1; i >= 0; i--)
        {
            if (now - chat[i].at > 10000 && !chatting) continue;
            api->fill_rect(g, 6, y - 14, api->text_width_font(f10, chat[i].text.c_str()) + 10, 18, 0x90000000);
            api->draw_text_font(g, f10, chat[i].text.c_str(), 11, y, chat[i].color);
            y -= 20;
        }
        return;
    }
    if (reopenScreen && state == Lobby && ui::OnMainMenu(api) && ui::DialogCount(api) == 0) { reopenScreen = false; OpenScreen(); }
    if (!screen) return;
    if (!ui::GetDialog(api, DialogId)) { screen = false; return; }
    using ui::Look;
    if (state == Off)
    {
        api->draw_text_font(g, f12, "Play by address", CX, DY + 78, ui::Yellow);
        ui::Button(api, g, HostBtn(), "Host a game", Look::Main);
        ui::EditBox(api, g, AddrBox(), address, editing, "address or room code");
        ui::Button(api, g, JoinBtn(), "Join", Look::Main, !address.empty());
        ui::FitText(api, g, f10, "Host's TCP port " + std::to_string(port) + " must be open: LAN, VPN or forwarding.", CX, DY + 140, CW, ui::Cream);
        ui::FitText(api, g, f10, "Room codes and Steam join here too. Same mods for all.", CX, DY + 156, CW, ui::Cream);
        bool srv = UseServer();
        ui::Button(api, g, SteamTab(), "Steam", Look::Center, true, !srv);
        ui::Button(api, g, ServerTab(), "Online", Look::Center, true, srv);
        if (ui::Hover(api, ServerTab())) ui::Tooltip(("Games through the server " + online::ServerName() + " (no ports to open). Change it in Server settings.").c_str());
        if (srv) ui::Button(api, g, ServerBtn(), serverView ? "Server settings" : "Server settings...", Look::Center, true, serverView);
        if (ui::Hover(api, SteamTab())) ui::Tooltip("Games through Steam: needs Steam running and a steam_api.dll (found by itself in most cases).");
        if (srv && serverView)
        {
            std::string r;
            if (online::TestResult(r)) srvTest = r;
            api->draw_text_font(g, f12, "Server", CX, DY + 218, ui::Cream);
            ui::EditBox(api, g, SrvHostBox(), srvHost, editing && editField == &srvHost, "the server's name or address");
            api->draw_text_font(g, f12, "Port", CX, DY + 258, ui::Cream);
            ui::EditBox(api, g, SrvPortBox(), srvPort, editing && editField == &srvPort, srvTls ? "443" : "80");
            ui::Button(api, g, SrvTlsBtn(), srvTls ? "TLS: on (https)" : "TLS: off (http)", Look::Center, true, srvTls);
            if (ui::Hover(api, SrvTlsBtn())) ui::Tooltip("On for servers on the internet. Off only for a server on your own network (plain http, port 7350).");
            api->draw_text_font(g, f12, "Key", CX, DY + 298, ui::Cream);
            ui::EditBox(api, g, SrvKeyBox(), srvKey, editing && editField == &srvKey, "the server key (not a password: every player has it)");
            ui::Button(api, g, SrvPublicBtn(), "Public server", Look::Center);
            if (ui::Hover(api, SrvPublicBtn())) ui::Tooltip("Back to the mod's public server (insanicoop.mychud.net).");
            ui::Button(api, g, SrvTestBtn(), "Test connection", Look::Center);
            ui::Button(api, g, SrvDoneBtn(), "Done", Look::Main);
            if (!srvTest.empty()) ui::FitText(api, g, f10, srvTest, CX, DY + 362, CW, srvTest.rfind("Connection OK", 0) == 0 ? 0xff9cf09c : ui::White);
            ui::DrawTooltip(api, g);
            return;
        }
        ui::Button(api, g, SteamPub(), "Host: listed", Look::Center);
        ui::Button(api, g, SteamPriv(), "Host: private", Look::Center);
        ui::Button(api, g, SteamRefresh(), "Find games", Look::Center);
        struct Row { std::string name, code; int members; };
        std::vector<Row> rows;
        bool listed = srv ? serverListed : steamListed;
        if (srv) for (auto& l : online::Lobbies()) rows.push_back({ l.name, l.code, l.members });
        else for (auto& l : steam::Lobbies()) rows.push_back({ l.name, l.code, l.members });
        if (listed)
        {
            if (rows.empty()) ui::FitText(api, g, f10, srv ? "No listed games on " + online::ServerName() + " right now (private ones join by code)."
                                                           : std::string("No listed games right now (private ones join by code)."), CX, DY + 252, CW, 0xffd0d0d0);
            for (int i = 0; i < (int)rows.size() && i < 4; i++)
            {
                RECT rr = SteamRow(i);
                bool over = ui::Hover(api, rr);
                if (over) api->fill_rect(g, rr.left, rr.top, ui::W(rr), ui::H(rr), 0x40ffffff);
                ui::FitText(api, g, f12, rows[i].name, rr.left + 8, rr.bottom - 7, CW - 170, over ? ui::White : ui::Cream);
                std::string t = std::to_string(rows[i].members) + "/4   " + rows[i].code;
                api->draw_text_font(g, f10, t.c_str(), rr.right - 8 - api->text_width_font(f10, t.c_str()), rr.bottom - 7, 0xff9cf09c);
            }
        }
    }
    else
    {
        ui::Button(api, g, LeaveBtn(), "Leave", Look::Center);
        std::string code = ShownCode();
        if (!code.empty())
        {
            api->draw_text_font(g, f12, ((net == NetSteam ? "Steam code: " : "Room code: ") + code).c_str(), CX + 136, DY + 100, 0xff9cf09c);
            if (net == NetServer)
            {
                std::string on = "on " + online::ServerName();
                ui::FitText(api, g, f10, on, CX + 136, DY + 116, 210, ui::Cream);
                if (ui::Hover(api, RECT{ CX + 136, DY + 86, CX + 346, DY + 120 })) ui::Tooltip("Room codes work on this server only: friends need the same server (Online, Server settings).");
            }
            ui::Button(api, g, CopyBtn(), "Copy", Look::Center);
        }
        api->draw_text_font(g, f12, "Players", CX, DY + 140, ui::Yellow);
        for (int i = 0; i < MaxPlayers; i++)
        {
            std::string n = names[i].empty() ? "-" : names[i] + (i == 0 ? "  (host)" : "") + (i == localSlot ? "  (you)" : "") +
                            (roles[i] ? std::string("  - ") + RoleNames[roles[i]] : "");
            if (versus && i == VersusSlot && !names[i].empty()) n += "  - the aliens";
            api->fill_rect(g, CX, DY + 152 + i * 26, 12, 12, names[i].empty() ? 0x40000000 : Colors[i]);
            if (!names[i].empty()) DrawAvatar(g, avatars[i], CX + 16, DY + 145 + i * 26, 24);
            ui::FitText(api, g, f12, n, CX + 46, DY + 164 + i * 26, CW - 276, ui::White);
        }
        ui::Button(api, g, RoleBtn(), (std::string("Your role: ") + RoleNames[roles[localSlot]]).c_str(), Look::Main);
        if (roles[localSlot]) ui::WrapText(api, g, f10, RoleHelp[roles[localSlot]], RoleBtn().left, RoleBtn().bottom + 14, ui::W(RoleBtn()), ui::Cream, 2);
        ui::Button(api, g, AvatarBtn(), (std::string("Avatar: ") + AvatarNames[MyAvatar()]).c_str(), Look::Main);
        if (ui::Hover(api, AvatarBtn())) ui::Tooltip("Shown on your pointer for the others. Click for the next one.");
        if (host)
        {
            ui::Button(api, g, VersusBtn(), versus ? "Versus: on" : "Versus: off", Look::Center, true, versus);
            if (ui::Hover(api, VersusBtn())) ui::Tooltip("The first player to join steers the aliens with the pointer (click to dash) and tries to eat every fish; the others defend the tank.");
        }
        else if (versus) ui::WrapText(api, g, f10, localSlot == VersusSlot ? "Versus: you steer the aliens! Eat every fish." : "Versus: " + names[VersusSlot] + " steers the aliens.", VersusBtn().left, VersusBtn().top + 14, ui::W(VersusBtn()), 0xffff9090, 2);
        if (host)
        {
            ui::Button(api, g, WalletBtn(), splitMoney ? "Money: split" : "Money: shared", Look::Center, true, splitMoney);
            if (ui::Hover(api, WalletBtn())) ui::Tooltip("Split: every player has their own money. Coins pay whoever clicked them, pets' pickups are shared out, and you buy with your own.");
        }
        else if (splitMoney) ui::FitText(api, g, f10, "Money: everyone has their own", CX, WalletBtn().top + 18, ui::W(WalletBtn()) + 60, ui::Cream);
        if (host)
        {
            ui::Button(api, g, ScaleBtn(), scaling ? "Scaling: on" : "Scaling: off", Look::Center, true, scaling);
            if (ui::Hover(api, ScaleBtn())) ui::Tooltip("Tougher tanks for more players: prices +25% and aliens +50% for each player after the first.");
            ui::Button(api, g, ModeBtn(), ModeName(pickMode), Look::Center);
            if (pickMode != 0) ui::Button(api, g, TankBtn(), ("Tank " + std::to_string(pickTank)).c_str(), Look::Center);
            ui::Button(api, g, StartBtn(), "Start", Look::Main);
        }
        else ui::FitText(api, g, f10, "The host picks the game and starts it.", CX, DY + 314, CW, ui::Cream);
    }
    ui::FitText(api, g, f10, status, CX, DY + DH - 82, CW, ui::White);
    ui::DrawTooltip(api, g);
}

extern "C" __declspec(dllexport) const char* RemodDescribe() { return "Co-op for 2-4 players online (no setup), over Steam or by address; avatars, versus, split money. F7."; }

extern "C" __declspec(dllexport) int RemodInit(const RemodApi* a)
{
    if (a->version < 5) return 0;
    api = a;
    port = api->config_int("coop", "port", 27615);
    resyncTest = api->config_int("coop", "resync_test", 0);   // testing: the host resyncs every N ticks
    bool ok = api->hook((void*)App_UpdateFrames, (void*)&UpdateFrames, (void**)&oUpdateFrames)
           && api->hook((void*)Board_Pause, (void*)&PauseHook, (void**)&oPause)
           && api->hook((void*)App_LostFocus, (void*)&LostFocus, (void**)&oLostFocus)
           && api->hook((void*)App_SaveProfile, (void*)&SaveProfile, (void**)&oSaveProfile)
           && api->hook((void*)Board_SaveOrDeleteGame, (void*)&SaveGame, (void**)&oSaveGame)
           && api->hook((void*)App_ShowGameSelector, (void*)&Selector, (void**)&oSelector)
           && api->hook((void*)App_ButtonDepress, (void*)&ButtonHook, (void**)&oButton)
           && api->hook((void*)Board_SpendMoney, (void*)&SpendMoney, (void**)&oSpendMoney)
           && api->hook((void*)Alien_Shoot, (void*)&Shoot, (void**)&oShoot)
           && api->hook((void*)Coin_MouseDown, (void*)&CoinMouseDown, (void**)&oCoinMouseDown)
           && api->hook((void*)Board_BuyItem, (void*)&BuyItem, (void**)&oBuyItem)
           && api->hook((void*)Widget_MouseDown, (void*)&WidgetMouseDown, (void**)&oWidgetMouseDown)
           && api->hook((void*)GameObject_PetSleepy, (void*)&PetSleepy, (void**)&oPetSleepy)
           && api->hook((void*)MTRand_Next, (void*)&MTNext, (void**)&oMTNext)
           && api->hook((void*)Shot_ctorKind, (void*)&ShotCtor, (void**)&oShotCtor)
           && api->hook((void*)Board_InitLevel, (void*)&InitLevel, (void**)&oInitLevel)
           && api->hook((void*)Board_AddMoney, (void*)&AddMoney, (void**)&oAddMoney)
           && api->hook((void*)Coin_ReceiveMoney, (void*)&ReceiveMoney, (void**)&oReceiveMoney)
           && api->hook((void*)GameObject_RemoveFromGame, (void*)&RemoveFromGame, (void**)&oRemoveFromGame)
           && api->hook((void*)Board_UpdateMoneyLabel, (void*)&MoneyLabel, (void**)&oMoneyLabel)
           && api->hook((void*)App_WriteBytesToFile, (void*)&WriteBytes, (void**)&oWriteBytes)
           && api->hook((void*)App_ReadBufferFromFile, (void*)&ReadBuffer, (void**)&oReadBuffer)
           && api->hook((void*)App_DoContinueDialog, (void*)&ContinueDialog, (void**)&oContinueDialog)
           && api->hook((void*)Board_BankCoins, (void*)&Bank, (void**)&oBank)
           && api->hook((void*)Alien_Think, (void*)&Think, (void**)&oThink)
           && api->hook((void*)App_StartGame, (void*)&StartGame, (void**)&oStartGame)
           && api->hook((void*)App_StartBoard, (void*)&StartBoard, (void**)&oStartBoard);
    if (!ok) return 0;
    api->on_mouse(Mouse);
    api->on_key(Key);
    api->on_overlay(Overlay);
    return 1;
}
