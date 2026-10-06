// accessibility: a red "!" over hungry fish (the game only tints them green, which is hard to see for some players)
// and each coin's value under it (so silver, gold and gems don't rely on colour). Display only.
// [accessibility] hungry=1, coins=1.
#include "remod.h"
#include "game.h"
#include <windows.h>
#include <stdio.h>

using namespace game;
static const RemodApi* api;
static bool hungry = true, coins = true;

typedef bool(__thiscall* BoolFn)(void*);
typedef int(__thiscall* IntFn)(void*);

static void Badge(void* g, void* o, int bob)
{
    int x = at<int>(o, Widget_mX) + at<int>(o, Widget_mWidth) / 2, y = at<int>(o, Widget_mY) + 6 + bob;
    api->fill_rect(g, x - 7, y - 7, 14, 15, 0xa0000000);
    api->fill_rect(g, x - 6, y - 6, 12, 13, 0xffe83030);
    api->fill_rect(g, x - 1, y - 4, 3, 6, 0xffffffff);
    api->fill_rect(g, x - 1, y + 3, 3, 2, 0xffffffff);
}

static void Overlay(void* g)
{
    void* b = api->board();
    void* app = api->app();
    if (!b || at<bool>(b, Board_mPaused) || at<int>(app, App_mDialogCount) > 0) return;   // menus and dialogs cover the tank
    if (hungry)
    {
        int bob = (GetTickCount() / 120) % 4 < 2 ? 0 : 1;
        const int lists[] = { Board_mGuppies, Board_mOscars, Board_mUltras, Board_mGekkos, Board_mPentas, Board_mGrubbers,
                              Board_mBreeders, Board_mSpecialFish };
        for (int l : lists)
            for (int i = 0; i < Count(b, l); i++)
            {
                void* o = Item(b, l, i);
                if (reinterpret_cast<BoolFn>(GameObject_IsHungry)(o)) Badge(g, o, bob);
            }
    }
    if (coins)
        for (int i = 0; i < Count(b, Board_mCoins); i++)
        {
            void* c = Item(b, Board_mCoins, i);
            if (at<bool>(c, Coin_mCollected) || at<int>(c, Coin_mCoinType) >= 0xf) continue;
            int v = reinterpret_cast<IntFn>(Coin_GetValue)(c);
            if (v <= 0) continue;
            char s[16];
            snprintf(s, sizeof s, "%d", v);
            int w = api->text_width(s);
            api->draw_text(g, s, at<int>(c, Widget_mX) + (at<int>(c, Widget_mWidth) - w) / 2, at<int>(c, Widget_mY) + at<int>(c, Widget_mHeight) - 4, 0xffffffff);
        }
}

static void Load()
{
    hungry = api->config_int("accessibility", "hungry", 1) != 0;
    coins = api->config_int("accessibility", "coins", 1) != 0;
}

extern "C" __declspec(dllexport) const char* RemodDescribe() { return "A red ! over hungry fish, and each coin's value under it."; }

extern "C" __declspec(dllexport) int RemodInit(const RemodApi* a)
{
    if (a->version < REMOD_API_VERSION) return 0;
    api = a;
    Load();
    if (api->version >= 3) api->on_config(Load);
    api->on_overlay(Overlay);
    return 1;
}
