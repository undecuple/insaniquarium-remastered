// extramodes: the Extra Modes screen (a button on the main menu) with new ways to play:
//   Daily Challenge  one Challenge tank a day with two mutators and the same random seed for everyone that day
//   Boss Rush        every alien in turn, each 12% tougher, then the double waves (tank 4, $3,000, laser level 4)
//   Endless          alien waves that never stop, sooner and tougher each time; how many can your tank survive?
//   Sandbox / Play as Alien (when those mods are installed)
//   Records          your bests, and Hall of Fame tables for Hard mode and Endless
// Bests are kept per profile in userdata\extramodes<n>.txt, the Hall of Fame tables in userdata\halloffame_extra.txt.
#include "remod.h"
#include "game.h"
#include "remodui.h"
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <algorithm>

using namespace game;
static const RemodApi* api;
static std::string userdata;

enum Mode { None, Daily, BossRush, Endless };
static Mode mode;
static int dailyKey, stage, wave, lastWaves, startTick;
static bool finished, eggWarned;
static const int Sequence[] = { 2, 3, 4, 5, 6, 7, 8, 9, 11, 10 };   // boss rush: alien types in order (9-12 = pairs)
static const int Stages = sizeof Sequence / sizeof Sequence[0];
enum { MutHungry = 1, MutRich = 128, MutHard = 256 };

// ---- small helpers ----------------------------------------------------------------------------------------------------
static void* App() { return api->app(); }
static void* Profile() { void* a = App(); return a ? at<void*>(a, App_mProfile) : nullptr; }
static std::string ProfileName()
{
    void* p = Profile();
    if (!p) return "Player";
    char* s = static_cast<char*>(p) + 0x24;   // std::string mName
    uint32_t res = at<uint32_t>(s, 0x18);
    const char* text = res >= 16 ? at<const char*>(s, 4) : s + 4;
    return text;
}
template <typename F> static F Export(const char* dll, const char* name)
{
    HMODULE m = GetModuleHandleA(dll);
    return m ? reinterpret_cast<F>(GetProcAddress(m, name)) : nullptr;
}
static void Achievement(const char* id) { if (auto f = Export<void (*)(const char*)>("achievements.dll", "AchievementUnlock")) f(id); }
static int Mutators() { auto f = Export<int (*)()>("mutators.dll", "MutatorsActive"); return f ? f() : 0; }
static std::string Clock(long long s) { char t[16]; snprintf(t, sizeof t, "%lld:%02lld", s / 60, s % 60); return t; }

// ---- records ----------------------------------------------------------------------------------------------------------
static std::map<std::string, long long> records;
static std::string recordsFile;
static void LoadRecords()
{
    void* p = Profile();
    std::string f = userdata + "\\extramodes" + (p ? std::to_string(at<int>(p, 0x40)) : std::string("_guest")) + ".txt";
    if (f == recordsFile) return;
    recordsFile = f;
    records.clear();
    std::ifstream in(f);
    std::string line;
    while (std::getline(in, line)) { size_t eq = line.find('='); if (eq != std::string::npos) records[line.substr(0, eq)] = atoll(line.c_str() + eq + 1); }
}
static void SaveRecords()
{
    CreateDirectoryA(userdata.c_str(), nullptr);
    std::ofstream out(recordsFile, std::ios::trunc);
    for (auto& r : records) out << r.first << "=" << r.second << "\n";
}
static long long Record(const std::string& k) { LoadRecords(); auto it = records.find(k); return it == records.end() ? 0 : it->second; }
static void SetRecord(const std::string& k, long long v) { LoadRecords(); records[k] = v; SaveRecords(); }

