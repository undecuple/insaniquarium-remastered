// Steam for co-op: see steam.h. The flat API (steam_api_flat.h of the Steamworks SDK) with manual callback dispatch,
// so no C++ interfaces or Steam headers are needed. Structures below are Steam's, packed to 8 on Windows.
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <fstream>
#include <iterator>
#include "steam.h"

namespace steam {
namespace {

typedef int32_t HSteamPipe;
typedef uint64_t SteamAPICall;
#pragma pack(push, 8)
struct CallbackMsg { int32_t user; int32_t callback; uint8_t* param; int32_t size; };
struct CallCompleted { SteamAPICall call; int32_t callback; uint32_t size; };
struct LobbyCreated { int32_t result; uint64_t lobby; };
struct LobbyMatchList { uint32_t count; };
struct LobbyEnter { uint64_t lobby; uint32_t perms; bool locked; uint32_t response; };
struct LobbyChatUpdate { uint64_t lobby, changed, by; uint32_t state; };
struct Identity { int32_t type; int32_t size; uint8_t data[128]; };   // SteamNetworkingIdentity (136 bytes)
struct SessionRequest { Identity remote; };
#pragma pack(pop)
enum { CbCallCompleted = 703, CbLobbyCreated = 513, CbLobbyMatchList = 510, CbLobbyEnter = 504, CbLobbyChatUpdate = 506, CbSessionRequest = 1251 };
const int AppId = 480, Reliable = 8;

HMODULE dll;
void (*logf)(const char*);
void* utils;
void Log(const char* fmt, ...)
{
    if (!logf) return;
    char b[300]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
    logf(b);
}
bool running;
Events ev;
HSteamPipe pipe;
void *mm, *net, *user, *friends;
uint64_t lobby;
std::vector<Lobby> lobbies;
SteamAPICall createCall, listCall, joinCall;
std::string hostName, hostTag;
bool hostListed;

#define F(ret, name, ...) typedef ret(__cdecl* name##_t)(__VA_ARGS__); name##_t name;
F(int, SteamAPI_InitFlat, char*)
F(bool, SteamAPI_Init)
F(HSteamPipe, SteamAPI_GetHSteamPipe)
F(void, SteamAPI_ManualDispatch_Init)
F(void, SteamAPI_ManualDispatch_RunFrame, HSteamPipe)
F(bool, SteamAPI_ManualDispatch_GetNextCallback, HSteamPipe, CallbackMsg*)
F(void, SteamAPI_ManualDispatch_FreeLastCallback, HSteamPipe)
F(bool, SteamAPI_ManualDispatch_GetAPICallResult, HSteamPipe, SteamAPICall, void*, int, int, bool*)
F(void*, SteamAPI_SteamMatchmaking_v009)
F(void*, SteamAPI_SteamNetworkingMessages_SteamAPI_v002)
F(void*, SteamAPI_SteamUser_v023)
F(void*, SteamAPI_SteamFriends_v018)
F(SteamAPICall, SteamAPI_ISteamMatchmaking_CreateLobby, void*, int, int)
F(SteamAPICall, SteamAPI_ISteamMatchmaking_RequestLobbyList, void*)
F(void, SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter, void*, const char*, const char*, int)
F(void, SteamAPI_ISteamMatchmaking_AddRequestLobbyListResultCountFilter, void*, int)
F(uint64_t, SteamAPI_ISteamMatchmaking_GetLobbyByIndex, void*, int)
F(const char*, SteamAPI_ISteamMatchmaking_GetLobbyData, void*, uint64_t, const char*)
F(bool, SteamAPI_ISteamMatchmaking_SetLobbyData, void*, uint64_t, const char*, const char*)
F(SteamAPICall, SteamAPI_ISteamMatchmaking_JoinLobby, void*, uint64_t)
F(void, SteamAPI_ISteamMatchmaking_LeaveLobby, void*, uint64_t)
F(uint64_t, SteamAPI_ISteamMatchmaking_GetLobbyOwner, void*, uint64_t)
F(int, SteamAPI_ISteamMatchmaking_GetNumLobbyMembers, void*, uint64_t)
F(bool, SteamAPI_ISteamMatchmaking_SetLobbyJoinable, void*, uint64_t, bool)
F(uint64_t, SteamAPI_ISteamUser_GetSteamID, void*)
F(const char*, SteamAPI_ISteamFriends_GetPersonaName, void*)
F(int, SteamAPI_ISteamNetworkingMessages_SendMessageToUser, void*, const Identity*, const void*, uint32_t, int, int)
F(int, SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel, void*, int, void**, int)
F(bool, SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser, void*, const Identity*)
F(void, SteamAPI_SteamNetworkingIdentity_SetSteamID, Identity*, uint64_t)
F(uint64_t, SteamAPI_SteamNetworkingIdentity_GetSteamID, const Identity*)
F(void, SteamAPI_SteamNetworkingMessage_t_Release, void*)
F(void*, SteamAPI_SteamUtils_v010)
F(uint32_t, SteamAPI_ISteamUtils_GetAppID, void*)
F(int, SteamAPI_ISteamUtils_GetAPICallFailureReason, void*, SteamAPICall)
F(bool, SteamAPI_ISteamUser_BLoggedOn, void*)
#undef F

template <typename T> bool Load(T& f, const char* name) { f = reinterpret_cast<T>(GetProcAddress(dll, name)); return f != nullptr; }
// an interface accessor under any of the names below (older SDKs have older versions with the same functions we use)
template <typename T> bool LoadAny(T& f, std::initializer_list<const char*> names)
{
    for (const char* n : names) if (Load(f, n)) return true;
    return false;
}
const std::initializer_list<const char*> UserNames = { "SteamAPI_SteamUser_v023", "SteamAPI_SteamUser_v022", "SteamAPI_SteamUser_v021", "SteamAPI_SteamUser_v020" };
const std::initializer_list<const char*> FriendsNames = { "SteamAPI_SteamFriends_v018", "SteamAPI_SteamFriends_v017" };

// ---- finding a steam_api.dll: ours in mods\coop\, else a copy another Steam game already has on this computer ----
std::string loadedFrom;

// the export names of a 32-bit DLL, read from the file (nothing is loaded); empty if it isn't one
std::vector<std::string> Exports32(const std::string& path)
{
    std::vector<std::string> out;
    std::ifstream f(path, std::ios::binary);
    if (!f) return out;
    std::string d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto u16 = [&](size_t o) { return o + 2 <= d.size() ? *(const uint16_t*)&d[o] : 0; };
    auto u32 = [&](size_t o) { return o + 4 <= d.size() ? *(const uint32_t*)&d[o] : 0; };
    if (u16(0) != 0x5a4d) return out;
    size_t pe = u32(0x3c);
    if (u32(pe) != 0x4550 || u16(pe + 4) != 0x14c || u16(pe + 24) != 0x10b) return out;   // PE, i386, PE32
    size_t sections = pe + 24 + u16(pe + 20);
    int count = u16(pe + 6);
    auto off = [&](uint32_t rva) -> size_t {
        for (int i = 0; i < count; i++)
        {
            size_t s = sections + i * 40;
            uint32_t vs = u32(s + 8), va = u32(s + 12), rs = u32(s + 16), ro = u32(s + 20);
            if (rva >= va && rva < va + (vs > rs ? vs : rs)) return rva - va + ro;
        }
        return 0;
    };
    size_t e = off(u32(pe + 24 + 96));
    if (!e) return out;
    uint32_t n = u32(e + 24);
    size_t names = off(u32(e + 32));
    for (uint32_t i = 0; i < n && names && i < 5000; i++)
    {
        size_t r = off(u32(names + 4 * i));
        if (!r || r >= d.size()) break;
        out.emplace_back(d.c_str() + r, strnlen(d.c_str() + r, d.size() - r));
    }
    return out;
}

// how good a candidate is: 0 = unusable (not 32-bit, or an SDK older than the flat API with interface versions, 2020),
// otherwise its export count (newer SDKs have more)
int Score(const std::string& path)
{
    std::vector<std::string> ex = Exports32(path);
    auto has = [&](const char* n) { for (auto& e : ex) if (e == n) return true; return false; };
    auto any = [&](std::initializer_list<const char*> ns) { for (const char* n : ns) if (has(n)) return true; return false; };
    bool ok = (has("SteamAPI_InitFlat") || has("SteamAPI_Init")) && has("SteamAPI_ManualDispatch_Init")
        && has("SteamAPI_SteamMatchmaking_v009") && has("SteamAPI_SteamNetworkingMessages_SteamAPI_v002")
        && has("SteamAPI_SteamNetworkingIdentity_SetSteamID") && any(UserNames) && any(FriendsNames);
    return ok ? (int)ex.size() : 0;
}

bool IsFile(const std::string& p) { DWORD a = GetFileAttributesA(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
bool IsDir(const std::string& p) { DWORD a = GetFileAttributesA(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
// a Linux path from Steam's environment under Proton/Wine -> the same folder through Wine's Z: drive
std::string WinPath(std::string p)
{
    if (!p.empty() && p[0] == '/') { for (char& c : p) if (c == '/') c = '\\'; p = "Z:" + p; }
    while (!p.empty() && (p.back() == '\\' || p.back() == '/')) p.pop_back();
    return p;
}

// every steamapps\common folder we can find: the one this game is in, Steam's own and its other libraries
std::vector<std::string> CommonFolders(const std::string& modsDir)
{
    std::vector<std::string> libs, out;
    // the Steam release: ...\steamapps\common\Insaniquarium Deluxe\mods
    std::string low = modsDir;
    for (char& c : low) c = (char)tolower((unsigned char)c);
    size_t at = low.rfind("\\steamapps\\common\\");
    if (at != std::string::npos) libs.push_back(modsDir.substr(0, at));
    char buf[MAX_PATH];
    DWORD size = sizeof buf;
    if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath", RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS)
        libs.push_back(WinPath(buf));
    for (const char* var : { "STEAM_COMPAT_CLIENT_INSTALL_PATH" })   // set by Steam for games run through Proton
        if (GetEnvironmentVariableA(var, buf, sizeof buf)) libs.push_back(WinPath(buf));
    // the other libraries, from libraryfolders.vdf: "path"  "D:\\SteamLibrary"
    for (size_t i = 0; i < libs.size() && i < 32; i++)
    {
        std::ifstream f(libs[i] + "\\steamapps\\libraryfolders.vdf");
        std::string line;
        while (std::getline(f, line))
        {
            size_t k = line.find("\"path\"");
            if (k == std::string::npos) continue;
            size_t a = line.find('"', k + 6), b = a == std::string::npos ? a : line.find('"', a + 1);
            if (b == std::string::npos) continue;
            std::string v = line.substr(a + 1, b - a - 1), u;
            for (size_t j = 0; j < v.size(); j++) { if (v[j] == '\\' && j + 1 < v.size() && v[j + 1] == '\\') j++; u += v[j]; }
            libs.push_back(WinPath(u));
        }
    }
    for (auto& l : libs)
    {
        std::string c = l + "\\steamapps\\common", lc = c;
        for (char& ch : lc) ch = (char)tolower((unsigned char)ch);
        bool dup = false;
        for (auto& o : out) { std::string lo = o; for (char& ch : lo) ch = (char)tolower((unsigned char)ch); dup |= lo == lc; }
        if (!dup && IsDir(c)) out.push_back(c);
    }
    return out;
}

// steam_api.dll files under dir, at most depth folders down (Unity games keep it in <Game>_Data\Plugins\x86)
void Find(const std::string& dir, int depth, std::vector<std::string>& found, int& budget)
{
    if (--budget < 0) return;
    if (IsFile(dir + "\\steam_api.dll")) found.push_back(dir + "\\steam_api.dll");
    if (depth == 0) return;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileExA((dir + "\\*").c_str(), FindExInfoBasic, &fd, FindExSearchLimitToDirectories, nullptr, 2 /* FIND_FIRST_EX_LARGE_FETCH */);
    if (h == INVALID_HANDLE_VALUE) return;
    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        Find(dir + "\\" + fd.cFileName, depth - 1, found, budget);
    } while (budget > 0 && FindNextFileA(h, &fd));
    FindClose(h);
}

// the best usable steam_api.dll in the Steam libraries, or "" (a one-time search; the caller remembers the result)
std::string Search(const std::string& modsDir)
{
    DWORD t0 = GetTickCount();
    std::vector<std::string> found;
    int budget = 40000;   // folders: enough for big libraries, bounded for huge ones
    std::vector<std::string> roots = CommonFolders(modsDir);
    for (auto& c : roots) Find(c, 5, found, budget);
    std::string best;
    int bestScore = 0;
    for (auto& f : found)
    {
        int sc = Score(f);
        if (sc > bestScore) { bestScore = sc; best = f; }
    }
    Log("steam: searched %d Steam librar%s in %lu ms: %d steam_api.dll, best: %s", (int)roots.size(), roots.size() == 1 ? "y" : "ies",
        GetTickCount() - t0, (int)found.size(), best.empty() ? "none usable" : best.c_str());
    return best;
}

Identity Id(uint64_t sid) { Identity i{}; SteamAPI_SteamNetworkingIdentity_SetSteamID(&i, sid); return i; }

const char Digits[] = "0123456789abcdefghjkmnpqrstuvwxyz";   // 33 symbols, no i, l or o

}  // namespace

bool Start(const Events& events, std::string& error, const std::string& hint)
{
    ev = events;
    if (running) return true;
    if (!dll)
    {
        // mods\coop\steam_api.dll (not directly in mods\: the loader would take it for a mod), else wherever Windows finds it
        char path[MAX_PATH];
        HMODULE self;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&Start, &self);
        GetModuleFileNameA(self, path, MAX_PATH);
        std::string mods = path;
        mods = mods.substr(0, mods.find_last_of("\\/"));
        // ours first, then the copy found last time, then a search of the Steam libraries (each checked before loading)
        std::string p = mods + "\\coop\\steam_api.dll";
        if (!IsFile(p)) p = !hint.empty() && IsFile(hint) && Score(hint) ? hint : Search(mods);
        if (!p.empty()) dll = LoadLibraryExA(p.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!dll) { error = "no steam_api.dll found: see the README (Co-op over Steam)"; return false; }
        loadedFrom = p;
        Log("steam: using %s", p.c_str());
    }
    Load(SteamAPI_InitFlat, "SteamAPI_InitFlat");   // SDK 1.58+; older ones have SteamAPI_Init
    bool ok = (SteamAPI_InitFlat || Load(SteamAPI_Init, "SteamAPI_Init")) && Load(SteamAPI_GetHSteamPipe, "SteamAPI_GetHSteamPipe")
        && Load(SteamAPI_ManualDispatch_Init, "SteamAPI_ManualDispatch_Init") && Load(SteamAPI_ManualDispatch_RunFrame, "SteamAPI_ManualDispatch_RunFrame")
        && Load(SteamAPI_ManualDispatch_GetNextCallback, "SteamAPI_ManualDispatch_GetNextCallback")
        && Load(SteamAPI_ManualDispatch_FreeLastCallback, "SteamAPI_ManualDispatch_FreeLastCallback")
        && Load(SteamAPI_ManualDispatch_GetAPICallResult, "SteamAPI_ManualDispatch_GetAPICallResult")
        && Load(SteamAPI_SteamMatchmaking_v009, "SteamAPI_SteamMatchmaking_v009")
        && Load(SteamAPI_SteamNetworkingMessages_SteamAPI_v002, "SteamAPI_SteamNetworkingMessages_SteamAPI_v002")
        && LoadAny(SteamAPI_SteamUser_v023, UserNames) && LoadAny(SteamAPI_SteamFriends_v018, FriendsNames)
        && Load(SteamAPI_ISteamMatchmaking_CreateLobby, "SteamAPI_ISteamMatchmaking_CreateLobby")
        && Load(SteamAPI_ISteamMatchmaking_RequestLobbyList, "SteamAPI_ISteamMatchmaking_RequestLobbyList")
        && Load(SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter, "SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter")
        && Load(SteamAPI_ISteamMatchmaking_AddRequestLobbyListResultCountFilter, "SteamAPI_ISteamMatchmaking_AddRequestLobbyListResultCountFilter")
        && Load(SteamAPI_ISteamMatchmaking_GetLobbyByIndex, "SteamAPI_ISteamMatchmaking_GetLobbyByIndex")
        && Load(SteamAPI_ISteamMatchmaking_GetLobbyData, "SteamAPI_ISteamMatchmaking_GetLobbyData")
        && Load(SteamAPI_ISteamMatchmaking_SetLobbyData, "SteamAPI_ISteamMatchmaking_SetLobbyData")
        && Load(SteamAPI_ISteamMatchmaking_JoinLobby, "SteamAPI_ISteamMatchmaking_JoinLobby")
        && Load(SteamAPI_ISteamMatchmaking_LeaveLobby, "SteamAPI_ISteamMatchmaking_LeaveLobby")
        && Load(SteamAPI_ISteamMatchmaking_GetLobbyOwner, "SteamAPI_ISteamMatchmaking_GetLobbyOwner")
        && Load(SteamAPI_ISteamMatchmaking_GetNumLobbyMembers, "SteamAPI_ISteamMatchmaking_GetNumLobbyMembers")
        && Load(SteamAPI_ISteamMatchmaking_SetLobbyJoinable, "SteamAPI_ISteamMatchmaking_SetLobbyJoinable")
        && Load(SteamAPI_ISteamUser_GetSteamID, "SteamAPI_ISteamUser_GetSteamID")
        && Load(SteamAPI_ISteamFriends_GetPersonaName, "SteamAPI_ISteamFriends_GetPersonaName")
        && Load(SteamAPI_ISteamNetworkingMessages_SendMessageToUser, "SteamAPI_ISteamNetworkingMessages_SendMessageToUser")
        && Load(SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel, "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel")
        && Load(SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser, "SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser")
        && Load(SteamAPI_SteamNetworkingIdentity_SetSteamID, "SteamAPI_SteamNetworkingIdentity_SetSteamID")
        && Load(SteamAPI_SteamNetworkingIdentity_GetSteamID, "SteamAPI_SteamNetworkingIdentity_GetSteamID")
        && Load(SteamAPI_SteamNetworkingMessage_t_Release, "SteamAPI_SteamNetworkingMessage_t_Release");
    if (!ok) { error = "steam_api.dll is too old"; return false; }
    Load(SteamAPI_SteamUtils_v010, "SteamAPI_SteamUtils_v010"); Load(SteamAPI_ISteamUtils_GetAppID, "SteamAPI_ISteamUtils_GetAppID");
    Load(SteamAPI_ISteamUtils_GetAPICallFailureReason, "SteamAPI_ISteamUtils_GetAPICallFailureReason");
    Load(SteamAPI_ISteamUser_BLoggedOn, "SteamAPI_ISteamUser_BLoggedOn");
    // App ID 480: the Steam release starts the game as 3320, which can't make lobbies for us
    char id[16];
    snprintf(id, sizeof id, "%d", AppId);
    SetEnvironmentVariableA("SteamAppId", id);
    SetEnvironmentVariableA("SteamGameId", id);
    char err[1024] = "";
    if (SteamAPI_InitFlat ? SteamAPI_InitFlat(err) != 0 : !SteamAPI_Init()) { error = err[0] ? err : "Steam isn't running"; return false; }
    SteamAPI_ManualDispatch_Init();
    pipe = SteamAPI_GetHSteamPipe();
    mm = SteamAPI_SteamMatchmaking_v009();
    net = SteamAPI_SteamNetworkingMessages_SteamAPI_v002();
    user = SteamAPI_SteamUser_v023();
    friends = SteamAPI_SteamFriends_v018();
    running = mm && net && user;
    utils = SteamAPI_SteamUtils_v010 ? SteamAPI_SteamUtils_v010() : nullptr;
    Log("steam: started, app %u, logged on %d, user %llu", utils && SteamAPI_ISteamUtils_GetAppID ? SteamAPI_ISteamUtils_GetAppID(utils) : 0,
        SteamAPI_ISteamUser_BLoggedOn ? (int)SteamAPI_ISteamUser_BLoggedOn(user) : -1, (unsigned long long)SteamAPI_ISteamUser_GetSteamID(user));
    if (!running) error = "Steam's interfaces aren't available";
    return running;
}

bool Running() { return running; }
std::string LoadedFrom() { return loadedFrom; }
void SetLog(void (*log)(const char* line)) { logf = log; }
uint64_t Me() { return running ? SteamAPI_ISteamUser_GetSteamID(user) : 0; }
std::string MyName() { const char* n = running && friends ? SteamAPI_ISteamFriends_GetPersonaName(friends) : nullptr; return n ? n : "Player"; }

void Frame()
{
    if (!running) return;
    SteamAPI_ManualDispatch_RunFrame(pipe);
    CallbackMsg cb;
    while (SteamAPI_ManualDispatch_GetNextCallback(pipe, &cb))
    {
        if (cb.callback == CbCallCompleted)
        {
            CallCompleted* c = reinterpret_cast<CallCompleted*>(cb.param);
            if (c->call != createCall && c->call != listCall && c->call != joinCall) { SteamAPI_ManualDispatch_FreeLastCallback(pipe); continue; }   // Steam's own
            uint8_t buf[256];
            bool failed = false;
            if (c->size <= sizeof buf && SteamAPI_ManualDispatch_GetAPICallResult(pipe, c->call, buf, (int)c->size, c->callback, &failed) && !failed)
            {
                if (c->call == createCall && c->callback == CbLobbyCreated)
                {
                    auto* r = reinterpret_cast<LobbyCreated*>(buf);
                    if (r->result == 1)
                    {
                        lobby = r->lobby;
                        SteamAPI_ISteamMatchmaking_SetLobbyData(mm, lobby, "game", hostTag.c_str());
                        SteamAPI_ISteamMatchmaking_SetLobbyData(mm, lobby, "name", hostName.c_str());
                        SteamAPI_ISteamMatchmaking_SetLobbyData(mm, lobby, "visibility", hostListed ? "public" : "private");
                        if (ev.created) ev.created(lobby);
                    }
                    else if (ev.failed) ev.failed("Steam couldn't make the lobby");
                }
                else if (c->call == listCall && c->callback == CbLobbyMatchList)
                {
                    lobbies.clear();
                    uint32_t n = reinterpret_cast<LobbyMatchList*>(buf)->count;
                    for (uint32_t i = 0; i < n && i < 50; i++)
                    {
                        uint64_t l = SteamAPI_ISteamMatchmaking_GetLobbyByIndex(mm, (int)i);
                        const char* name = SteamAPI_ISteamMatchmaking_GetLobbyData(mm, l, "name");
                        lobbies.push_back({ l, name && *name ? name : "A game", Code(l), SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(mm, l) });
                    }
                    if (ev.listed) ev.listed();
                }
                else if (c->call == joinCall && c->callback == CbLobbyEnter)
                {
                    auto* r = reinterpret_cast<LobbyEnter*>(buf);
                    if (r->response == 1) { lobby = r->lobby; if (ev.entered) ev.entered(SteamAPI_ISteamMatchmaking_GetLobbyOwner(mm, lobby)); }
                    else if (ev.failed) ev.failed(r->response == 4 ? "That game is full" : "Couldn't join that game (it may be gone)");
                }
            }
            else
            {
                int reason = utils && SteamAPI_ISteamUtils_GetAPICallFailureReason ? SteamAPI_ISteamUtils_GetAPICallFailureReason(utils, c->call) : -2;
                Log("steam: call %llu (callback %d, %u bytes) failed: reason %d", (unsigned long long)c->call, c->callback, c->size, reason);
                if (ev.failed) ev.failed("Steam didn't answer");
            }
        }
        else if (cb.callback == CbLobbyChatUpdate)
        {
            auto* u = reinterpret_cast<LobbyChatUpdate*>(cb.param);
            if (u->lobby == lobby && (u->state & (2 | 4 | 8 | 16)) && ev.left) ev.left(u->changed);   // left, disconnected, kicked, banned
        }
        else if (cb.callback == CbSessionRequest)
        {
            // a player of our lobby wants to talk: accept (lobby members only)
            auto* s = reinterpret_cast<SessionRequest*>(cb.param);
            SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser(net, &s->remote);
        }
        SteamAPI_ManualDispatch_FreeLastCallback(pipe);
    }
}

void Host(bool listed, const std::string& name, const std::string& tag)
{
    if (!running) return;
    hostName = name; hostTag = tag; hostListed = listed;   // set on the lobby when it exists (Frame)
    createCall = SteamAPI_ISteamMatchmaking_CreateLobby(mm, 2, 4);   // a public lobby (private games are hidden by a tag)
}

void Refresh(const std::string& tag)
{
    if (!running) return;
    SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter(mm, "game", tag.c_str(), 0);   // equal
    SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter(mm, "visibility", "public", 0);
    SteamAPI_ISteamMatchmaking_AddRequestLobbyListResultCountFilter(mm, 20);
    listCall = SteamAPI_ISteamMatchmaking_RequestLobbyList(mm);
}

const std::vector<Lobby>& Lobbies() { return lobbies; }
void Join(uint64_t l) { if (running) joinCall = SteamAPI_ISteamMatchmaking_JoinLobby(mm, l); }
void Leave() { if (running && lobby) SteamAPI_ISteamMatchmaking_LeaveLobby(mm, lobby); lobby = 0; }
uint64_t CurrentLobby() { return lobby; }
void SetJoinable(bool on) { if (running && lobby) SteamAPI_ISteamMatchmaking_SetLobbyJoinable(mm, lobby, on); }

// a lobby's code: "S" and its account number in base 33 (the rest of a lobby's Steam ID is always the same)
std::string Code(uint64_t l)
{
    uint32_t a = (uint32_t)l;
    std::string s;
    do { s = Digits[a % 33] + s; a /= 33; } while (a);
    return "S" + s;
}
uint64_t FromCode(const std::string& code)
{
    if (code.size() < 2 || (code[0] != 's' && code[0] != 'S')) return 0;
    uint64_t a = 0;
    for (size_t i = 1; i < code.size(); i++)
    {
        char c = (char)tolower((unsigned char)code[i]);
        const char* p = strchr(Digits, c);
        if (!p || !c) return 0;
        a = a * 33 + (uint64_t)(p - Digits);
        if (a > 0xffffffffULL) return 0;
    }
    return (1ULL << 56) | (7ULL << 52) | (0x40000ULL << 32) | a;   // universe public, type chat, instance "lobby"
}

bool Send(uint64_t to, const std::string& data)
{
    if (!running) return false;
    Identity id = Id(to);
    return SteamAPI_ISteamNetworkingMessages_SendMessageToUser(net, &id, data.data(), (uint32_t)data.size(), Reliable, 0) == 1;
}

bool Receive(uint64_t& from, std::string& data)
{
    if (!running) return false;
    void* msg = nullptr;
    if (SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel(net, 0, &msg, 1) < 1 || !msg) return false;
    // SteamNetworkingMessage_t: m_pData, m_cbSize, m_conn, m_identityPeer...
    uint8_t* m = static_cast<uint8_t*>(msg);
    const void* p = *reinterpret_cast<void**>(m);
    int size = *reinterpret_cast<int*>(m + 4);
    from = SteamAPI_SteamNetworkingIdentity_GetSteamID(reinterpret_cast<Identity*>(m + 12));
    data.assign(static_cast<const char*>(p), size);
    SteamAPI_SteamNetworkingMessage_t_Release(msg);
    return true;
}
}  // namespace steam
