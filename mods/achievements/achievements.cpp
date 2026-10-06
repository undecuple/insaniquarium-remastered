// achievements: 31 achievements per player profile, a banner when one unlocks, shell rewards (paid on the main menu),
// and a list (the "Achievements" button on the main menu, or F4). Progress is kept next to the game's profiles as
// userdata\achievements<n>.txt (one line per unlocked achievement, counters as name=value); the game's own files are
// never touched. Other mods unlock theirs with the exported AchievementUnlock("id").
#include "remod.h"
#include "game.h"
#include "remodui.h"
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <fstream>
#include <algorithm>

using namespace game;
static const RemodApi* api;

struct Def { const char* id; const char* name; const char* desc; const char* counter; int goal; int shells; };
static const Def All[] = {
    { "first_coin", "Pocket Money", "Collect your first coin.", nullptr, 1, 50 },
    { "coins_1000", "Coin Collector", "Collect 1,000 coins.", "coins", 1000, 500 },
    { "coins_10000", "Coin Hoarder", "Collect 10,000 coins.", "coins", 10000, 2000 },
    { "treasure", "Treasure Hunter", "Collect a treasure chest.", nullptr, 1, 200 },
    { "diamond", "Shine On", "Collect a diamond.", nullptr, 1, 200 },
    { "grow_large", "Growing Up", "Grow a guppy to full size.", nullptr, 1, 100 },
    { "crowned", "Long Live the King", "Feed a guppy until it wears a crown.", nullptr, 1, 300 },
    { "egg_piece", "Egg-cellent", "Buy an egg piece.", nullptr, 1, 100 },
    { "level_1", "Hatchling", "Finish a level.", nullptr, 1, 100 },
    { "no_loss", "Untouchable", "Finish a level without losing a single fish.", nullptr, 1, 500 },
    { "no_pets", "Lone Swimmer", "Finish a level with no pets in the tank.", nullptr, 1, 500 },
    { "tank_2", "Deeper Waters", "Finish tank 2 in Adventure.", nullptr, 1, 1000 },
    { "tank_4", "Almost There", "Finish tank 4 in Adventure.", nullptr, 1, 2000 },
    { "beat_game", "Insaniquarium!", "Beat Adventure mode.", nullptr, 1, 5000 },
    { "alien_1", "Close Encounter", "Defeat an alien.", nullptr, 1, 150 },
    { "alien_100", "Exterminator", "Defeat 100 aliens.", "aliens", 100, 1500 },
    { "low_tech", "Pea Shooter", "Defeat an alien with the starting laser.", nullptr, 1, 750 },
    { "pets_12", "Pet Lover", "Own 12 pets.", nullptr, 1, 1000 },
    { "pets_24", "Full House", "Own all 24 pets.", nullptr, 1, 3000 },
    { "shells_1000", "Shell Shocked", "Have 1,000 shells.", nullptr, 1, 250 },
    { "vt_buy", "Window Shopper", "Buy something in the Virtual Tank store.", nullptr, 1, 100 },
    { "coop_level", "Better Together", "Finish a level in co-op.", nullptr, 1, 500 },
    { "coop_full", "Full Tank", "Play co-op with 4 players.", nullptr, 1, 1000 },
    { "daily", "Daily Diver", "Finish a daily challenge.", nullptr, 1, 500 },
    { "bossrush", "Boss Basher", "Clear the boss rush.", nullptr, 1, 2500 },
    { "alienplay", "Role Reversal", "Clear a stage as the alien.", nullptr, 1, 1000 },
    { "alienplay_all", "Conqueror", "Clear all 20 stages as the alien.", nullptr, 1, 5000 },
    { "teamwork", "Teamwork", "Rescue a starving fish together in co-op.", nullptr, 1, 500 },
    { "endless_10", "Holding the Line", "Survive 10 waves in Endless.", nullptr, 1, 1000 },
    { "endless_25", "Unbreakable", "Survive 25 waves in Endless.", nullptr, 1, 3000 },
    { "mutated", "Mutant", "Finish a level with three or more hard mutators.", nullptr, 1, 1500 },
};
static const int Count_ = sizeof All / sizeof All[0];