// Hall of Fame tables: "table name value" lines, top 3 per table (endless, hardtt1-4 money, hardch1-4 seconds)
struct Entry { std::string table, name; long long value; };
static std::vector<Entry> fame;
static std::string FameFile() { return userdata + "\\halloffame_extra.txt"; }
static void LoadFame()
{
    fame.clear();
    std::ifstream in(FameFile());
    std::string t, n; long long v;
    while (in >> t >> v >> std::ws && std::getline(in, n)) fame.push_back({ t, n, v });
}
static void AddFame(const std::string& table, const std::string& name, long long v, bool higherBetter)
{
    LoadFame();
    fame.push_back({ table, name, v });
    std::vector<Entry> keep;
    for (auto& e : fame) if (e.table != table) keep.push_back(e);
    std::vector<Entry> mine;
    for (auto& e : fame) if (e.table == table) mine.push_back(e);
    std::stable_sort(mine.begin(), mine.end(), [&](const Entry& a, const Entry& b) { return higherBetter ? a.value > b.value : a.value < b.value; });
    if (mine.size() > 3) mine.resize(3);
    keep.insert(keep.end(), mine.begin(), mine.end());
    fame = keep;
    CreateDirectoryA(userdata.c_str(), nullptr);
    std::ofstream out(FameFile(), std::ios::trunc);
    for (auto& e : fame) out << e.table << " " << e.value << " " << e.name << "\n";
}
static bool CountsAsHard() { int m = Mutators(); return (m & MutHard) && !(m & MutRich) && mode != BossRush; }

// ---- the daily challenge -----------------------------------------------------------------------------------------------
static int Today() { SYSTEMTIME t; GetLocalTime(&t); return t.wYear * 10000 + t.wMonth * 100 + t.wDay; }
static uint32_t Mix(int key)
{
    uint32_t x = (uint32_t)key * 2654435761u;
    x ^= x >> 15; x *= 0x2c1b3c6d; x ^= x >> 12; x *= 0x297a2d39; x ^= x >> 15;
    return x;
}
static int DailyTank(int key) { return (int)(Mix(key + 1) % 4) + 1; }
static int DailyMutators(int key)   // two different mutators (never Rich start)
{
    int pool[14], n = 0;
    for (int i = 0; i < 15; i++) if ((1 << i) != MutRich) pool[n++] = 1 << i;
    uint32_t h = Mix(key + 2);
    int a = (int)(h % (uint32_t)n), b = (int)((h / 7) % (uint32_t)(n - 1));
    if (b >= a) b++;
    return pool[a] | pool[b];
}
// seeds the game's own random generator (MTRand at app +0x7b0: 624 words, then the index)
static void SeedGame(uint32_t seed)
{
    uint32_t* mt = at<uint32_t*>(App(), 0x7b0);
    if (!mt) return;
    if (!seed) seed = 4357;
    mt[0] = seed;
    for (int i = 1; i < 624; i++) mt[i] = 1812433253u * (mt[i - 1] ^ (mt[i - 1] >> 30)) + i;
    mt[624] = 624;
}

// ---- starting a mode ---------------------------------------------------------------------------------------------------
static void Start(Mode m)
{
    void* a = App();
    mode = m; stage = 0; wave = 0; finished = false; eggWarned = false; lastWaves = 0;
    int tank = 4;
    if (m == Daily)
    {
        dailyKey = Today();
        tank = DailyTank(dailyKey);
        if (auto force = Export<void (*)(int)>("mutators.dll", "MutatorsForce")) force(DailyMutators(dailyKey));
        SeedGame(Mix(dailyKey) | 1);
    }
    at<bool>(a, 0x880) = false;      // as the main menu's own buttons do
    reinterpret_cast<void(__thiscall*)(void*)>(App_RemoveGameSelector)(a);
    at<int>(a, App_mGameMode) = 4;   // Challenge
    at<int>(a, 0x888) = tank;        // mSelectedTank
    reinterpret_cast<void(__thiscall*)(void*, bool, bool)>(App_StartGame)(a, false, false);
}

// ---- hooks ---------------------------------------------------------------------------------------------------------------
typedef void(__thiscall* VoidFn)(void*);
static VoidFn oStartLevel, oSelector, oBank;
static void*(__thiscall* oAlienCtor)(void*, int, int, int);
static void(__thiscall* oBuyItem)(void*, int);
static void(__thiscall* oDialog)(void*, int, bool, const MsvcString*, const MsvcString*, const MsvcString*, int);
static bool(__thiscall* oAddTimeTrial)(void*, int, void*, int);
static void(__thiscall* oButton)(void*, int);

// never inside a co-op game (a mode left set when the host started a co-op Challenge would apply on this machine only)
static bool Active() { void* a = App(); return mode != None && a && at<int>(a, App_mGameMode) == 4 && !ui::CoopPlaying(); }

