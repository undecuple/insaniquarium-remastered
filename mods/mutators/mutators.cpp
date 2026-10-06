// mutators: optional rule changes for Adventure, Time Trial and Challenge levels (never the Virtual Tank, the sandbox
// or the screensaver). Switched on in [mutators] in mods/remastered-mod.ini (1 = on), read at the start of each level:
//   hungry       fish get hungry twice as fast
//   double       every alien arrives with a twin
//   glass        your laser hits twice as hard, but aliens are faster
//   nopets       no pets come into the tank
//   pacifist     your laser can't hurt aliens (pets and food only)
//   heavycoins   coins sink faster
//   tinywallet   you can't hold more than $2,500
//   richstart    start each level with $1,000
//   hard         prices and alien health +50%, aliens come sooner, fish hungrier
//   golden       1 in 50 guppies you buy is golden: coins twice as often
//   combos       quick coin pickups build a bonus of up to double value
//   events       feeding frenzies, power cuts and currents now and then
//   overeat      feeding a full fish stuffs it (no coins for a while); three times and it pops
//   decay        dead fish float until you click them away, and make the others hungry
//   gadgets      buy an auto-feeder, a coin magnet and an alien alarm in each tank (panel top left)
#include "remod.h"
#include "game.h"
#include "remodui.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <algorithm>

using namespace game;
static const RemodApi* api;

enum { Hungry = 1, Double = 2, Glass = 4, NoPets = 8, Pacifist = 16, Heavy = 32, Tiny = 64, Rich = 128, Hard = 256, Golden = 512, Combos = 1024,
       Events = 2048, Overeat = 4096, Decay = 8192, Gadgets = 16384 };
static const struct { int flag; const char* key; } Keys[] = {
    { Hungry, "hungry" }, { Double, "double" }, { Glass, "glass" }, { NoPets, "nopets" }, { Pacifist, "pacifist" },
    { Heavy, "heavycoins" }, { Tiny, "tinywallet" }, { Rich, "richstart" }, { Hard, "hard" }, { Golden, "golden" },
    { Combos, "combos" }, { Events, "events" }, { Overeat, "overeat" }, { Decay, "decay" }, { Gadgets, "gadgets" },
};
static int active;          // this level's set
static bool inStartLevel, spawningTwin;
static std::vector<void*> golden;   // golden guppies of this board
static void* goldenBoard;
static int lastPrice[12], lastAlienTimer, comboCount, comboLastTick = -1000, goldenOdds = 50;
static bool comboShown;
// tank events
enum { EvNone, EvFrenzy, EvLightsOut, EvCurrent };
static int eventKind, eventLeft, eventTimer = 6000;
// overeating: per fish, how many times in a row it was fed full and how long it makes no coins
struct Stuffed { void* fish; int count, timer; };
static std::vector<Stuffed> stuffed;
// decay: corpses that float until clicked
static std::vector<void*> decaying;
// gadgets
static bool feeder, magnet, alarm;
static const RECT FeederRect = { 0x26, 0x4c, 0x26 + 0x8c, 0x4c + 0x16 }, MagnetRect = { 0x26, 0x66, 0x26 + 0x8c, 0x66 + 0x16 },
                  AlarmRect = { 0x26, 0x80, 0x26 + 0x8c, 0x80 + 0x16 };
static const int FeederPrice = 750, MagnetPrice = 1500, AlarmPrice = 400;
enum { SOUND_BUY = 273, SOUND_SONAR = 316, SOUND_SPLASH = 317 };

static bool On(int f) { return (active & f) != 0; }
static bool Applies()
{
    void* app = api->app();
    return app && at<int>(app, App_mGameMode) != 5 && !at<bool>(app, App_mIsScreenSaver);
}
static bool OnHere(int f) { return On(f) && Applies(); }

