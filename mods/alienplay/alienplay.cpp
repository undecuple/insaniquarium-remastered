// alienplay: Play as the Alien (from Extra Modes). A 20-stage campaign mirroring Adventure (tanks 1-4, levels 1-5):
// you steer the alien Adventure sends at that level (the pointer; click to dash, or fire for the floor walkers) and
// eat every fish, while an AI keeper shoots you, feeds its fish, picks up coins and restocks. Two lives. The keeper
// gets stronger with each stage (laser, aim, speed, defending pets), murky water closes in from tank 2, and your
// alien's health bonus shrinks. Progress and best times per stage: userdata\alienplay<n>.txt.
#include "remod.h"
#include "game.h"
#include "remodui.h"
#include <windows.h>
#include <shlobj.h>
#include <math.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <algorithm>

using namespace game;
static const RemodApi* api;
static std::string userdata;

static const int Stages = 20;
static const int StageAliens[Stages] = { 1, 1, 2, 3, 3,   2, 3, 4, 5, -45,   3, -45, 7, 6, -67,   3, -67, 4, -67, -67 };
static bool active, over;
static int stage, alienType, lives, respawn, dash, dashCooldown, goalX = 320, goalY = 240, keeperShot, keeperCoin, keeperFeed, keeperBuy, buysLeft, startTick;
static bool fireNow;
static const int NextId = 0x4c, RetryId = 0x4d;

static const char* Name(int t)
{
    switch (t) { case 1: return "Little Sylvester"; case 2: return "Sylvester"; case 3: return "Balrog"; case 4: return "Gus";
                 case 5: return "Destructor"; case 6: return "Ulysses"; case 7: return "Psychosquid"; default: return "Alien"; }
}
static std::string StageName(int s) { return "Tank " + std::to_string(s / 5 + 1) + "-" + std::to_string(s % 5 + 1); }
static int Pick(int s) { int a = StageAliens[s]; return a >= 0 ? a : (rand() & 1) + (a == -45 ? 4 : 6); }
static double HealthScale() { return 3.0 - stage * 0.05; }
static int KeeperLaser() { return std::min(10, 1 + stage * 5 / 19); }
static int MurkRadius() { return stage < 5 ? 0 : 300 - (stage - 5) * 10; }
static bool Shooter() { return alienType == 5 || alienType == 6; }   // floor walkers fire instead of dashing

// ---- progress -------------------------------------------------------------------------------------------------------------
static std::map<std::string, long long> prog;
static std::string progFile;
static void* Profile() { void* a = api->app(); return a ? at<void*>(a, App_mProfile) : nullptr; }
static void LoadProgress()
{
    void* p = Profile();
    std::string f = userdata + "\\alienplay" + (p ? std::to_string(at<int>(p, 0x40)) : std::string("_guest")) + ".txt";
    if (f == progFile) return;
    progFile = f; prog.clear();
    std::ifstream in(f);
    std::string l;
    while (std::getline(in, l)) { size_t e = l.find('='); if (e != std::string::npos) prog[l.substr(0, e)] = atoll(l.c_str() + e + 1); }
}
static void SaveProgress()
{
    CreateDirectoryA(userdata.c_str(), nullptr);
    std::ofstream out(progFile, std::ios::trunc);
    for (auto& p : prog) out << p.first << "=" << p.second << "\n";
}
static int NextStage() { LoadProgress(); return (int)std::min<long long>(std::max<long long>(prog["stage"], 0), Stages - 1); }
static void Achievement(const char* id)
{
    HMODULE m = GetModuleHandleA("achievements.dll");
    if (auto f = m ? reinterpret_cast<void (*)(const char*)>(GetProcAddress(m, "AchievementUnlock")) : nullptr) f(id);
}