static std::set<std::string> unlocked;
static std::map<std::string, long long> counters;
static std::string file, userdata;
static bool dirty, banners = true;
static std::vector<const Def*> queue;
static DWORD bannerUntil;
static const Def* banner;

// ---- storage --------------------------------------------------------------------------------------------------------
static void Save()
{
    if (!dirty || file.empty()) return;
    CreateDirectoryA(userdata.c_str(), nullptr);
    std::ofstream f(file, std::ios::trunc);
    for (auto& u : unlocked) f << u << "\n";
    for (auto& c : counters) f << c.first << "=" << c.second << "\n";
    dirty = false;
}

static void* Profile() { void* a = api->app(); return a ? at<void*>(a, App_mProfile) : nullptr; }

// the progress file of the current profile (reloaded when the player changes)
static void Sync()
{
    void* p = Profile();
    std::string f = userdata + (p ? "\\achievements" + std::to_string(at<int>(p, 0x40)) + ".txt" : "\\achievements_guest.txt");   // +0x40 mUserIndex
    if (f == file) return;
    Save();
    file = f;
    unlocked.clear(); counters.clear();
    std::ifstream in(f);
    std::string line;
    while (std::getline(in, line))
    {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) unlocked.insert(line);
        else counters[line.substr(0, eq)] = atoll(line.c_str() + eq + 1);
    }
}

static bool Allowed()
{
    void* a = api->app();
    return a && !at<bool>(a, App_mIsScreenSaver) && at<int>(a, App_mGameMode) != 3;   // not the screensaver or the sandbox
}

// Play as the Alien: the computer keeper's kills and coins aren't the player's (only that mode's own achievements count)
static bool AlienPlaying()
{
    HMODULE m = GetModuleHandleA("alienplay.dll");
    auto f = m ? reinterpret_cast<int (*)()>(GetProcAddress(m, "AlienPlayActive")) : nullptr;
    return f && f();
}

static void Unlock(const char* id)
{
    if (!Allowed() || (AlienPlaying() && strncmp(id, "alienplay", 9) != 0)) return;
    Sync();
    const Def* d = nullptr;
    for (auto& x : All) if (!strcmp(x.id, id)) d = &x;
    if (!d || !unlocked.insert(id).second) return;
    counters["pending_shells"] += d->shells;
    dirty = true;
    Save();
    api->log("achievements: %s (+%d shells)", d->name, d->shells);
    if (banners) queue.push_back(d);
}

static void Add(const char* counter, long long n = 1)
{
    if (!Allowed() || AlienPlaying()) return;
    Sync();
    long long v = counters[counter] += n;
    dirty = true;
    for (auto& d : All) if (d.counter && !strcmp(d.counter, counter) && v >= d.goal) Unlock(d.id);
}

extern "C" __declspec(dllexport) void AchievementUnlock(const char* id) { Unlock(id); }

static void CheckProfile()
{
    void* p = Profile();
    if (!p) return;
    int owned = 0;
    for (int i = 0; i < 24; i++) if (at<bool>(p, i)) owned++;   // +0x00 bool[24]: pets owned
    if (owned >= 12) Unlock("pets_12");
    if (owned >= 24) Unlock("pets_24");
    if (at<int>(p, Profile_mShells) >= 1000) Unlock("shells_1000");
    if (at<bool>(p, 0x59)) Unlock("beat_game");   // mFinishedGame
}

// ---- hooks ----------------------------------------------------------------------------------------------------------
static bool fishLost;
static int hardMutators;

typedef void(__thiscall* VoidFn)(void*);
static VoidFn oInitLevel, oBank;
static void(__thiscall* oCoinMouseDown)(void*, int, int, int);
static void(__thiscall* oAlienDie)(void*, bool);
static void(__thiscall* oFishEat)(void*, void*);
static void(__thiscall* oBuyItem)(void*, int);
static void(__thiscall* oAddDeadFish)(void*, int, int, double, double, double, int, bool, void*);
static void*(__thiscall* oPurchase)(void*);