typedef void(__thiscall* VoidFn)(void*);
typedef void(__thiscall* IntFn)(void*, int);
typedef void*(__thiscall* AddPetFn)(void*, int, int, int, bool, bool);
typedef void(__thiscall* SpawnAlienFn)(void*, int, int, int, bool);
typedef void*(__thiscall* AlienCtorFn)(void*, int, int, int);
typedef bool(__thiscall* ShootFn)(void*, int, int);
typedef void(__thiscall* FlashFn)(void*, bool);
static VoidFn oInitLevel, oStartLevel, oTickHunger, oCoinUpdate, oDropCoin;
static IntFn oAddMoney, oBuyItem;
static AddPetFn oAddPet;
static SpawnAlienFn oSpawnAlien;
static AlienCtorFn oAlienCtor;
static ShootFn oShoot;
static void(__thiscall* oFishEat)(void*, void*);
static void(__thiscall* oAddDeadFish)(void*, int, int, double, double, double, int, bool, void*);
static VoidFn oDeadFishUpdate;
static void(__thiscall* oWidgetMouseDown)(void*, int, int, int);
static void(__thiscall* oCoinMouseDown)(void*, int, int, int);

// prices and alien health: hard mode (x1.5) times the co-op scaling for more players (set by the co-op mod)
static double coopPrice = 1, coopHealth = 1;
static double PriceScale() { return (On(Hard) ? 1.5 : 1) * coopPrice; }
static double HealthScale() { return (On(Hard) ? 1.5 : 1) * coopHealth; }
extern "C" __declspec(dllexport) void MutatorsSetCoopScale(double price, double health) { coopPrice = price; coopHealth = health; }
static int Scaled(int p) { return std::min((int)((p * PriceScale()) / 5 + 0.5) * 5, 99999); }

// other mods (the daily challenge) can choose the set for the next level, and read the current one
static int forced = -1;
extern "C" __declspec(dllexport) void MutatorsForce(int flags) { forced = flags; }
extern "C" __declspec(dllexport) int MutatorsActive() { return active; }
// co-op: a player joining a game in progress gets this level's mutator state (everything but the object lists)
struct SavedState { int active, lastPrice[12], lastAlienTimer, comboCount, comboLastTick, goldenOdds, eventKind, eventLeft, eventTimer; bool comboShown, feeder, magnet, alarm; double coopPrice, coopHealth; };
extern "C" __declspec(dllexport) int MutatorsSave(void* buf, int cap)
{
    SavedState st{ active, {}, lastAlienTimer, comboCount, comboLastTick, goldenOdds, eventKind, eventLeft, eventTimer, comboShown, feeder, magnet, alarm, coopPrice, coopHealth };
    memcpy(st.lastPrice, lastPrice, sizeof lastPrice);
    if (cap < (int)sizeof st) return 0;
    memcpy(buf, &st, sizeof st);
    return sizeof st;
}
extern "C" __declspec(dllexport) void MutatorsLoad(const void* buf, int len)
{
    SavedState st;
    if (len != (int)sizeof st) return;
    memcpy(&st, buf, sizeof st);
    active = st.active; memcpy(lastPrice, st.lastPrice, sizeof lastPrice); lastAlienTimer = st.lastAlienTimer; comboCount = st.comboCount;
    comboLastTick = st.comboLastTick; goldenOdds = st.goldenOdds; eventKind = st.eventKind; eventLeft = st.eventLeft; eventTimer = st.eventTimer;
    comboShown = st.comboShown; feeder = st.feeder; magnet = st.magnet; alarm = st.alarm; coopPrice = st.coopPrice; coopHealth = st.coopHealth;
    forced = -1;
}
// co-op reloaded the board from a snapshot on every machine at once: the old objects are gone (the rest stays)
extern "C" __declspec(dllexport) void MutatorsBoardReloaded(void* b) { golden.clear(); goldenBoard = b; stuffed.clear(); decaying.clear(); }
// the set's names, for showing it (flags as above): "Hungry fish, Tiny wallet"
extern "C" __declspec(dllexport) const char* MutatorsDescribe(int flags)
{
    static const char* names[] = { "Hungry fish", "Double trouble", "Glass cannon", "No pets", "Pacifist", "Heavy coins", "Tiny wallet",
                                   "Rich start", "Hard mode", "Golden guppies", "Coin combos", "Tank events", "Overeating", "Decay", "Gadgets" };
    static char out[256];
    out[0] = 0;
    for (int i = 0; i < 15; i++)
        if (flags & (1 << i)) { if (out[0]) strcat(out, ", "); strcat(out, names[i]); }
    if (!out[0]) strcpy(out, "none");
    return out;
}