// ---- helpers --------------------------------------------------------------------------------------------------------------
static const int FishLists[] = { Board_mGuppies, Board_mOscars, Board_mPentas, Board_mUltras, Board_mGrubbers, Board_mGekkos, Board_mBreeders };
static int FishLeft(void* b) { int n = 0; for (int l : FishLists) n += Count(b, l); return n; }
static bool HasAliens(void* b) { return Count(b, Board_mAliens) > 0 || Count(b, Board_mBilaterus) > 0; }
static void* PlayerAlien(void* b) { return Count(b, Board_mAliens) > 0 ? Item(b, Board_mAliens, 0) : nullptr; }
// never inside a co-op game (a stage left running when the host started would steer the alien on this machine only)
static bool On() { void* a = api->app(); return active && a && api->board() && at<int>(a, App_mGameMode) == 4 && !ui::CoopPlaying(); }
// a stage is being played (the achievements mod doesn't count the keeper's kills and coins as the player's)
extern "C" __declspec(dllexport) int AlienPlayActive() { return On() ? 1 : 0; }
static void BoardClick(void* b, int x, int y)   // as a player's click on the tank (the keeper's laser, feeding)
{
    reinterpret_cast<void(__thiscall*)(void*, int, int, int)>((*reinterpret_cast<void***>(b))[0xd8 / 4])(b, x, y, 1);
}
static void Remove(void* o) { reinterpret_cast<void(__thiscall*)(void*, bool)>(GameObject_RemoveFromGame)(o, true); }

// ---- starting a stage ------------------------------------------------------------------------------------------------------
static void StartStage(int s)
{
    void* a = api->app();
    stage = std::min(std::max(s, 0), Stages - 1);
    active = true; over = false;
    if (HMODULE m = GetModuleHandleA("mutators.dll"))   // the campaign is played without the player's mutators
        if (auto force = reinterpret_cast<void (*)(int)>(GetProcAddress(m, "MutatorsForce"))) force(0);
    at<bool>(a, 0x880) = false;
    reinterpret_cast<void(__thiscall*)(void*)>(App_RemoveGameSelector)(a);
    at<int>(a, App_mGameMode) = 4;
    at<int>(a, 0x888) = stage / 5 + 1;
    reinterpret_cast<void(__thiscall*)(void*)>(App_RemoveBoard)(a);
    reinterpret_cast<void(__thiscall*)(void*)>(App_StartBoard)(a);
}
extern "C" __declspec(dllexport) void AlienPlayStart() { StartStage(NextStage()); }

typedef void(__thiscall* VoidFn)(void*);
static VoidFn oStartLevel, oSelector;

