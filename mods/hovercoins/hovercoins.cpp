// hovercoins: coins (and gems, treasure, pearls) are picked up when the pointer passes over them, without clicking.
// Works like a click on the coin (same sound, same money, achievements and combos still count); not while a menu or
// dialog is open. [hovercoins] enabled=1 (also on the settings page).
#include "remod.h"
#include "game.h"
#include "remodui.h"

using namespace game;
static const RemodApi* api;
static bool enabled = true;

static void Load() { enabled = api->config_int("hovercoins", "enabled", 1) != 0; }

static void Tick(void* b)
{
    if (!enabled || at<bool>(b, Board_mPaused) || ui::DialogCount(api) > 0) return;
    // in co-op, every player's pointer as the lockstep applied it (the same on every machine); else the local one
    int px[4], py[4], n = 0;
    if (ui::CoopPlaying())
    {
        static int (*pointer)(int, int*, int*) = reinterpret_cast<int (*)(int, int*, int*)>(GetProcAddress(GetModuleHandleA("coop.dll"), "CoopPointer"));
        for (int s = 0; s < 4 && pointer; s++) if (pointer(s, &px[n], &py[n])) n++;
    }
    else { api->mouse_pos(&px[0], &py[0]); n = 1; }
    for (int k = 0; k < n; k++)
    for (int i = Count(b, Board_mCoins) - 1; i >= 0; i--)
    {
        int mx = px[k], my = py[k];
        void* c = Item(b, Board_mCoins, i);
        if (at<bool>(c, Coin_mCollected) || !at<bool>(c, Widget_mMouseVisible)) continue;   // flying away / not clickable
        int t = at<int>(c, Coin_mCoinType);
        if (t >= 0xf && t != 0x12) continue;   // not text, bombs or other specials
        int x = at<int>(c, Widget_mX), y = at<int>(c, Widget_mY), w = at<int>(c, Widget_mWidth), h = at<int>(c, Widget_mHeight);
        // the coin's picture fills the middle of its widget: a slightly smaller box than the widget
        if (mx < x + w / 6 || mx >= x + w - w / 6 || my < y + h / 6 || my >= y + h - h / 6) continue;
        // through the game's vtable (MouseDown, 0xd8), so every mod's hooks on it run, as for a click
        void** vt = *reinterpret_cast<void***>(c);
        reinterpret_cast<void(__thiscall*)(void*, int, int, int)>(vt[0xd8 / 4])(c, mx - x, my - y, 1);
    }
}

extern "C" __declspec(dllexport) const char* RemodDescribe() { return "Pick up coins by moving the pointer over them, no click needed."; }

extern "C" __declspec(dllexport) int RemodInit(const RemodApi* a)
{
    if (a->version < 3) return 0;
    api = a;
    Load();
    api->on_config(Load);
    api->on_tick(Tick);
    return 1;
}