// the player's chosen set (the settings), for the co-op host to send to everyone
extern "C" __declspec(dllexport) int MutatorsFromSettings(int) { int f = 0; for (auto& k : Keys) if (api->config_int("mutators", k.key, 0)) f |= k.flag; return f; }

// random numbers from the game's own generator, so every co-op player gets the same (and replays stay the same)
static int GameRand() { void* a = api->app(); return (int)(reinterpret_cast<uint32_t(__thiscall*)(void*)>(MTRand_Next)(at<void*>(a, App_mMTRand)) & 0x7fffffff); }

static void __fastcall InitLevel(void* b, void*)
{
    active = 0;
    if (forced >= 0) { active = forced; forced = -1; }
    else for (auto& k : Keys) if (api->config_int("mutators", k.key, 0)) active |= k.flag;
    golden.clear(); goldenBoard = b; comboCount = 0; comboLastTick = -1000;
    eventKind = EvNone; eventLeft = 0; eventTimer = api->config_int("mutators", "firstevent", 6000); stuffed.clear(); decaying.clear(); feeder = magnet = alarm = false;
    oInitLevel(b);
    void* app = api->app();
    for (int i = 0; i < 12; i++) lastPrice[i] = 0;
    lastAlienTimer = 0;
    if (!Applies() || !active) return;
    if (On(Rich) && at<int>(app, App_mGameMode) != 3) { at<int>(b, Board_mMoney) = 1000; reinterpret_cast<VoidFn>(Board_UpdateMoneyLabel)(b); }
    api->log("mutators: level starts with set %d", active);
}

static void __fastcall StartLevel(void* b, void*) { inStartLevel = true; oStartLevel(b); inStartLevel = false; }

static void* __fastcall AddPet(void* b, void*, int pet, int x, int y, bool special, bool notVirtual)
{
    if (inStartLevel && OnHere(NoPets)) return nullptr;
    return oAddPet(b, pet, x, y, special, notVirtual);
}

static void __fastcall AddMoney(void* b, void*, int amount)
{
    if (OnHere(Tiny) && !at<bool>(b, Board_mBonusRound) && amount > 0)
        amount = std::max(0, std::min(amount, 2500 - at<int>(b, Board_mMoney)));
    oAddMoney(b, amount);
}

static void __fastcall SpawnAlien(void* b, void*, int type, int x, int y, bool sound)
{
    oSpawnAlien(b, type, x, y, sound);
    if (OnHere(Double) && !spawningTwin && type != 8 && (type < 9 || type > 12) && type != 0x15)
    {
        spawningTwin = true;
        oSpawnAlien(b, type, std::min(std::max(0x230 - x, 0x14), 0x1d6), y, false);
        spawningTwin = false;
    }
}

static void* __fastcall AlienCtor(void* a, void*, int x, int y, int type)
{
    oAlienCtor(a, x, y, type);
    if (Applies())
    {
        if (HealthScale() != 1) { at<double>(a, Alien_mHealth) *= HealthScale(); at<double>(a, Alien_mMaxHealth) = at<double>(a, Alien_mHealth); }
        if (On(Glass)) at<double>(a, Alien_mSpeedDiv) /= 1.3;
    }
    return a;
}