static void __fastcall StartLevel(void* b, void*)
{
    oStartLevel(b);
    if (!Active()) { mode = None; return; }
    startTick = at<int>(b, Board_mTick);
    lastWaves = at<int>(b, Board_mAlienWaves);
    if (mode == BossRush)
    {
        at<int>(b, Board_mMoney) = 3000;
        at<int>(b, Board_mWeaponLevel) = std::max(at<int>(b, Board_mWeaponLevel), 4);
        at<int>(b, Board_mAlienType) = Sequence[0];
        at<int>(b, Board_mAlienTimer) = 900;
        reinterpret_cast<VoidFn>(Board_UpdateMoneyLabel)(b);
        api->toast("Boss Rush: defeat every alien as fast as you can!");
    }
    else if (mode == Endless)
    {
        reinterpret_cast<void*(__thiscall*)(void*)>(Board_SpawnGuppy)(b);
        reinterpret_cast<void*(__thiscall*)(void*)>(Board_SpawnGuppy)(b);
        at<int>(b, Board_mMoney) = 600;
        reinterpret_cast<VoidFn>(Board_UpdateMoneyLabel)(b);
        for (int i = 0; i < 0xb; i++) reinterpret_cast<void(__thiscall*)(void*, int, bool)>(Board_UnlockStoreItem)(b, i, false);
        at<int>(b, Board_mAlienType) = 1;   // little Sylvester first
        at<int>(b, Board_mAlienTimer) = 4000;
        api->toast("Endless: survive as many waves as you can!");
    }
    else if (mode == Daily)
    {
        auto describe = Export<const char* (*)(int)>("mutators.dll", "MutatorsDescribe");
        std::string t = std::string("Daily challenge: ") + (describe ? describe(Mutators()) : "");
        api->toast(t.c_str());
    }
}

static void* __fastcall AlienCtor(void* al, void*, int x, int y, int type)
{
    oAlienCtor(al, x, y, type);
    if (Active() && (mode == BossRush || mode == Endless))
    {
        double k = mode == BossRush ? 1 + 0.12 * stage : 0.7 + 0.08 * std::max(0, wave - 1);
        at<double>(al, Alien_mHealth) *= k;
        at<double>(al, Alien_mMaxHealth) = at<double>(al, Alien_mHealth);
    }
    return al;
}

static void __fastcall BuyItem(void* b, void*, int item)
{
    if (Active() && mode == Endless && item == 0xb)
    {
        if (!eggWarned) { api->toast("No egg pieces in Endless: just survive!"); eggWarned = true; }
        return;
    }
    oBuyItem(b, item);
}

// a game-over style dialog straight from the game (not through other mods' hooks); its button goes back to the menu
static void EndDialog(const char* header, const char* text)
{
    void* a = App();
    MsvcString h = MakeString(header), l = MakeString(text), f = MakeString("Click to Continue");
    void** vt = *reinterpret_cast<void***>(a);
    void* d = reinterpret_cast<void*(__thiscall*)(void*, int, bool, const MsvcString*, const MsvcString*, const MsvcString*, int)>(vt[App_vDoDialog / 4])(a, 0x11, true, &h, &l, &f, 3);
    if (d) reinterpret_cast<void(__thiscall*)(void*, int)>((*reinterpret_cast<void***>(d))[0x130 / 4])(d, 30);   // buttons off for 30 ticks
}

static void __fastcall DialogHook(void* a, void*, int id, bool modal, const MsvcString* h, const MsvcString* l, const MsvcString* f, int buttons)
{
    if (id == 0x11 && Active() && mode == Endless && !finished)
    {
        finished = true;
        void* b = api->board();
        int waves = std::max(0, wave - (b && (Count(b, Board_mAliens) || Count(b, Board_mBilaterus)) ? 1 : 0));
        long long best = Record("endless_best");
        if (waves > best) SetRecord("endless_best", waves);
        AddFame("endless", ProfileName(), waves, true);
        char t[160];
        snprintf(t, sizeof t, "You survived %d wave%s. %s", waves, waves == 1 ? "" : "s", waves > best ? "A new best!" : ("Best: " + std::to_string(best) + ".").c_str());
        api->log("extramodes: endless over after %d waves", waves);
        EndDialog("THE TANK HAS FALLEN", t);
        return;
    }
    oDialog(a, id, modal, h, l, f, buttons);
}