static void Stock(void* b)
{
    for (int l : FishLists) while (Count(b, l)) Remove(Item(b, l, Count(b, l) - 1));
    for (int l : { Board_mCoins, Board_mFood }) while (Count(b, l)) Remove(Item(b, l, Count(b, l) - 1));
    int tank = stage / 5 + 1, extra = stage / 7;
    int guppies = 4 + stage / 3 + rand() % 3;
    for (int i = 0; i < guppies; i++)
    {
        void* f = reinterpret_cast<void*(__thiscall*)(void*, int, int)>(Board_AddGuppyAt)(b, 40 + rand() % 500, 120 + rand() % 220);
        if (f) at<int>(f, 0x1a0) = rand() % 3;
    }
    auto xy = [](int& x, int& y) { x = 40 + rand() % 480; y = 110 + rand() % 200; };
    int x, y;
    if (stage >= 2) for (int i = rand() % 2 + extra; i > 0; i--) { xy(x, y); reinterpret_cast<void(__thiscall*)(void*, int, int, bool)>(Board_AddOscarAt)(b, x, y, true); }
    if (tank == 2) for (int i = rand() % 2 + 1 + extra; i > 0; i--) reinterpret_cast<void(__thiscall*)(void*, int)>(Board_AddPentaAt)(b, 40 + rand() % 500);
    if (tank == 3)
    {
        for (int i = rand() % 2 + 1; i > 0; i--) reinterpret_cast<void(__thiscall*)(void*, int)>(Board_AddGrubberAt)(b, 40 + rand() % 500);
        if (rand() % 2 == 0 || extra > 0) { xy(x, y); reinterpret_cast<void(__thiscall*)(void*, int, int, bool)>(Board_AddGekkoAt)(b, x, y, true); }
    }
    if (tank == 4)
    {
        for (int i = rand() % 2 + 1; i > 0; i--) { xy(x, y); reinterpret_cast<void*(__thiscall*)(void*, int, int)>(Board_AddBreederAt)(b, x, y); }
        for (int i = rand() % 2 + (extra > 1 ? 1 : 0); i > 0; i--) { xy(x, y); reinterpret_cast<void(__thiscall*)(void*, int, int, bool)>(Board_AddUltraAt)(b, x, y, true); }
    }
    // pets against you: Itchy from tank 1, Rufus from 2, Gash and Angie in 4 (one from 1-3, one more every five stages)
    std::vector<int> defenders = tank == 1 ? std::vector<int>{ 2 } : tank < 4 ? std::vector<int>{ 2, 7 } : std::vector<int>{ 2, 7, 0x11, 0x12 };
    std::vector<int> pets;
    int want = std::min((stage + 3) / 5, (int)defenders.size());
    while ((int)pets.size() < want) { int p = defenders[rand() % defenders.size()]; if (std::find(pets.begin(), pets.end(), p) == pets.end()) pets.push_back(p); }
    for (int helpers = 1 + stage / 10, tries = 0; helpers > 0 && tries < 50; tries++)
    {
        int p = rand() % std::min(tank * 5, 24);   // never Presto, Wadsworth (hides guppies) or another defender
        if (p == 0x13 || p == 9 || std::find(pets.begin(), pets.end(), p) != pets.end() || p == 2 || p == 7 || p == 0x11 || p == 0x12) continue;
        pets.push_back(p); helpers--;
    }
    // the tank's own pets from the profile were put in by the game: keep them out, the stage chooses its own
    for (int l : { Board_mFishPets, 0xb0 }) while (Count(b, l)) Remove(Item(b, l, Count(b, l) - 1));
    for (int p : pets) reinterpret_cast<void*(__thiscall*)(void*, int, int, int, bool, bool)>(Board_AddPet)(b, p, -1, -1, false, false);
    for (int i = 0; i < 12; i++) if (i != 0xb) reinterpret_cast<void(__thiscall*)(void*, int, bool)>(Board_UnlockStoreItem)(b, i, false);
    at<int>(b, Board_mWeaponLevel) = KeeperLaser();
    at<int>(b, Board_mMoney) = 200 + stage * 50 + rand() % 3 * 100;
    reinterpret_cast<VoidFn>(Board_UpdateMoneyLabel)(b);
    alienType = Pick(stage);
    at<int>(b, Board_mAlienType) = 0;
    at<int>(b, Board_mAlienTimer) = 99999;   // no normal waves
    lives = 2; respawn = 150; dash = dashCooldown = 0;
    keeperShot = 100; keeperCoin = 0; keeperFeed = 0; keeperBuy = 300; buysLeft = 2 + stage / 4;
    startTick = at<int>(b, Board_mTick);
    api->toast((StageName(stage) + ": you are " + Name(alienType) + "! Eat every fish!").c_str());
}

static void __fastcall StartLevel(void* b, void*)
{
    oStartLevel(b);
    if (!On()) { active = false; return; }
    Stock(b);
}

static void __fastcall Selector(void* a, void*) { active = false; oSelector(a); }

// ---- steering: the player's alien follows the pointer instead of the AI ------------------------------------------------------
static bool(__thiscall* oThink)(void*);
static bool __fastcall Think(void* al, void*)
{
    void* b = api->board();
    if (!On() || over || !b || PlayerAlien(b) != al) return oThink(al);
    double speedDiv = at<double>(al, Alien_mSpeedDiv);
    bool dashing = dash > 0;
    double screen = std::min(std::max(1.4 * sqrt(1.6 / speedDiv), 0.9), 1.6) * (dashing ? 2.2 : 1.0);
    double maxV = screen * speedDiv, acc = (dashing ? 0.5 : 0.15) * speedDiv;
    double& vx = at<double>(al, 0x170), &vy = at<double>(al, 0x178);
    double dx = goalX - (at<double>(al, 0x160) + 80.0), dy = goalY - (at<double>(al, 0x168) + 80.0);
    if (dx > 6 && vx < maxV) vx += acc; else if (dx < -6 && vx > -maxV) vx -= acc; else if (fabs(dx) <= 6) vx *= 0.85;
    int t = at<int>(al, Alien_mAlienType);
    if (t != 5 && t != 6) { if (dy > 6 && vy < maxV) vy += acc; else if (dy < -6 && vy > -maxV) vy -= acc; else if (fabs(dy) <= 6) vy *= 0.85; }
    if (!dashing) { vx = std::min(std::max(vx, -maxV), maxV); vy = std::min(std::max(vy, -maxV), maxV); }
    if (fireNow) { fireNow = false; at<int>(al, 0x1a4) = at<int>(al, 0x1a8); }   // mFireTimer = mFireDelay: fire now
    if (at<int>(al, 0x1a0) < 1 || t == 4) reinterpret_cast<void(__thiscall*)(void*)>(Alien_TryEat)(al);   // mInvuln
    return true;
}