static void __fastcall InitLevel(void* b, void*)
{
    oInitLevel(b);
    fishLost = false;
    hardMutators = 0;
    if (api->config_int("mods", "mutators", 1))
    {
        const char* hard[] = { "hungry", "double", "nopets", "pacifist", "heavycoins", "tinywallet", "hard", "overeat", "decay" };
        for (auto k : hard) if (api->config_int("mutators", k, 0)) hardMutators++;
    }
}

static void __fastcall CoinMouseDown(void* c, void*, int x, int y, int clicks)
{
    bool was = at<bool>(c, Coin_mCollected);
    oCoinMouseDown(c, x, y, clicks);
    if (was || !at<bool>(c, Coin_mCollected)) return;
    int t = at<int>(c, Coin_mCoinType);
    if (t >= 0xf && t != 0x11 && t != 0x12) return;
    Unlock("first_coin");
    Add("coins");
    if (t == 7 || t == 0xe) Unlock("treasure");
    if (t == 4 || t == 5 || t == 0xb || t == 0xc) Unlock("diamond");
}

static void __fastcall AlienDie(void* a, void*, bool killed)
{
    void* b = api->board();
    int weapon = b ? at<int>(b, Board_mWeaponLevel) : 99;
    oAlienDie(a, killed);
    if (!killed) return;
    Unlock("alien_1");
    Add("aliens");
    if (weapon <= 2) Unlock("low_tech");
}

static void __fastcall FishEat(void* f, void*, void* food)
{
    int before = at<int>(f, 0x1a0);   // mSize: 0 small, 1 medium, 2 large, 4 crowned
    oFishEat(f, food);
    int after = at<int>(f, 0x1a0);
    if (after == 2 && before == 1) Unlock("grow_large");
    if (after == 4 && before != 4) Unlock("crowned");
}

static void __fastcall BuyItem(void* b, void*, int item)
{
    int eggs = at<int>(b, 0x43c);   // mEggPieces
    oBuyItem(b, item);
    void* now = api->board();
    if (item == 0xb && now == b && at<int>(b, 0x43c) > eggs) Unlock("egg_piece");
}

static void __fastcall AddDeadFish(void* b, void*, int x, int y, double vx, double vy, double speed, int size, bool right, void* shadow)
{
    oAddDeadFish(b, x, y, vx, vy, speed, size, right, shadow);
    fishLost = true;
}

// the level is won: the board banks the coins still flying to the counter (also at a game over, then with < 4 egg pieces)
static void __fastcall Bank(void* b, void*)
{
    if (at<int>(b, 0x43c) >= 4 && Allowed())
    {
        if (at<int>(b, 0x43c) == 4) Unlock("egg_piece");
        Unlock("level_1");
        if (hardMutators >= 3) Unlock("mutated");
        if (!fishLost) Unlock("no_loss");
        if (Count(b, Board_mFishPets) + Count(b, 0xb0) == 0) Unlock("no_pets");   // + mOtherPets
        if (at<int>(api->app(), App_mGameMode) == 0 && at<int>(b, 0x3cc) >= 5)   // Adventure, level 5 of the tank
        {
            if (at<int>(b, Board_mTank) == 2) Unlock("tank_2");
            if (at<int>(b, Board_mTank) == 4) Unlock("tank_4");
        }
        CheckProfile();
    }
    oBank(b);
}

static void* __fastcall Purchase(void* store, void*)
{
    void* r = oPurchase(store);
    Unlock("vt_buy");
    return r;
}

// ---- the list ---------------------------------------------------------------------------------------------------------
static const int DialogId = 0x49, PerPage = 5;
static const int DX = 30, DY = 20, DW = 580, DH = 440, CX = DX + 40, CW = DW - 80;
static bool open;
static int page;
static RECT Prev() { return { CX, DY + DH - 110, CX + 110, DY + DH - 81 }; }
static RECT Next() { return { CX + CW - 110, DY + DH - 110, CX + CW, DY + DH - 81 }; }
static int Pages() { return (Count_ + PerPage - 1) / PerPage; }