static bool __fastcall Shoot(void* a, void*, int x, int y)
{
    if (!Applies()) return oShoot(a, x, y);
    if (On(Pacifist)) return false;
    void* b = api->board();
    if (On(Glass) && b && at<int>(a, Alien_mAlienType) != 7)   // not the psychosquid (shooting it can heal it)
    {
        int& level = at<int>(b, Board_mWeaponLevel);
        int saved = level;
        level *= 2;
        bool r = oShoot(a, x, y);
        level = saved;
        return r;
    }
    return oShoot(a, x, y);
}

static void __fastcall TickHunger(void* o, void*)
{
    oTickHunger(o);
    if (!active || at<int>(o, GameObject_mSongId) != -1 || !Applies()) return;
    void* b = api->board();
    int tick = b ? at<int>(b, Board_mTick) : 0;
    int extra = (On(Hungry) && (tick & 1) == 0 ? 1 : 0) + (On(Hard) && tick % 3 == 0 ? 1 : 0);
    int dead = On(Decay) && b ? Count(b, Board_mDeadFish) : 0;
    if (dead > 0 && tick % std::max(2, 8 - dead) == 0) extra++;   // corpses make the others hungry
    for (; extra > 0; extra--)
    {
        int& t = at<int>(o, GameObject_mHungerTimer);
        t--;
        if (t == at<int>(o, GameObject_mHungryThreshold) + 4) reinterpret_cast<FlashFn>(GameObject_SetHungryFlash)(o, true);
        if (t < -1000) t = -1000;
    }
}

static void __fastcall CoinUpdate(void* c, void*)
{
    double y0 = at<double>(c, Coin_mYD);
    oCoinUpdate(c);
    void* b = api->board();
    if (OnHere(Heavy) && b && !at<bool>(b, Board_mBonusRound) && at<double>(c, Coin_mYD) - y0 == 1.5) at<double>(c, Coin_mYD) += 0.9;
}

// coin combos: a coin picked up within 150 ticks of the last pays 10% more per step (up to double)
static void __fastcall CoinMouseDown(void* c, void*, int x, int y, int clicks)
{
    bool was = at<bool>(c, Coin_mCollected);
    oCoinMouseDown(c, x, y, clicks);
    void* b = api->board();
    if (!OnHere(Combos) || !b || was || !at<bool>(c, Coin_mCollected) || at<int>(c, Coin_mCoinType) >= 0xf) return;
    int tick = at<int>(b, Board_mTick);
    comboCount = tick - comboLastTick <= 150 ? comboCount + 1 : 1;
    comboLastTick = tick;
    int steps = std::min(comboCount - 1, 10);
    int value = reinterpret_cast<int(__thiscall*)(void*)>(Coin_GetValue)(c);
    if (steps > 0 && value > 0) oAddMoney(b, std::max(1, value * steps / 10));
}

static void __fastcall BuyItem(void* b, void*, int item)
{
    int before = Count(b, Board_mGuppies);
    oBuyItem(b, item);
    if (item == 0 && OnHere(Golden) && Count(b, Board_mGuppies) > before && GameRand() % goldenOdds == 0)
    {
        golden.push_back(Item(b, Board_mGuppies, Count(b, Board_mGuppies) - 1));
        api->toast("A golden guppy! Its coins come twice as often.");
    }
}

static Stuffed* StuffedOf(void* f)
{
    for (auto& s : stuffed) if (s.fish == f) return &s;
    return nullptr;
}

static void FloatingText(void* b, int x, int y, const char* text)
{
    MsvcString t = MakeString(text);
    reinterpret_cast<void*(__thiscall*)(void*, int, int, int, const MsvcString*)>(Board_AddTextCoin)(b, x, y, 2, &t);
}

