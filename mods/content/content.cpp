// content: content packs, folders in mods\ (any name; several are applied in name order, later ones win):
//   mods\<pack>\assets\...              replaces the game's own files: images\, sounds\, music\, properties\... (same
//                                       relative paths and names; the game's files are never changed)
//   mods\<pack>\content\levels.json     changes what levels start with: store, prices, aliens, money... (format below)
// levels.json (comments and trailing commas allowed):
//   { "levels": [ { "tank": 1, "levels": [2, 3], "modes": ["adventure"], "money": 500, "foodPrice": 10,
//                   "alienDelay": 2000, "backdrop": 2, "eggPieces": 2, "startGuppies": 4, "startBreeders": 0,
//                   "aliens": ["sylvester", "balrog"], "unlockAll": false,
//                   "store": { "carnivore": { "slot": 2, "price": 800, "unlocked": true }, "starpotion": null } } ] }
// Store items: guppy breeder foodquality foodquantity carnivore starpotion starcatcher guppycruncher beetlemuncher
// ultravore weapon egg. Aliens: littlesylvester sylvester balrog gus destructor ulysses psychosquid bilaterus (or 1-12).
// Never applied to the Virtual Tank, the sandbox, the screensaver or the Extra Modes.
#include "remod.h"
#include "game.h"
#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <fstream>
#include <sstream>
#include <algorithm>

using namespace game;
static const RemodApi* api;
static std::string gameDir, modsDir;
static std::vector<std::string> assetDirs;   // mods\<pack>\assets, in order (the last one wins)

// ---- a small JSON reader ----------------------------------------------------------------------------------------------
struct Json
{
    enum Kind { Null, Bool, Num, Str, Arr, Obj } kind = Null;
    double num = 0; bool b = false; std::string str;
    std::vector<Json> arr; std::vector<std::pair<std::string, Json>> obj;
    const Json* get(const char* k) const { for (auto& p : obj) if (p.first == k) return &p.second; return nullptr; }
};
struct Parser
{
    const char* p; std::string err;
    void ws()
    {
        for (;;)
        {
            while (*p && isspace((unsigned char)*p)) p++;
            if (p[0] == '/' && p[1] == '/') { while (*p && *p != '\n') p++; continue; }
            if (p[0] == '/' && p[1] == '*') { p += 2; while (*p && !(p[0] == '*' && p[1] == '/')) p++; if (*p) p += 2; continue; }
            return;
        }
    }
    bool value(Json& j)
    {
        ws();
        if (*p == '{')
        {
            p++; j.kind = Json::Obj;
            for (;;)
            {
                ws();
                if (*p == '}') { p++; return true; }
                Json k;
                if (*p != '"' || !value(k)) { err = "expected a name in quotes"; return false; }
                ws();
                if (*p != ':') { err = "expected ':'"; return false; }
                p++;
                Json v;
                if (!value(v)) return false;
                j.obj.push_back({ k.str, v });
                ws();
                if (*p == ',') p++;
                else if (*p != '}') { err = "expected ',' or '}'"; return false; }
            }
        }
        if (*p == '[')
        {
            p++; j.kind = Json::Arr;
            for (;;)
            {
                ws();
                if (*p == ']') { p++; return true; }
                Json v;
                if (!value(v)) return false;
                j.arr.push_back(v);
                ws();
                if (*p == ',') p++;
                else if (*p != ']') { err = "expected ',' or ']'"; return false; }
            }
        }
        if (*p == '"')
        {
            p++; j.kind = Json::Str;
            while (*p && *p != '"') { if (*p == '\\' && p[1]) p++; j.str += *p++; }
            if (*p != '"') { err = "unfinished text"; return false; }
            p++; return true;
        }
        if (!strncmp(p, "true", 4)) { p += 4; j.kind = Json::Bool; j.b = true; return true; }
        if (!strncmp(p, "false", 5)) { p += 5; j.kind = Json::Bool; return true; }
        if (!strncmp(p, "null", 4)) { p += 4; return true; }
        char* end;
        j.num = strtod(p, &end);
        if (end == p) { err = "unexpected character"; return false; }
        p = end; j.kind = Json::Num; return true;
    }
};