// the level is won (also called at a game over, then with fewer than 4 egg pieces)
static void __fastcall Bank(void* b, void*)
{
    if (at<int>(b, 0x43c) >= 4 && at<int>(App(), App_mGameMode) == 4)
    {
        int secs = std::max(1, (at<int>(b, Board_mTick) * 28 - at<int>(b, Board_mLevelStartTime)) / 1000);
        if (Active() && mode == Daily)
        {
            std::string k = "daily_" + std::to_string(dailyKey);
            long long best = Record(k);
            if (best <= 0 || secs < best) SetRecord(k, secs);
            Achievement("daily");
            api->toast(("Daily challenge done in " + Clock(secs) + (best <= 0 || secs < best ? " - your best today!" : "")).c_str());
        }
        if (CountsAsHard()) AddFame("hardch" + std::to_string(at<int>(b, Board_mTank)), ProfileName(), secs, false);
    }
    oBank(b);
}

static bool __fastcall AddTimeTrial(void* mgr, void*, int tank, void* profile, int money)
{
    bool r = oAddTimeTrial(mgr, tank, profile, money);
    if (CountsAsHard() && tank >= 1 && tank <= 4) AddFame("hardtt" + std::to_string(tank), ProfileName(), money, true);
    return r;
}

static void __fastcall Selector(void* a, void*) { mode = None; oSelector(a); }

static void Ticked(void* b)
{
    if (!Active() || finished || at<bool>(b, Board_mPaused)) return;
    bool aliens = Count(b, Board_mAliens) > 0 || Count(b, Board_mBilaterus) > 0;
    int waves = at<int>(b, Board_mAlienWaves);
    if (mode == BossRush)
    {
        if (waves != lastWaves)   // a wave just came in: the next one waits for the tank to be clear
        {
            lastWaves = waves;
            stage++;
            at<int>(b, Board_mAlienType) = stage < Stages ? Sequence[stage] : 0;
            at<int>(b, Board_mAlienTimer) = 450;
        }
        if (stage >= Stages && !aliens)
        {
            finished = true;
            int secs = std::max(1, (at<int>(b, Board_mTick) - startTick) * 28 / 1000);
            long long best = Record("bossrush_best");
            bool better = best <= 0 || secs < best;
            if (better) SetRecord("bossrush_best", secs);
            AddFame("bossrush", ProfileName(), secs, false);
            Achievement("bossrush");
            reinterpret_cast<void(__thiscall*)(void*, bool)>(Board_Pause)(b, true);
            EndDialog("BOSS RUSH CLEARED!", ("Every alien defeated in " + Clock(secs) + (better ? ". A new best!" : ". Best: " + Clock(best) + ".")).c_str());
        }
        if (stage < Stages && !aliens && at<int>(b, Board_mAlienType) == 0) at<int>(b, Board_mAlienType) = Sequence[stage];
    }
    else if (mode == Endless && waves != lastWaves)
    {
        lastWaves = waves;
        wave++;
        if (wave == 10) Achievement("endless_10");
        if (wave == 25) Achievement("endless_25");
        int type = at<int>(b, Board_mAlienType);
        for (int extra = wave / 6; extra > 0; extra--)   // an extra alien every sixth wave
            reinterpret_cast<void(__thiscall*)(void*, int, int, int, bool)>(Board_SpawnAlien)(b, type >= 8 ? 2 : (type ? type : 2), 40 + rand() % 400, 120 + rand() % 120, false);
        std::vector<int> pool = { 2 };
        if (wave < 4) pool.push_back(1);
        if (wave >= 2) pool.push_back(3);
        if (wave >= 4) pool.push_back(4);
        if (wave >= 6) pool.push_back(5);
        if (wave >= 8) { pool.push_back(6); pool.push_back(7); }
        if (wave >= 10) pool.push_back(8);
        if (wave >= 13) { pool.push_back(9); pool.push_back(10); pool.push_back(11); pool.push_back(12); }
        at<int>(b, Board_mAlienType) = pool[rand() % pool.size()];
        at<int>(b, Board_mAlienTimer) = std::max(1200, 3000 - wave * 100);
        char t[32];
        snprintf(t, sizeof t, "Wave %d!", wave);
        api->toast(t);
    }
}