static void Open()
{
    if (open || ui::DialogCount(api) > 0) return;
    Sync();
    if (ui::OpenDialog(api, DialogId, "ACHIEVEMENTS", "CLOSE", DX, DY, DW, DH)) open = true;
}

static void(__thiscall* oButton)(void*, int);
static void __fastcall ButtonHook(void* app, void*, int id)
{
    if (id == DialogId + 2000 || id == DialogId + 3000) { ui::KillDialog(api, DialogId); open = false; return; }
    oButton(app, id);
}

static int Mouse(int x, int y, int button, int down)
{
    if (!open)
    {
        return 0;
    }
    if (!ui::GetDialog(api, DialogId)) { open = false; return 0; }
    if (y >= DY + DH - 70) return 0;   // CLOSE
    if (down && button == 0)
    {
        if (ui::In(Prev(), x, y) && page > 0) page--;
        else if (ui::In(Next(), x, y) && page < Pages() - 1) page++;
        api->redraw();
    }
    return 1;
}

static int Key(int vk, int down)
{
    if (down && vk == VK_F4 && !open && !ui::CoopPlaying()) { Open(); return 1; }
    return 0;
}

static void DrawList(void* g)
{
    void* f12 = ui::Font(FONT_JUNGLEFEVER12OUTLINE), *f10 = ui::Font(FONT_JUNGLEFEVER10OUTLINE);
    char s[160];
    int got = 0;
    for (auto& d : All) if (unlocked.count(d.id)) got++;
    long long pending = counters.count("pending_shells") ? counters["pending_shells"] : 0;
    snprintf(s, sizeof s, "%d of %d unlocked", got, Count_);
    api->draw_text_font(g, f12, s, CX, DY + 80, 0xffffffff);
    if (pending > 0)
    {
        snprintf(s, sizeof s, "%lld shells waiting (paid on the main menu)", pending);
        api->draw_text_font(g, f10, s, CX + CW - api->text_width_font(f10, s), DY + 80, 0xffffe8a0);
    }
    for (int i = 0; i < PerPage; i++)
    {
        int n = page * PerPage + i;
        if (n >= Count_) break;
        const Def& d = All[n];
        bool on = unlocked.count(d.id) > 0;
        int y = DY + 92 + i * 44;
        api->fill_rect(g, CX, y, CW, 40, on ? 0x40fff060 : 0x30000000);
        ui::FitText(api, g, f12, d.name, CX + 10, y + 17, CW - 150, on ? ui::Yellow : 0xffd0d0d0);
        ui::FitText(api, g, f10, d.desc, CX + 10, y + 34, CW - 110, ui::White);
        snprintf(s, sizeof s, on ? "%d shells" : "%d shells", d.shells);
        api->draw_text_font(g, f10, s, CX + CW - 10 - api->text_width_font(f10, s), y + 17, on ? 0xff9cf09c : 0xffffe8a0);
        if (d.counter && !on)
        {
            snprintf(s, sizeof s, "%lld / %d", std::min<long long>(counters[d.counter], d.goal), d.goal);
            api->draw_text_font(g, f10, s, CX + CW - 10 - api->text_width_font(f10, s), y + 34, 0xffffffff);
        }
        else if (on) api->draw_text_font(g, f10, "Unlocked", CX + CW - 10 - api->text_width_font(f10, "Unlocked"), y + 34, 0xff9cf09c);
    }
    snprintf(s, sizeof s, "Page %d / %d", page + 1, Pages());
    api->draw_text_font(g, f12, s, CX + (CW - api->text_width_font(f12, s)) / 2, DY + DH - 90, 0xffffffff);
    ui::Button(api, g, Prev(), "< Prev", ui::Look::Center, page > 0);
    ui::Button(api, g, Next(), "Next >", ui::Look::Center, page < Pages() - 1);
    ui::DrawTooltip(api, g);
}

extern "C" __declspec(dllexport) void AchievementsOpen() { Open(); }   // from the main menu's Remastered page
extern "C" __declspec(dllexport) void RemodOpen() { Open(); }          // the Open button on the settings' Mods tab