// overeating: fed while full (hunger timer > 600) -> stuffed: no coins for 1000 ticks; the third time in a row it pops
static void __fastcall FishEat(void* f, void*, void* food)
{
    bool full = at<int>(f, GameObject_mHungerTimer) > 600;
    oFishEat(f, food);
    void* b = api->board();
    if (!OnHere(Overeat) || !b || !food || at<int>(food, 0x194) == 2 || at<int>(food, 0x188) == 3) return;   // not potions / special food
    Stuffed* s = StuffedOf(f);
    if (!full) { if (s) s->count = 0; return; }
    if (!s) { stuffed.push_back({ f, 0, 0 }); s = &stuffed.back(); }
    int x = at<int>(f, Widget_mX) + 20, y = at<int>(f, Widget_mY);
    if (++s->count >= 3)
    {
        FloatingText(b, x, y, "POP!");
        s->fish = nullptr;
        void** vt = *reinterpret_cast<void***>(f);
        reinterpret_cast<void(__thiscall*)(void*, bool)>(vt[GameObject_vDie / 4])(f, false);
        return;
    }
    s->timer = 1000;
    FloatingText(b, x, y, "Stuffed!");
}

// decay: new corpses float up and can be clicked away
static void __fastcall AddDeadFish(void* b, void*, int x, int y, double vx, double vy, double speed, int size, bool right, void* shadow)
{
    int before = Count(b, Board_mDeadFish);
    oAddDeadFish(b, x, y, vx, vy, speed, size, right, shadow);
    if (!OnHere(Decay) || Count(b, Board_mDeadFish) <= before) return;
    void* d = Item(b, Board_mDeadFish, Count(b, Board_mDeadFish) - 1);
    at<bool>(d, DeadFish_mFloatUp) = true;
    at<bool>(d, Widget_mMouseVisible) = true;
    decaying.push_back(d);
}

static bool IsDecaying(void* d) { return std::find(decaying.begin(), decaying.end(), d) != decaying.end(); }

static void __fastcall DeadFishUpdate(void* d, void*)
{
    oDeadFishUpdate(d);
    // never fades away by itself (the death animation still plays)
    if (!decaying.empty() && IsDecaying(d) && at<int>(d, DeadFish_mLife) < 0x69) at<int>(d, DeadFish_mLife) = 0x69;
}

static void __fastcall WidgetMouseDown(void* w, void*, int x, int y, int clicks)
{
    if (*reinterpret_cast<uintptr_t*>(w) == DeadFish_vtable && IsDecaying(w))
    {
        decaying.erase(std::find(decaying.begin(), decaying.end(), w));
        api->play_sound(SOUND_SPLASH);
        reinterpret_cast<void(__thiscall*)(void*)>(DeadFish_Remove)(w);
        return;
    }
    oWidgetMouseDown(w, x, y, clicks);
}

static bool IsGolden(void* f)
{
    if (golden.empty() || goldenBoard != api->board()) return false;
    return std::find(golden.begin(), golden.end(), f) != golden.end();
}

static void __fastcall DropCoin(void* f, void*)
{
    if (!stuffed.empty())
        if (Stuffed* s = StuffedOf(f)) if (s->timer > 0) { s->timer--; return; }   // stuffed: no coins
    oDropCoin(f);
    if (IsGolden(f)) oDropCoin(f);   // the coin timer runs twice as fast
}

static bool HasAliens(void* b) { return Count(b, Board_mAliens) > 0 || Count(b, Board_mBilaterus) > 0; }

static void TankEvents(void* b)
{
    if (eventLeft > 0)
    {
        eventLeft--;
        if (eventKind == EvFrenzy && eventLeft % 15 == 0) reinterpret_cast<VoidFn>(Board_DropBonusCoins)(b);
        if (eventKind == EvCurrent)
        {
            // a strong current pushes food and falling coins to the right
            for (int i = 0; i < Count(b, Board_mFood); i++) { double& x = at<double>(Item(b, Board_mFood, i), Food_mXD); if (x < 590) x += 0.8; }
            for (int i = 0; i < Count(b, Board_mCoins); i++)
            {
                void* c = Item(b, Board_mCoins, i);
                double& x = at<double>(c, Coin_mXD);
                if (x < 570 && !at<bool>(c, Coin_mCollected)) x += 0.6;
            }
        }
        if (eventLeft == 0) { eventKind = EvNone; api->redraw(); }
        return;
    }
    if (--eventTimer > 0 || HasAliens(b)) return;
    static int forced = api->config_int("mutators", "testevent", 0);   // for testing: the first event's kind
    eventKind = forced ? forced : GameRand() % 3 + 1;
    forced = 0;
    eventLeft = eventKind == EvFrenzy ? 600 : 1500;
    api->toast(eventKind == EvFrenzy ? "Feeding frenzy! Coins are raining in!"
             : eventKind == EvLightsOut ? "Lights out! Keep an eye on your fish..." : "A strong current is sweeping through the tank!");
    eventTimer = 6000 + GameRand() % 6000;   // the next one in 60-120 s
}