// ---- the screens ---------------------------------------------------------------------------------------------------------
static const int MenuId = 0x4a, RecordsId = 0x4b;
static const int DX = 30, DY = 20, DW = 580, DH = 440, CX = DX + 40, CW = DW - 80;
static int screen;   // 0 none, MenuId, RecordsId
static int recordsPage;
static bool recordsFromMenu;

struct Row { const char* title; std::string line, best; int action; };
static std::vector<Row> Rows()
{
    std::vector<Row> r;
    int key = Today();
    auto describe = Export<const char* (*)(int)>("mutators.dll", "MutatorsDescribe");
    long long daily = Record("daily_" + std::to_string(key)), boss = Record("bossrush_best"), endless = Record("endless_best");
    r.push_back({ "Daily Challenge", "Today: tank " + std::to_string(DailyTank(key)) + " against the clock, with " + (describe ? describe(DailyMutators(key)) : "two mutators"),
                  daily > 0 ? "Today's best " + Clock(daily) : "Not played today", 1 });
    r.push_back({ "Boss Rush", "Every alien in turn, each tougher than the last. $3,000 and a better laser to start.", boss > 0 ? "Best " + Clock(boss) : "No clear yet", 2 });
    r.push_back({ "Endless", "Alien waves never stop, sooner and tougher each time.", endless > 0 ? "Best " + std::to_string(endless) + " waves" : "No run yet", 3 });
    if (Export<void (*)()>("sandbox.dll", "SandboxStart")) r.push_back({ "Sandbox", "Build your own tank: place fish, pets and aliens, save layouts.", "", 4 });
    if (Export<void (*)()>("alienplay.dll", "AlienPlayStart")) r.push_back({ "Play as Alien", "The tables turned: steer the alien and empty the tank.", "", 5 });
    r.push_back({ "Records", "Your bests and the Hall of Fame for Hard mode and Endless.", "", 6 });
    return r;
}
static RECT RowRect(int i) { return { CX, DY + 72 + i * 47, CX + CW, DY + 72 + i * 47 + 44 }; }

static void OpenScreen(int id, const char* title)
{
    if (screen || ui::DialogCount(api) > 0) return;
    LoadRecords();
    if (ui::OpenDialog(api, id, title, id == MenuId ? "CANCEL" : "BACK", DX, DY, DW, DH)) screen = id;
}

static void __fastcall ButtonHook(void* a, void*, int id)
{
    if (id == MenuId + 2000 || id == MenuId + 3000) { ui::KillDialog(api, MenuId); screen = 0; return; }
    if (id == RecordsId + 2000 || id == RecordsId + 3000)   // back to Extra Modes when it came from there
    { ui::KillDialog(api, RecordsId); screen = 0; if (recordsFromMenu) OpenScreen(MenuId, "EXTRA MODES"); return; }
    oButton(a, id);
}

static void Choose(int action)
{
    ui::KillDialog(api, MenuId);
    screen = 0;
    switch (action)
    {
        case 1: Start(Daily); break;
        case 2: Start(BossRush); break;
        case 3: Start(Endless); break;
        case 4: if (auto f = Export<void (*)()>("sandbox.dll", "SandboxStart")) f(); break;
        case 5: if (auto f = Export<void (*)()>("alienplay.dll", "AlienPlayStart")) f(); break;
        case 6: recordsFromMenu = true; OpenScreen(RecordsId, "RECORDS"); break;
    }
}

static int Mouse(int x, int y, int button, int down)
{
    if (!screen)
    {
        return 0;
    }
    if (!ui::GetDialog(api, screen)) { screen = 0; return 0; }
    if (y >= DY + DH - 70) return 0;
    if (down && button == 0)
    {
        if (screen == MenuId)
        {
            auto rows = Rows();
            for (size_t i = 0; i < rows.size(); i++) if (ui::In(RowRect((int)i), x, y)) { Choose(rows[i].action); return 1; }
        }
        else
        {
            for (int i = 0; i < 3; i++)
                if (ui::In(RECT{ CX + i * (CW / 3), DY + 74, CX + (i + 1) * (CW / 3) - 6, DY + 100 }, x, y)) { recordsPage = i; api->redraw(); }
        }
    }
    return 1;
}

