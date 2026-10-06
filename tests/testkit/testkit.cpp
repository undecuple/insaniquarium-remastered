// testkit (test-only, never released): keys for staging situations in headless tests.
//   F9  every fish in the tank starves (hunger timer 0: they die on the next tick); K only the first guppy
//   F11 six coins of different kinds appear
//   Shift+F11 every fish gets hungry (not starving; Windows only: needs the real Shift state)
//   C   click the first dead fish
//   F   feed the first guppy while it's full
//   R   determinism probe: seeds the game's random numbers, starts Adventure (no help, no saved game) and from then on
//       counts update ticks; at fixed ticks it injects clicks (feeding, a coin, buying) and logs a state hash at tick 1500
//   M   +$10,000;  E  buy an egg piece (unlocking its store slot)
//   B   buy a guppy (through the store, as a click on its button)
//   F10 the profile: +5000 shells, and Adventure moves on to tank 1 level 2 (past the tutorial) if it's still at 1-1
#include "remod.h"
#include "game.h"
#include <windows.h>

using namespace game;
static const RemodApi* api;
static int probeTick = -1;

static int Key(int vk, int down)
{
    if (!down) return 0;
    void* app = api->app();
    if (vk == 0x78)   // F9
    {
        void* b = api->board();
        if (!b) return 0;
        const int lists[] = { Board_mGuppies, Board_mOscars, Board_mUltras, Board_mGekkos, Board_mPentas, Board_mGrubbers, Board_mBreeders };
        int n = 0;
        for (int l : lists)
            for (int i = 0; i < Count(b, l); i++) { at<int>(Item(b, l, i), 0x9c) = 0; n++; }
        api->log("testkit: %d fish starve", n);
        return 1;
    }
    if (vk == 0x7a)   // F11
    {
        void* b = api->board();
        if (!b) return 0;
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        {
            const int lists[] = { Board_mGuppies, Board_mOscars, Board_mUltras, Board_mGekkos, Board_mPentas, Board_mGrubbers, Board_mBreeders };
            for (int l : lists)
                for (int i = 0; i < Count(b, l); i++) at<int>(Item(b, l, i), 0x9c) = 250;
            api->log("testkit: hungry fish");
            return 1;
        }
        const int types[] = { 1, 2, 3, 4, 6, 7 };
        typedef void*(__thiscall* AddCoinFn)(void*, int, int, int, int, double, int);
        for (int i = 0; i < 6; i++) reinterpret_cast<AddCoinFn>(Board_AddCoin)(b, 60 + i * 90, 120, types[i], 0, -1.0, 0);
        api->log("testkit: 6 coins");
        return 1;
    }
    if (vk == 'K' && api->board() && Count(api->board(), Board_mGuppies) > 0)
    {
        at<int>(Item(api->board(), Board_mGuppies, 0), 0x9c) = 0;
        api->log("testkit: a guppy starves");
        return 1;
    }
    if (vk == 'C' && api->board() && Count(api->board(), Board_mDeadFish) > 0)   // click the first corpse (its MouseDown, vtable 0xd8)
    {
        void* d = Item(api->board(), Board_mDeadFish, 0);
        void** vt = *reinterpret_cast<void***>(d);
        reinterpret_cast<void(__thiscall*)(void*, int, int, int)>(vt[0xd8 / 4])(d, 40, 40, 1);
        api->log("testkit: clicked a corpse, %d left", Count(api->board(), Board_mDeadFish));
        return 1;
    }
    if (vk == 'F' && api->board() && Count(api->board(), Board_mGuppies) > 0)   // feed the first guppy while it's full
    {
        void* b = api->board();
        void* f = Item(b, Board_mGuppies, 0);
        at<int>(f, 0x9c) = 900;
        reinterpret_cast<void(__thiscall*)(void*, int, int, int, bool, int, int)>(Board_DropFood)(b, at<int>(f, Widget_mX) + 20, 0x50, 0, true, 0x14, -1);
        void* food = Item(b, Board_mFood, Count(b, Board_mFood) - 1);
        void** vt = *reinterpret_cast<void***>(f);
        reinterpret_cast<void(__thiscall*)(void*, void*)>(vt[0x134 / 4])(f, food);
        api->log("testkit: fed a full guppy (%d guppies)", Count(b, Board_mGuppies));
        return 1;
    }
    if (vk == 'R' && probeTick < 0) { probeTick = 0; api->log("testkit: determinism probe starts"); return 1; }
    if (vk == 'M' && api->board())
    {
        at<int>(api->board(), Board_mMoney) += 10000;
        reinterpret_cast<void(__thiscall*)(void*)>(Board_UpdateMoneyLabel)(api->board());
        api->log("testkit: +$10000");
        return 1;
    }
    if (vk == 'E' && api->board())
    {
        reinterpret_cast<void(__thiscall*)(void*, int, bool)>(Board_UnlockStoreItem)(api->board(), 0xb, false);
        reinterpret_cast<void(__thiscall*)(void*, int)>(Board_BuyItem)(api->board(), 0xb);
        api->log("testkit: bought an egg piece");
        return 1;
    }
    if (vk == 'B' && api->board())
    {
        reinterpret_cast<void(__thiscall*)(void*, int)>(Board_BuyItem)(api->board(), 0);
        api->log("testkit: bought a guppy (money %d)", at<int>(api->board(), Board_mMoney));
        return 1;
    }
    if (vk == 0x79 && app)   // F10
    {
        void* p = at<void*>(app, App_mProfile);
        if (!p) return 0;
        at<int>(p, Profile_mShells) += 5000;
        if (at<int>(p, 0x1c) == 1 && at<int>(p, 0x20) == 1) at<int>(p, 0x20) = 2;   // mTank, mLevel
        reinterpret_cast<bool(__thiscall*)(void*)>(App_SaveProfile)(app);
        api->log("testkit: shells %d, adventure %d-%d", at<int>(p, Profile_mShells), at<int>(p, 0x1c), at<int>(p, 0x20));
        return 1;
    }
    return 0;
}