// ---- level definitions ------------------------------------------------------------------------------------------------
struct StoreDef { int item; bool remove = false, unlocked = false; int slot = -1, price = -1, step = -1; };
struct LevelDef
{
    int tank = 0; std::vector<int> levels, modes, aliens;
    int money = -1, foodPrice = -1, alienDelay = -1, backdrop = -1, eggPieces = -1, startGuppies = -1, startBreeders = -1;
    bool unlockAll = false; std::vector<StoreDef> store;
};
static std::vector<LevelDef> defs;
static const char* ItemNames[] = { "guppy", "breeder", "foodquality", "foodquantity", "carnivore", "starpotion",
                                   "starcatcher", "guppycruncher", "beetlemuncher", "ultravore", "weapon", "egg" };
static std::string Key(std::string s)
{
    std::string r;
    for (char c : s) if (c != '_' && c != '-' && c != ' ') r += (char)tolower((unsigned char)c);
    return r;
}
static int AlienOf(const Json& j)
{
    if (j.kind == Json::Num) return (int)j.num;
    static const std::map<std::string, int> names = { { "none", 0 }, { "littlesylvester", 1 }, { "sylvester", 2 }, { "balrog", 3 }, { "gus", 4 },
        { "destructor", 5 }, { "ulysses", 6 }, { "psychosquid", 7 }, { "bilaterus", 8 }, { "littlesylvester+balrog", 9 },
        { "psychosquid+balrog", 10 }, { "destructor+ulysses", 11 }, { "balrog+bilaterus", 12 } };
    auto it = names.find(Key(j.str));
    if (it == names.end()) throw std::string("unknown alien '" + j.str + "'");
    return it->second;
}
static std::vector<int> Ints(const Json& j, int (*one)(const Json&))
{
    std::vector<int> r;
    if (j.kind == Json::Arr) for (auto& x : j.arr) r.push_back(one(x)); else r.push_back(one(j));
    return r;
}
static int Num(const Json& j) { if (j.kind != Json::Num) throw std::string("a number was expected"); return (int)j.num; }
static int ModeOf(const Json& j)
{
    std::string k = Key(j.str);
    if (k == "adventure") return 0;
    if (k == "timetrial") return 1;
    if (k == "challenge") return 4;
    throw std::string("unknown mode '" + j.str + "'");
}