static void DrawMenu(void* g)
{
    void* f12 = ui::Font(FONT_JUNGLEFEVER12OUTLINE), *f10 = ui::Font(FONT_JUNGLEFEVER10OUTLINE);
    int mx, my;
    api->mouse_pos(&mx, &my);
    auto rows = Rows();
    for (size_t i = 0; i < rows.size(); i++)
    {
        // the mode as one of the game's wide buttons, what it is beside it (the whole row is the button)
        RECT r = RowRect((int)i);
        bool over = ui::In(r, mx, my);
        RECT b = { r.left, r.top, r.left + 170, r.bottom };
        void* img = ui::Image(IMAGE_FATBUTTON);
        int cw = ui::ImgW(img) / 3, ch = ui::ImgH(img);
        RECT bb = { b.left, b.top + (ui::H(b) - ch) / 2, b.right, b.top + (ui::H(b) - ch) / 2 + ch };
        bool down = over && ui::Pressed();
        ui::ImageBox(api, g, img, down ? cw * 2 : over ? cw : 0, 0, cw, ch, bb);
        ui::Label(api, g, f12, rows[i].title, bb, over ? ui::White : ui::Yellow, down ? 1 : 0, down ? 1 : -1);
        int tx = b.right + 10, tw = r.right - tx;
        if (over) ui::Tooltip(rows[i].best.empty() ? rows[i].line : rows[i].line + "\n" + rows[i].best);   // the whole text, whatever was cut
        if (!rows[i].best.empty())
        {
            ui::FitText(api, g, f10, rows[i].line, tx, r.top + 18, tw, ui::White);
            ui::FitText(api, g, f10, rows[i].best, tx, r.top + 34, tw, 0xff9cf09c);
        }
        else ui::WrapText(api, g, f10, rows[i].line, tx, r.top + 18, tw, ui::White, 2);
    }
}

static void DrawRecords(void* g)
{
    void* f12 = ui::Font(FONT_JUNGLEFEVER12OUTLINE), *f10 = ui::Font(FONT_JUNGLEFEVER10OUTLINE);
    const char* tabs[3] = { "Your bests", "Hard mode", "Endless" };
    for (int i = 0; i < 3; i++) ui::Button(api, g, RECT{ CX + i * (CW / 3), DY + 72, CX + (i + 1) * (CW / 3) - 6, DY + 101 }, tabs[i], ui::Look::Main, true, i == recordsPage);
    int y = DY + 128;
    LoadFame();
    auto line = [&](const std::string& a, const std::string& b, unsigned col = 0xffffffff) {
        api->draw_text_font(g, f12, a.c_str(), CX + 10, y, col);
        api->draw_text_font(g, f12, b.c_str(), CX + CW - 10 - api->text_width_font(f12, b.c_str()), y, col);
        y += 24;
    };
    auto table = [&](const std::string& t, bool money, bool time) {
        int n = 0;
        for (auto& e : fame)
            if (e.table == t)
            {
                n++;
                std::string v = time ? Clock(e.value) : money ? "$" + std::to_string(e.value) : std::to_string(e.value) + " waves";
                api->draw_text_font(g, f10, (std::to_string(n) + ". " + e.name).c_str(), CX + 30, y, 0xffffffff);
                api->draw_text_font(g, f10, v.c_str(), CX + CW - 10 - api->text_width_font(f10, v.c_str()), y, 0xffffe8a0);
                y += 16;
            }
        if (!n) { api->draw_text_font(g, f10, "-", CX + 30, y, 0xffc0c0c0); y += 16; }
    };
    if (recordsPage == 0)
    {
        long long d = Record("daily_" + std::to_string(Today())), b = Record("bossrush_best"), e = Record("endless_best");
        line("Daily challenge today", d > 0 ? Clock(d) : "-");
        line("Boss rush", b > 0 ? Clock(b) : "-");
        line("Endless", e > 0 ? std::to_string(e) + " waves" : "-");
        int days = 0;
        for (auto& r : records) if (r.first.rfind("daily_", 0) == 0) days++;
        line("Daily challenges finished", std::to_string(days));
    }
    else if (recordsPage == 1)
    {
        api->draw_text_font(g, f10, "Games with the Hard mode mutator (not with Rich start, not boss rush).", CX + 10, y, 0xffffe8a0);
        y += 22;
        for (int t = 1; t <= 4; t++)
        {
            api->draw_text_font(g, f12, ("Tank " + std::to_string(t) + ": Time Trial money / Challenge time").c_str(), CX + 10, y, 0xfffff000);
            y += 18;
            int y0 = y;
            table("hardtt" + std::to_string(t), true, false);
            int y1 = y; y = y0;
            table("hardch" + std::to_string(t), false, true);
            y = std::max(y, y1) + 4;
            if (y > DY + DH - 90) break;
        }
    }
    else
    {
        api->draw_text_font(g, f12, "Endless: most waves survived", CX + 10, y, 0xfffff000);
        y += 20;
        table("endless", false, false);
        y += 10;
        api->draw_text_font(g, f12, "Boss rush: fastest clears", CX + 10, y, 0xfffff000);
        y += 20;
        table("bossrush", false, true);
    }
}