// ---- determinism probe -----------------------------------------------------------------------------------------------
static void(__thiscall* oUpdateFrames)(void*);

static uint64_t Hash()
{
    uint64_t h = 0xcbf29ce484222325ULL;
    auto mix = [&](uint32_t v) { for (int i = 0; i < 4; i++) { h ^= (v >> (i * 8)) & 0xff; h *= 0x100000001b3ULL; } };
    void* a = api->app();
    uint32_t* mt = at<uint32_t*>(a, App_mMTRand);
    mix(mt[624]); mix(mt[0]); mix(mt[100]); mix(mt[623]);
    void* b = api->board();
    if (b)
    {
        mix(at<int>(b, Board_mMoney)); mix(at<int>(b, Board_mTick)); mix(at<int>(b, Board_mAlienTimer));
        const int lists[] = { Board_mGuppies, Board_mOscars, Board_mCoins, Board_mFood, Board_mAliens, Board_mFishPets, 0xb0 };
        for (int l : lists)
        {
            mix(Count(b, l));
            for (int i = 0; i < Count(b, l); i++) { void* o = Item(b, l, i); mix(at<int>(o, Widget_mX)); mix(at<int>(o, Widget_mY)); mix(at<int>(o, 0x9c)); }
        }
    }
    return h;
}

static void Click(int x, int y)
{
    api->inject_input(WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
    api->inject_input(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
    api->inject_input(WM_LBUTTONUP, 0, MAKELPARAM(x, y));
}

static void __fastcall UpdateFrames(void* app, void*)
{
    if (probeTick >= 0)
    {
        int t = probeTick++;
        if (t == 0)
        {
            uint32_t* mt = at<uint32_t*>(app, App_mMTRand);
            mt[0] = 12345;
            for (int i = 1; i < 624; i++) mt[i] = 1812433253u * (mt[i - 1] ^ (mt[i - 1] >> 30)) + i;
            mt[624] = 624;
            at<bool>(app, 0x880) = false;
            reinterpret_cast<void(__thiscall*)(void*)>(App_RemoveGameSelector)(app);
            at<int>(app, App_mGameMode) = 0;
            reinterpret_cast<void(__thiscall*)(void*, bool, bool)>(App_StartGame)(app, false, false);
        }
        else if (t % 150 == 0 && t < 1500) Click(150 + (t / 150) * 37 % 340, 200 + (t / 150) * 53 % 150);   // feed here and there
        else if (t % 210 == 0 && t < 1500) Click(45, 30);   // the guppy button
        if (t % 300 == 0 || t == 1500) api->log("testkit: probe tick %d hash %016llx", t, (unsigned long long)Hash());
        if (t == 1500) probeTick = -1;
    }
    oUpdateFrames(app);
}

extern "C" __declspec(dllexport) int RemodInit(const RemodApi* a)
{
    api = a;
    if (a->version >= 5) api->hook((void*)App_UpdateFrames, (void*)&UpdateFrames, (void**)&oUpdateFrames);
    api->on_key(Key);
    api->log("testkit: F9 starve, F10 shells + level 1-2");
    return 1;
}