// ---- the end of a stage ------------------------------------------------------------------------------------------------------
static void Dialog(int id, const char* header, const std::string& text, int buttons, const char* footer = "")
{
    void* a = api->app();
    MsvcString h = MakeString(header), l = MakeString(text.c_str()), f = MakeString(footer);
    void** vt = *reinterpret_cast<void***>(a);
    void* d = reinterpret_cast<void*(__thiscall*)(void*, int, bool, const MsvcString*, const MsvcString*, const MsvcString*, int)>(vt[App_vDoDialog / 4])(a, id, true, &h, &l, &f, buttons);
    if (d) reinterpret_cast<void(__thiscall*)(void*, int)>((*reinterpret_cast<void***>(d))[0x130 / 4])(d, 30);
}

static void Won(void* b)
{
    over = true;
    reinterpret_cast<void(__thiscall*)(void*, bool)>(Board_Pause)(b, true);
    at<bool>(b, 0x4ee) = false;   // nothing to save
    int secs = std::max(1, (at<int>(b, Board_mTick) - startTick) * 28 / 1000);
    LoadProgress();
    std::string k = "t" + std::to_string(stage);
    long long best = prog[k];
    bool better = best <= 0 || secs < best;
    if (better) prog[k] = secs;
    if (stage + 1 > prog["stage"]) prog["stage"] = std::min(stage + 1, Stages - 1);
    bool last = stage >= Stages - 1;
    if (last) prog["done"] = 1;
    SaveProgress();
    Achievement("alienplay");
    if (last) Achievement("alienplay_all");
    char t[200];
    snprintf(t, sizeof t, "%s ate every fish in %d:%02d.%s", Name(alienType), secs / 60, secs % 60,
             better ? " A new best!" : (" Best: " + std::to_string(best / 60) + ":" + (best % 60 < 10 ? "0" : "") + std::to_string(best % 60)).c_str());
    if (last) Dialog(0x11, "EVERY TANK IS YOURS!", std::string(t) + " All four tanks conquered!", 3, "Click to Continue");
    else Dialog(NextId, "THE TANK IS YOURS!", std::string(t) + " Next: " + StageName(stage + 1) + ". Play it now?", 1);
}

static void Lost(void* b)
{
    over = true;
    reinterpret_cast<void(__thiscall*)(void*, bool)>(Board_Pause)(b, true);
    at<bool>(b, 0x4ee) = false;
    Dialog(RetryId, "THE KEEPER WINS", "Your last alien was blasted out of " + StageName(stage) + ". Try again?", 1);
}

static void(__thiscall* oButton)(void*, int);
static void __fastcall ButtonHook(void* a, void*, int id)
{
    int base = id >= 3000 ? id - 3000 : id - 2000;
    if (base == NextId || base == RetryId)
    {
        ui::KillDialog(api, base);
        if (id < 3000) StartStage(base == NextId ? stage + 1 : stage);
        else { active = false; oButton(a, 0x11 + 2000); }   // back to the main menu, as after a game over
        return;
    }
    oButton(a, id);
}

// the game's own "all your fish have died" is our win
static void(__thiscall* oDialog)(void*, int, bool, const MsvcString*, const MsvcString*, const MsvcString*, int);
static void __fastcall DialogHook(void* a, void*, int id, bool modal, const MsvcString* h, const MsvcString* l, const MsvcString* f, int buttons)
{
    if ((id == 0x10 || id == 0x11) && On())
    {
        if (!over) Won(api->board());
        return;
    }
    oDialog(a, id, modal, h, l, f, buttons);
}