static void Overlay(void* g)
{
    if (screen)
    {
        if (!ui::GetDialog(api, screen)) { screen = 0; return; }
        if (screen == MenuId) DrawMenu(g); else DrawRecords(g);
        ui::DrawTooltip(api, g);
        return;
    }

    // the wave counter in Endless, the stage in Boss Rush
    void* b = api->board();
    if (b && Active() && !finished && !at<bool>(b, Board_mPaused) && ui::DialogCount(api) == 0 && mode != Daily)
    {
        bool aliens = Count(b, Board_mAliens) > 0 || Count(b, Board_mBilaterus) > 0;
        char s[64];
        if (mode == Endless) snprintf(s, sizeof s, aliens ? "Endless - wave %d" : "Endless - wave %d in %d s", aliens ? wave : wave + 1, at<int>(b, Board_mAlienTimer) * 28 / 1000);
        else snprintf(s, sizeof s, "Boss rush - %d / %d", std::min(stage + (aliens ? 1 : 0), Stages), Stages);
        void* f = ui::Font(FONT_JUNGLEFEVER12OUTLINE);
        int w = api->text_width_font(f, s) + 14;
        api->fill_rect(g, 632 - w, 76, w, 20, 0x90000000);
        api->draw_text_font(g, f, s, 639 - w, 91, aliens ? 0xffff9c9c : 0xffffe880);
    }
}

// from the main menu's Remastered page
extern "C" __declspec(dllexport) void ExtraModesOpen() { OpenScreen(MenuId, "EXTRA MODES"); }
extern "C" __declspec(dllexport) void RemodOpen() { ExtraModesOpen(); }   // the Open button on the settings' Mods tab
extern "C" __declspec(dllexport) void RecordsOpen() { recordsFromMenu = false; OpenScreen(RecordsId, "RECORDS"); }

extern "C" __declspec(dllexport) int ModeActive() { return Active() ? 1 : 0; }   // for the content mod

extern "C" __declspec(dllexport) const char* RemodDescribe() { return "Extra Modes on the main menu: daily challenge, boss rush, endless, records."; }

extern "C" __declspec(dllexport) int RemodInit(const RemodApi* a)
{
    if (a->version < 3) return 0;
    api = a;
    userdata = ui::UserData();   // the game's own save folder (ProgramData\Steam\Insaniquarium\userdata for the Steam release)
    bool ok = api->hook((void*)Board_StartLevel, (void*)&StartLevel, (void**)&oStartLevel)
           && api->hook((void*)Alien_ctor, (void*)&AlienCtor, (void**)&oAlienCtor)
           && api->hook((void*)Board_BuyItem, (void*)&BuyItem, (void**)&oBuyItem)
           && api->hook((void*)App_DoTimedDialog, (void*)&DialogHook, (void**)&oDialog)
           && api->hook((void*)Board_BankCoins, (void*)&Bank, (void**)&oBank)
           && api->hook((void*)HighScore_AddTimeTrial, (void*)&AddTimeTrial, (void**)&oAddTimeTrial)
           && api->hook((void*)App_ShowGameSelector, (void*)&Selector, (void**)&oSelector)
           && api->hook((void*)App_ButtonDepress, (void*)&ButtonHook, (void**)&oButton);
    if (!ok) return 0;
    api->on_mouse(Mouse);
    api->on_overlay(Overlay);
    api->on_tick(Ticked);
    return 1;
}