static void LoadPack(const std::string& name, const std::string& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) return;
    std::stringstream ss; ss << in.rdbuf();
    std::string text = ss.str();
    Parser ps{ text.c_str(), "" };
    Json root;
    if (!ps.value(root)) { api->log("content: %s\\content\\levels.json: %s (at character %d)", name.c_str(), ps.err.c_str(), (int)(ps.p - text.c_str())); return; }
    std::vector<LevelDef> mine;   // all or nothing: a bad entry skips the whole file
    try
    {
        const Json* levels = root.get("levels");
        if (!levels || levels->kind != Json::Arr) throw std::string("\"levels\": [ ... ] is missing");
        for (auto& o : levels->arr)
        {
            LevelDef d;
            const Json* v = o.get("tank");
            if (!v || v->kind != Json::Num) throw std::string("\"tank\" (1-5) is required");
            d.tank = (int)v->num;
            if ((v = o.get("levels"))) d.levels = Ints(*v, Num);
            if ((v = o.get("modes"))) d.modes = Ints(*v, ModeOf);
            if ((v = o.get("aliens"))) d.aliens = Ints(*v, AlienOf);
            auto opt = [&](const char* k, int& out) { if (const Json* x = o.get(k)) out = Num(*x); };
            opt("money", d.money); opt("foodPrice", d.foodPrice); opt("alienDelay", d.alienDelay); opt("backdrop", d.backdrop);
            opt("eggPieces", d.eggPieces); opt("startGuppies", d.startGuppies); opt("startBreeders", d.startBreeders);
            if ((v = o.get("unlockAll"))) d.unlockAll = v->kind == Json::Bool && v->b;
            if ((v = o.get("store")))
                for (auto& p : v->obj)
                {
                    StoreDef s;
                    s.item = -1;
                    for (int i = 0; i < 12; i++) if (Key(p.first) == ItemNames[i]) s.item = i;
                    if (s.item < 0) throw std::string("unknown store item '" + p.first + "'");
                    if (p.second.kind == Json::Null || (p.second.kind == Json::Bool && !p.second.b)) s.remove = true;
                    else
                    {
                        if (const Json* x = p.second.get("slot")) { s.slot = Num(*x); if (s.slot < 0 || s.slot > 6) throw std::string(p.first + ": slot must be 0-6"); }
                        if (const Json* x = p.second.get("price")) s.price = Num(*x);
                        if (const Json* x = p.second.get("priceStep")) s.step = Num(*x);
                        if (const Json* x = p.second.get("unlocked")) s.unlocked = x->kind == Json::Bool && x->b;
                    }
                    d.store.push_back(s);
                }
            mine.push_back(d);
        }
    }
    catch (const std::string& e) { api->log("content: %s\\content\\levels.json: %s", name.c_str(), e.c_str()); return; }
    defs.insert(defs.end(), mine.begin(), mine.end());
    api->log("content: %s: %d level definition(s)", name.c_str(), (int)mine.size());
}

static bool SpecialMode()
{
    void* a = api->app();
    int mode = at<int>(a, App_mGameMode);
    if (mode == 3 || mode == 5 || at<bool>(a, App_mIsScreenSaver)) return true;
    for (const char* dll : { "extramodes.dll", "alienplay.dll" })
        if (HMODULE m = GetModuleHandleA(dll))
            if (auto f = reinterpret_cast<int (*)()>(GetProcAddress(m, "ModeActive"))) if (f()) return true;
    return false;
}

static std::vector<const LevelDef*> For(void* b)
{
    std::vector<const LevelDef*> r;
    if (defs.empty() || SpecialMode()) return r;
    int mode = at<int>(api->app(), App_mGameMode), tank = at<int>(b, Board_mTank), level = at<int>(b, 0x3cc);
    for (auto& d : defs)
        if (d.tank == tank && (d.levels.empty() || std::count(d.levels.begin(), d.levels.end(), level))
            && (d.modes.empty() ? (mode == 0 || mode == 1 || mode == 4) : std::count(d.modes.begin(), d.modes.end(), mode) > 0))
            r.push_back(&d);
    return r;
}

static int NextAlien(void* b)
{
    const LevelDef* pick = nullptr;
    for (auto d : For(b)) if (!d->aliens.empty()) pick = d;
    return pick ? pick->aliens[rand() % pick->aliens.size()] : -1;
}

typedef void(__thiscall* VoidFn)(void*);
static VoidFn oSetupLevel, oStartLevel;
static int lastWaves;

// after the level's own setup: store, prices, money, aliens, backdrop
static void __fastcall SetupLevel(void* b, void*)
{
    oSetupLevel(b);
    for (auto d : For(b))
    {
        if (d->money >= 0) { at<int>(b, Board_mMoney) = std::min(d->money, 999999); reinterpret_cast<VoidFn>(Board_UpdateMoneyLabel)(b); }
        if (d->foodPrice >= 0) at<int>(b, 0x4ac) = d->foodPrice;          // mFoodPrice
        if (d->alienDelay >= 0) at<int>(b, Board_mAlienTimer) = d->alienDelay;
        if (d->backdrop >= 0) reinterpret_cast<void(__thiscall*)(void*, int)>(Board_SetBackdrop)(b, d->backdrop);
        if (d->eggPieces >= 0) at<int>(b, 0x43c) = 4 - std::min(std::max(d->eggPieces, 1), 3);   // mEggPieces counts up to 4
        int* slot = &at<int>(b, Board_mStoreSlot), *price = &at<int>(b, Board_mPrice), *step = &at<int>(b, 0x374);   // mPriceStep
        for (auto& s : d->store)
        {
            if (s.remove) { slot[s.item] = -1; continue; }
            if (s.slot >= 0) { for (int i = 0; i < 12; i++) if (slot[i] == s.slot) slot[i] = -1; slot[s.item] = s.slot; }
            if (s.price >= 0) price[s.item] = std::min(s.price, 99999);
            if (s.step >= 0) step[s.item] = s.step;
        }
    }
    int first = NextAlien(b);
    if (first >= 0) at<int>(b, Board_mAlienType) = first;
    lastWaves = at<int>(b, Board_mAlienWaves);
}