static int Price(int p) { return PriceScale() != 1 ? Scaled(p) : p; }

static void GadgetsTick(void* b)
{
    int tick = at<int>(b, Board_mTick);
    if (feeder && tick % 400 == 0)   // a free pellet over a hungry fish
    {
        const int lists[] = { Board_mGuppies, Board_mOscars, Board_mUltras, Board_mGekkos, Board_mPentas, Board_mGrubbers, Board_mBreeders };
        for (int l : lists)
            for (int i = 0; i < Count(b, l); i++)
            {
                void* f = Item(b, l, i);
                if (!reinterpret_cast<bool(__thiscall*)(void*)>(GameObject_IsHungry)(f)) continue;
                int x = std::min(std::max(at<int>(f, Widget_mX) + 20, 0x28), 0x230);
                reinterpret_cast<void(__thiscall*)(void*, int, int, int, bool, int, int)>(Board_DropFood)(b, x, 0x50, 0, true, 0x14, -1);
                goto fed;
            }
    }
fed:
    if (magnet)   // coins that reach the bottom are picked up
        for (int i = Count(b, Board_mCoins) - 1; i >= 0; i--)
        {
            void* c = Item(b, Board_mCoins, i);
            if (at<bool>(c, Coin_mCollected) || at<int>(c, Coin_mCoinType) >= 0xf || at<double>(c, Coin_mYD) < 369.9) continue;
            if (reinterpret_cast<int(__thiscall*)(void*)>(Coin_GetValue)(c) <= 0) continue;
            oCoinMouseDown(c, at<int>(c, Widget_mX) + 20, at<int>(c, Widget_mY) + 20, 1);
        }
    // the alarm pings 10 s before an alien
    if (alarm && at<int>(b, Board_mAlienTimer) == 1000 && !HasAliens(b)) api->play_sound(SOUND_SONAR);
}