// ---- every tick: the alien's lives, the end, the keeper ---------------------------------------------------------------------
static void Tick(void* b)
{
    if (!On() || over || at<bool>(b, Board_mPaused)) return;
    if (dash > 0) dash--;
    if (dashCooldown > 0) dashCooldown--;
    at<int>(b, Board_mAlienTimer) = 99999;
    api->mouse_pos(&goalX, &goalY);
    if (FishLeft(b) == 0) { Won(b); return; }
    if (!HasAliens(b))
    {
        if (lives <= 0) { Lost(b); return; }
        if (--respawn <= 0)
        {
            lives--;
            respawn = 300;
            int x = std::min(std::max(goalX - 80, 0x14), 0x1d6), y = std::min(std::max(goalY - 80, 0x69), 0x12c);
            at<int>(b, Board_mAlienType) = alienType;
            reinterpret_cast<void(__thiscall*)(void*, int, int, int, bool)>(Board_SpawnAlien)(b, alienType, x, y, true);
            at<int>(b, Board_mAlienType) = 0;
            if (void* al = PlayerAlien(b)) { at<double>(al, Alien_mHealth) *= HealthScale(); at<double>(al, Alien_mMaxHealth) = at<double>(al, Alien_mHealth); }
        }
    }
    // the keeper
    void* al = PlayerAlien(b);
    if (al && at<int>(al, 0x158) == 0)   // mWarpIn done
    {
        if (--keeperShot <= 0)
        {
            keeperShot = 46 - stage + rand() % 15;
            bool hold = at<int>(al, Alien_mAlienType) == 7 && at<bool>(al, 0x204) && rand() % 4;   // Psychosquid heals from shots
            if (!hold)
            {
                int spread = rand() % 100 < 45 - stage * 3 / 2 ? 130 : 50;
                int x = at<int>(al, Widget_mX) + 80 + rand() % (spread * 2) - spread, y = at<int>(al, Widget_mY) + 80 + rand() % (spread * 2) - spread;
                BoardClick(b, std::min(std::max(x, 0x20), 0x260), std::min(std::max(y, 0x50), 0x1c0));
            }
        }
    }
    else if (--keeperFeed <= 0)
    {
        keeperFeed = 60 + rand() % 40;
        for (int l : { Board_mGuppies, Board_mBreeders })
            for (int i = 0; i < Count(b, l); i++)
            {
                void* f = Item(b, l, i);
                if (at<int>(f, GameObject_mHungerTimer) < 301 && Count(b, Board_mFood) < 3)
                { BoardClick(b, std::min(std::max(at<int>(f, Widget_mX) + 40, 0x30), 0x240), std::min(std::max(at<int>(f, Widget_mY) + 10, 0x50), 0x150)); goto fed; }
            }
    fed:;
    }
    if (--keeperCoin <= 0)
    {
        keeperCoin = 25 + rand() % 20;
        void* best = nullptr;
        for (int i = 0; i < Count(b, Board_mCoins); i++)
        {
            void* c = Item(b, Board_mCoins, i);
            if (!at<bool>(c, Coin_mCollected) && at<int>(c, Coin_mCoinType) < 0xf && (!best || at<int>(c, Widget_mY) > at<int>(best, Widget_mY))) best = c;
        }
        if (best) reinterpret_cast<void(__thiscall*)(void*, int, int, int)>((*reinterpret_cast<void***>(best))[0xd8 / 4])(best, 20, 20, 1);
    }
    if (--keeperBuy <= 0)
    {
        keeperBuy = 450 - stage * 10 + rand() % 200;
        if (buysLeft > 0 && FishLeft(b) < 5)
        {
            int item = at<int>(b, Board_mStoreSlot) >= 0 ? 0 : 1;   // tank 4 sells breeders instead of guppies
            reinterpret_cast<void(__thiscall*)(void*, int)>(Board_BuyItem)(b, item);
            buysLeft--;
        }
    }
}

// ---- input: the pointer steers, a click dashes (or fires); no shooting or feeding for the alien --------------------------------
static int Mouse(int x, int y, int button, int down)
{
    void* b = api->board();
    if (!On() || over || !b || ui::DialogCount(api) > 0) return 0;
    if (x >= 0x20d && y < 0x22) return 0;   // the Menu button still works
    if (button != 0) return 1;
    if (down && dashCooldown == 0 && HasAliens(b))
    {
        if (Shooter()) fireNow = true; else dash = 40;
        dashCooldown = 250;
    }
    return 1;
}