// after the starting fish: the definition's own starting fish, and the buttons it opens from the start
static void __fastcall StartLevel(void* b, void*)
{
    oStartLevel(b);
    auto mine = For(b);
    int guppies = -1, breeders = -1;
    for (auto d : mine) if (d->startGuppies >= 0 || d->startBreeders >= 0) { guppies = std::max(d->startGuppies, 0); breeders = std::max(d->startBreeders, 0); }
    if (guppies >= 0)
    {
        for (int l : { Board_mGuppies, Board_mBreeders })
            while (Count(b, l)) reinterpret_cast<void(__thiscall*)(void*, bool)>(GameObject_RemoveFromGame)(Item(b, l, Count(b, l) - 1), true);
        for (int i = 0; i < std::min(guppies, 30); i++) reinterpret_cast<void*(__thiscall*)(void*, int, int)>(Board_AddGuppyAt)(b, 40 + rand() % 500, 110 + rand() % 200);
        for (int i = 0; i < std::min(breeders, 30); i++) reinterpret_cast<void*(__thiscall*)(void*, int, int)>(Board_AddBreederAt)(b, 40 + rand() % 500, 110 + rand() % 200);
    }
    for (auto d : mine)
        for (int i = 0; i < 12; i++)
        {
            bool open = d->unlockAll;
            for (auto& s : d->store) if (s.item == i && !s.remove && s.unlocked) open = true;
            if (open) reinterpret_cast<void(__thiscall*)(void*, int, bool)>(Board_UnlockStoreItem)(b, i, false);
        }
}

// after each wave: the next alien from the definition's list
static void Tick(void* b)
{
    int waves = at<int>(b, Board_mAlienWaves);
    if (waves == lastWaves) return;
    lastWaves = waves;
    int next = NextAlien(b);
    if (next >= 0) at<int>(b, Board_mAlienType) = next;
}

// ---- asset overlays: file opens of game files go to a pack's copy when there is one --------------------------------------
static std::string Redirect(const char* path)
{
    if (!path || assetDirs.empty()) return "";
    std::string p = path;
    for (auto& c : p) if (c == '/') c = '\\';
    std::string rel;
    if (p.size() > 2 && p[1] == ':')   // absolute: only inside the game folder
    {
        if (_strnicmp(p.c_str(), gameDir.c_str(), gameDir.size()) != 0 || p.size() <= gameDir.size() || p[gameDir.size()] != '\\') return "";
        rel = p.substr(gameDir.size() + 1);
    }
    else if (p.size() > 2 && p[0] == '\\') return "";
    else rel = p.compare(0, 2, ".\\") == 0 ? p.substr(2) : p;
    if (_strnicmp(rel.c_str(), "mods\\", 5) == 0) return "";
    for (auto it = assetDirs.rbegin(); it != assetDirs.rend(); ++it)
    {
        std::string candidate = *it + "\\" + rel;
        if (GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) return candidate;
    }
    return "";
}

typedef HANDLE(WINAPI* CreateFileAFn)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef DWORD(WINAPI* GetAttrFn)(LPCSTR);
static CreateFileAFn oCreateFileA;
static GetAttrFn oGetFileAttributesA;
static thread_local bool inHook;