static bool In(const RECT& r, int x, int y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

static int Mouse(int x, int y, int button, int down)
{
    void* b = api->board();
    if (!down || button != 0 || !b || !OnHere(Gadgets) || at<bool>(b, Board_mPaused) || at<bool>(b, Board_mBonusRound)) return 0;
    if (at<int>(api->app(), App_mDialogCount) > 0) return 0;
    bool* which = In(FeederRect, x, y) ? &feeder : In(MagnetRect, x, y) ? &magnet : In(AlarmRect, x, y) ? &alarm : nullptr;
    if (!which) return 0;
    int price = Price(which == &feeder ? FeederPrice : which == &magnet ? MagnetPrice : AlarmPrice);
    if (!*which && reinterpret_cast<bool(__thiscall*)(void*, int, bool)>(Board_SpendMoney)(b, price, true))
    {
        *which = true;
        api->play_sound(SOUND_BUY);
        api->redraw();
    }
    return 1;
}

static void Pill(void* g, const RECT& r, const char* text, bool owned)   // the game's pill button, held down once bought
{
    ui::Button(api, g, r, text, ui::Look::Main, true, owned);
}

static void Tick(void* b)
{
    if ((!active && coopPrice == 1) || !Applies()) return;
    // drop golden guppies that are gone (eaten, starved)
    if (!golden.empty())
        golden.erase(std::remove_if(golden.begin(), golden.end(), [&](void* f) {
            for (int i = 0; i < Count(b, Board_mGuppies); i++) if (Item(b, Board_mGuppies, i) == f) return false;
            return true; }), golden.end());
    bool bonus = at<bool>(b, Board_mBonusRound), paused = at<bool>(b, Board_mPaused);
    if (On(Events) && !bonus && !paused) TankEvents(b);
    if (On(Gadgets) && !bonus && !paused) GadgetsTick(b);
    if (!stuffed.empty())   // forget fish that are gone
        stuffed.erase(std::remove_if(stuffed.begin(), stuffed.end(), [&](const Stuffed& st) {
            for (int i = 0; st.fish && i < Count(b, Board_mGuppies); i++) if (Item(b, Board_mGuppies, i) == st.fish) return false;
            return true; }), stuffed.end());
    if (!decaying.empty())   // forget corpses that are gone
        decaying.erase(std::remove_if(decaying.begin(), decaying.end(), [&](void* d) {
            for (int i = 0; i < Count(b, Board_mDeadFish); i++) if (Item(b, Board_mDeadFish, i) == d) return false;
            return true; }), decaying.end());
    if (PriceScale() != 1)
    {
        // prices: rescale each store price whenever the game sets a new one
        int* price = &at<int>(b, Board_mPrice);
        for (int i = 0; i < 12; i++)
            if (price[i] > 0 && price[i] != lastPrice[i]) { price[i] = Scaled(price[i]); lastPrice[i] = price[i]; }
    }
    if (On(Hard))
    {
        // aliens sooner: the first after 1500 ticks instead of 3000, then every 2200
        int& t = at<int>(b, Board_mAlienTimer);
        if (t == 3000 && lastAlienTimer != 3000) t = at<int>(b, Board_mTick) < 10 ? 1500 : 2200;
        lastAlienTimer = t;
    }
}

static void Overlay(void* g)
{
    void* b = api->board();
    if (!b) return;
    bool combo = OnHere(Combos) && comboCount >= 2 && at<int>(b, Board_mTick) - comboLastTick <= 150 && !at<bool>(b, Board_mPaused);
    if (comboShown && !combo) api->redraw();
    comboShown = combo;
    if (combo)
    {
        char s[32];
        wsprintfA(s, "Combo x%d  +%d%%", comboCount, std::min(comboCount - 1, 10) * 10);
        int w = api->text_width(s) + 12;
        api->fill_rect(g, 320 - w / 2, 76, w, 18, 0x90000000);
        api->draw_text(g, s, 320 - w / 2 + 6, 90, 0xffffe060);
    }
    bool clear = !at<bool>(b, Board_mPaused) && at<int>(api->app(), App_mDialogCount) == 0;
    if (clear && OnHere(Gadgets) && !at<bool>(b, Board_mBonusRound))
    {
        char t[48];
        wsprintfA(t, feeder ? "Auto-feeder ON" : "Auto-feeder $%d", Price(FeederPrice)); Pill(g, FeederRect, t, feeder);
        wsprintfA(t, magnet ? "Coin magnet ON" : "Coin magnet $%d", Price(MagnetPrice)); Pill(g, MagnetRect, t, magnet);
        int timer = at<int>(b, Board_mAlienTimer);
        if (alarm && !HasAliens(b) && timer > 0 && timer * 28 <= 30000) wsprintfA(t, "Alien in %ds", timer * 28 / 1000);
        else wsprintfA(t, alarm ? "Alien alarm ON" : "Alien alarm $%d", Price(AlarmPrice));
        Pill(g, AlarmRect, t, alarm);
    }
    if (clear && eventKind == EvLightsOut && eventLeft > 0)
    {
        // lights out: the tank goes dark except a square torch around the pointer
        int fade = std::min(std::min(eventLeft, 1500 - eventLeft), 60), a = 215 * fade / 60, mx, my, r = 80, top = 0x46;
        api->mouse_pos(&mx, &my);
        unsigned dark = (unsigned)a << 24 | 0x00000a, soft = (unsigned)(a * 2 / 3) << 24 | 0x00000a;
        int l = std::max(mx - r, 0), rt = std::min(mx + r, 640), t = std::max(my - r, top), bt = std::min(my + r, 480);
        api->fill_rect(g, 0, top, 640, std::max(t - top, 0), dark);
        api->fill_rect(g, 0, bt, 640, 480 - bt, dark);
        api->fill_rect(g, 0, t, l, bt - t, dark);
        api->fill_rect(g, rt, t, 640 - rt, bt - t, dark);
        api->fill_rect(g, l, t, rt - l, 10, soft); api->fill_rect(g, l, bt - 10, rt - l, 10, soft);
        api->fill_rect(g, l, t + 10, 10, bt - t - 20, soft); api->fill_rect(g, rt - 10, t + 10, 10, bt - t - 20, soft);
    }
    if (golden.empty() || goldenBoard != b) return;
    for (void* f : golden)
    {
        int x = at<int>(f, Widget_mX) + at<int>(f, Widget_mWidth) / 2, y = at<int>(f, Widget_mY) + 14;
        int k = (GetTickCount() / 150) % 3;
        api->fill_rect(g, x - 2 - k * 6, y - 1, 3, 3, 0xffffd65a);
        api->fill_rect(g, x + 4 + k * 3, y + 6, 2, 2, 0xffffe890);
        api->fill_rect(g, x - 9 + k * 4, y + 10, 2, 2, 0xffffd65a);
    }
}

extern "C" __declspec(dllexport) const char* RemodDescribe() { return "15 optional rule changes (Mutators tab of the settings)."; }

extern "C" __declspec(dllexport) int RemodInit(const RemodApi* a)
{
    if (a->version < 2) return 0;
    api = a;
    goldenOdds = std::max(1, api->config_int("mutators", "goldenodds", 50));   // for testing
    bool ok = api->hook((void*)Board_InitLevel, (void*)&InitLevel, (void**)&oInitLevel)
           && api->hook((void*)Board_StartLevel, (void*)&StartLevel, (void**)&oStartLevel)
           && api->hook((void*)Board_AddPet, (void*)&AddPet, (void**)&oAddPet)
           && api->hook((void*)Board_AddMoney, (void*)&AddMoney, (void**)&oAddMoney)
           && api->hook((void*)Board_SpawnAlien, (void*)&SpawnAlien, (void**)&oSpawnAlien)
           && api->hook((void*)Alien_ctor, (void*)&AlienCtor, (void**)&oAlienCtor)
           && api->hook((void*)Alien_Shoot, (void*)&Shoot, (void**)&oShoot)
           && api->hook((void*)GameObject_TickHunger, (void*)&TickHunger, (void**)&oTickHunger)
           && api->hook((void*)Coin_Update, (void*)&CoinUpdate, (void**)&oCoinUpdate)
           && api->hook((void*)Coin_MouseDown, (void*)&CoinMouseDown, (void**)&oCoinMouseDown)
           && api->hook((void*)Board_BuyItem, (void*)&BuyItem, (void**)&oBuyItem)
           && api->hook((void*)Fish_DropCoin, (void*)&DropCoin, (void**)&oDropCoin)
           && api->hook((void*)Fish_Eat, (void*)&FishEat, (void**)&oFishEat)
           && api->hook((void*)Board_AddDeadFish, (void*)&AddDeadFish, (void**)&oAddDeadFish)
           && api->hook((void*)DeadFish_Update, (void*)&DeadFishUpdate, (void**)&oDeadFishUpdate)
           && api->hook((void*)Widget_MouseDown, (void*)&WidgetMouseDown, (void**)&oWidgetMouseDown);
    if (!ok) return 0;
    api->on_tick(Tick);
    api->on_overlay(Overlay);
    api->on_mouse(Mouse);
    return 1;
}