static void Overlay(void* g)
{
    void* b = api->board();
    if (!On() || !b || ui::DialogCount(api) > 0 || at<bool>(b, Board_mPaused)) return;
    // murky water: the alien only sees around itself (from tank 2)
    int r = MurkRadius();
    void* al = PlayerAlien(b);
    if (r > 0 && !over)
    {
        int cx = al ? at<int>(al, Widget_mX) + 80 : goalX, cy = al ? at<int>(al, Widget_mY) + 80 : goalY, top = 0x46;
        unsigned a0 = (unsigned)std::min(std::max(170 + (stage - 5) * 4, 170), 226);
        unsigned dark = a0 << 24 | 0x040e0a, soft = (a0 * 2 / 3) << 24 | 0x040e0a;
        int l = std::max(cx - r, 0), rt = std::min(cx + r, 640), t = std::max(cy - r, top), bt = std::min(cy + r, 480);
        api->fill_rect(g, 0, top, 640, std::max(t - top, 0), dark);
        api->fill_rect(g, 0, bt, 640, 480 - bt, dark);
        api->fill_rect(g, 0, t, l, bt - t, dark);
        api->fill_rect(g, rt, t, 640 - rt, bt - t, dark);
        api->fill_rect(g, l, t, rt - l, 14, soft); api->fill_rect(g, l, bt - 14, rt - l, 14, soft);
        api->fill_rect(g, l, t + 14, 14, bt - t - 28, soft); api->fill_rect(g, rt - 14, t + 14, 14, bt - t - 28, soft);
    }
    void* f = ui::Font(FONT_JUNGLEFEVER12OUTLINE), *f10 = ui::Font(FONT_JUNGLEFEVER10OUTLINE);
    char s[160];
    std::string stars(std::max(0, lives + (HasAliens(b) ? 1 : 0)), '*');
    snprintf(s, sizeof s, "%s   %s   Lives %s   Fish left %d   Keeper laser %d", StageName(stage).c_str(), Name(alienType), stars.empty() ? "-" : stars.c_str(), FishLeft(b), at<int>(b, Board_mWeaponLevel));
    api->fill_rect(g, 0x24, 0x4a, api->text_width_font(f, s) + 12, 20, 0x82000000);
    api->draw_text_font(g, f, s, 0x2a, 0x5e, 0xff9cf09c);
    if (al && at<double>(al, Alien_mMaxHealth) > 0)
    {
        int w = 120, hp = (int)(w * std::min(std::max(at<double>(al, Alien_mHealth) / at<double>(al, Alien_mMaxHealth), 0.0), 1.0));
        api->fill_rect(g, 0x2a, 0x62, w + 2, 7, 0xa0000000);
        api->fill_rect(g, 0x2b, 0x63, hp, 5, 0xffe03030);
        const char* d = Shooter() ? (dashCooldown == 0 ? "Fire ready (click)" : "Reloading...") : dashCooldown == 0 ? "Dash ready (click)" : "Dash...";
        api->draw_text_font(g, f10, d, 0x2a + w + 10, 0x6a, dashCooldown == 0 ? 0xffffffff : 0xffa0a0a0);
    }
    else if (lives > 0)
    {
        snprintf(s, sizeof s, "Warping in... %d", respawn * 28 / 1000 + 1);
        api->draw_text_font(g, f10, s, 0x2a, 0x70, 0xffffffff);
    }
}

extern "C" __declspec(dllexport) int ModeActive() { return On() ? 1 : 0; }   // for the content mod

extern "C" __declspec(dllexport) const char* RemodDescribe() { return "Play as the Alien: a 20-stage campaign against an AI keeper (Extra Modes)."; }

extern "C" __declspec(dllexport) int RemodInit(const RemodApi* a)
{
    if (a->version < 4) return 0;
    api = a;
    char base[MAX_PATH];
    if (SHGetFolderPathA(nullptr, CSIDL_COMMON_APPDATA, nullptr, 0, base) != S_OK) return 0;
    userdata = std::string(base) + "\\PopCap Games\\Insaniquarium\\userdata";
    bool ok = api->hook((void*)Board_StartLevel, (void*)&StartLevel, (void**)&oStartLevel)
           && api->hook((void*)App_ShowGameSelector, (void*)&Selector, (void**)&oSelector)
           && api->hook((void*)Alien_Think, (void*)&Think, (void**)&oThink)
           && api->hook((void*)App_ButtonDepress, (void*)&ButtonHook, (void**)&oButton)
           && api->hook((void*)App_DoTimedDialog, (void*)&DialogHook, (void**)&oDialog);
    if (!ok) return 0;
    api->on_tick(Tick);
    api->on_mouse(Mouse);
    api->on_overlay(Overlay);
    return 1;
}