static HANDLE WINAPI MyCreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl)
{
    if (!inHook && !(access & GENERIC_WRITE) && disp == OPEN_EXISTING)
    {
        inHook = true;
        std::string r = Redirect(name);
        inHook = false;
        if (!r.empty()) return oCreateFileA(r.c_str(), access, share, sa, disp, flags, tmpl);
    }
    return oCreateFileA(name, access, share, sa, disp, flags, tmpl);
}
static DWORD WINAPI MyGetFileAttributesA(LPCSTR name)
{
    if (!inHook)
    {
        inHook = true;
        std::string r = Redirect(name);
        inHook = false;
        if (!r.empty()) return oGetFileAttributesA(r.c_str());
    }
    return oGetFileAttributesA(name);
}

// for co-op: everyone needs the same level definitions (asset files only change looks)
static std::string fingerprint = "none";
extern "C" __declspec(dllexport) const char* ContentFingerprint() { return fingerprint.c_str(); }

extern "C" __declspec(dllexport) const char* RemodDescribe() { return "Content packs: mods\\<pack>\\assets replaces game files, content\\levels.json changes levels."; }

extern "C" __declspec(dllexport) int RemodInit(const RemodApi* a)
{
    if (a->version < 4) return 0;
    api = a;
    char p[MAX_PATH];
    HMODULE self;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&RemodInit, &self);
    GetModuleFileNameA(self, p, MAX_PATH);
    modsDir = p;
    modsDir = modsDir.substr(0, modsDir.find_last_of("\\/"));
    gameDir = modsDir.substr(0, modsDir.find_last_of("\\/"));
    // the packs: folders in mods\ (name order)
    std::vector<std::string> packs;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((modsDir + "\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != '.') packs.push_back(fd.cFileName); while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    std::sort(packs.begin(), packs.end());
    for (auto& pk : packs)
    {
        if (!api->config_int("mods", pk.c_str(), 1)) { api->log("content: %s: switched off", pk.c_str()); continue; }
        std::string assets = modsDir + "\\" + pk + "\\assets";
        if (GetFileAttributesA(assets.c_str()) & FILE_ATTRIBUTE_DIRECTORY && GetFileAttributesA(assets.c_str()) != INVALID_FILE_ATTRIBUTES)
        { assetDirs.push_back(assets); api->log("content: %s: asset overlay", pk.c_str()); }
        LoadPack(pk, modsDir + "\\" + pk + "\\content\\levels.json");
        std::ifstream lj(modsDir + "\\" + pk + "\\content\\levels.json", std::ios::binary);
        if (lj)
        {
            std::stringstream ss; ss << lj.rdbuf();
            uint64_t h = 0xcbf29ce484222325ULL;
            for (char c : pk + ss.str()) { h ^= (unsigned char)c; h *= 0x100000001b3ULL; }
            char t[32]; snprintf(t, sizeof t, "%016llx", (unsigned long long)h);
            fingerprint = fingerprint == "none" ? t : fingerprint + "+" + t;
        }
    }
    if (!assetDirs.empty())
    {
        HMODULE k32 = GetModuleHandleA("kernel32.dll");
        if (!api->hook((void*)GetProcAddress(k32, "CreateFileA"), (void*)&MyCreateFileA, (void**)&oCreateFileA)
            || !api->hook((void*)GetProcAddress(k32, "GetFileAttributesA"), (void*)&MyGetFileAttributesA, (void**)&oGetFileAttributesA))
            api->log("content: couldn't hook file opening: asset overlays are off");
    }
    if (!defs.empty())
    {
        if (!api->hook((void*)Board_SetupLevel, (void*)&SetupLevel, (void**)&oSetupLevel)
            || !api->hook((void*)Board_StartLevel, (void*)&StartLevel, (void**)&oStartLevel)) return 0;
        api->on_tick(Tick);
    }
    return 1;
}