static void Overlay(void* g)
{
    if (open)
    {
        if (ui::GetDialog(api, DialogId)) DrawList(g); else open = false;
    }
    else if (ui::OnMainMenu(api))
    {
        // rewards are paid here, between games
        Sync();
        long long n = counters.count("pending_shells") ? counters["pending_shells"] : 0;
        void* p = Profile();
        if (n > 0 && p)
        {
            at<int>(p, Profile_mShells) = (int)std::min<long long>(at<int>(p, Profile_mShells) + n, 9999999);
            counters.erase("pending_shells");
            dirty = true;
            Save();
            reinterpret_cast<bool(__thiscall*)(void*)>(App_SaveProfile)(api->app());
            char t[80];
            snprintf(t, sizeof t, "+%lld shells from achievements", n);
            api->toast(t);
            CheckProfile();
        }
    }
    // the unlock banner, at the top, one at a time
    DWORD now = GetTickCount();
    if (banner && now >= bannerUntil) { banner = nullptr; api->redraw(); }
    if (!banner && !queue.empty() && !open) { banner = queue.front(); queue.erase(queue.begin()); bannerUntil = now + 4000; api->play_sound(268); }   // SOUND_TONEHI
    if (banner)
    {
        void* f12 = ui::Font(FONT_JUNGLEFEVER12OUTLINE), *f10 = ui::Font(FONT_JUNGLEFEVER10OUTLINE);
        char t[96];
        snprintf(t, sizeof t, "%s  (+%d shells)", banner->desc, banner->shells);
        int w = std::max(api->text_width_font(f12, banner->name) + 180, api->text_width_font(f10, t)) + 30;
        int x = (640 - w) / 2, y = 76;
        api->fill_rect(g, x, y, w, 44, 0xe0201040);
        api->fill_rect(g, x + 2, y + 2, w - 4, 40, 0xe0402080);
        std::string head = std::string("Achievement unlocked: ") + banner->name;
        api->draw_text_font(g, f12, head.c_str(), x + (w - api->text_width_font(f12, head.c_str())) / 2, y + 19, 0xfffff000);
        api->draw_text_font(g, f10, t, x + (w - api->text_width_font(f10, t)) / 2, y + 36, 0xffffffff);
    }
}

// counters are written every 10 s while they change, and when the game closes
static void Tick(void*)
{
    static DWORD last;
    if (dirty && GetTickCount() - last > 10000) { last = GetTickCount(); Save(); }
}

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_DETACH) Save();
    return TRUE;
}

static void Load() { banners = api->config_int("achievements", "banners", 1) != 0; }

extern "C" __declspec(dllexport) const char* RemodDescribe() { return "31 achievements with shell rewards; the list is on the main menu (F4)."; }

extern "C" __declspec(dllexport) int RemodInit(const RemodApi* a)
{
    if (a->version < 3) return 0;
    api = a;
    userdata = ui::UserData();   // the game's own save folder (ProgramData\Steam\Insaniquarium\userdata for the Steam release)
    Load();
    bool ok = api->hook((void*)Board_InitLevel, (void*)&InitLevel, (void**)&oInitLevel)
           && api->hook((void*)Coin_MouseDown, (void*)&CoinMouseDown, (void**)&oCoinMouseDown)
           && api->hook((void*)Alien_Die, (void*)&AlienDie, (void**)&oAlienDie)
           && api->hook((void*)Fish_Eat, (void*)&FishEat, (void**)&oFishEat)
           && api->hook((void*)Board_BuyItem, (void*)&BuyItem, (void**)&oBuyItem)
           && api->hook((void*)Board_AddDeadFish, (void*)&AddDeadFish, (void**)&oAddDeadFish)
           && api->hook((void*)Board_BankCoins, (void*)&Bank, (void**)&oBank)
           && api->hook((void*)StoreScreen_Purchase, (void*)&Purchase, (void**)&oPurchase)
           && api->hook((void*)App_ButtonDepress, (void*)&ButtonHook, (void**)&oButton);
    if (!ok) return 0;
    api->on_mouse(Mouse);
    api->on_key(Key);
    api->on_overlay(Overlay);
    api->on_tick(Tick);
    api->on_config(Load);
    return 1;
}
