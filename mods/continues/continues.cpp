// continues: when all your fish die, spend shells to get two guppies and keep going instead of "GAME OVER". The price
// starts at 500 shells and rises by 500 with each continue until you go back to the main menu. Only when the profile
// can pay; the first tutorial level and the pets-only bonus tank keep the game's own messages.
// [continues] enabled=1, price=500.
#include "remod.h"
#include "game.h"
#include "remodui.h"
#include <stdio.h>

using namespace game;
static const RemodApi* api;
static int price = 500, bought;
static bool enabled = true;
static const int DialogId = 0x46;   // not used by the game (its dialogs go up to 0x2b)

typedef void(__thiscall* DialogFn)(void*, int, bool, const MsvcString*, const MsvcString*, const MsvcString*, int);
typedef void(__thiscall* ButtonFn)(void*, int);
typedef void(__thiscall* VoidFn)(void*);
typedef bool(__thiscall* KillFn)(void*, int);
typedef void*(__thiscall* SpawnFn)(void*);
static DialogFn origDialog;
static ButtonFn origButton;
static VoidFn origSelector;

// in co-op every player's game must decide alike: the defaults, not each player's settings
static bool On() { return enabled || ui::CoopPlaying(); }
static int Cost() { return (ui::CoopPlaying() ? 500 : price) * (bought + 1); }
// co-op: a player joining a game in progress gets the count of continues bought
extern "C" __declspec(dllexport) int ContinuesBought() { return bought; }
extern "C" __declspec(dllexport) void ContinuesSetBought(int n) { bought = n; }
static int* Shells(void* app) { void* p = at<void*>(app, App_mProfile); return p ? &at<int>(p, Profile_mShells) : nullptr; }

static void __fastcall DialogHook(void* app, void*, int id, bool modal, const MsvcString* h, const MsvcString* l, const MsvcString* f, int buttons)
{
    void* b = api->board();
    int* shells = Shells(app);
    if (On() && id == 0x11 && b && at<int>(b, Board_mTank) != 5 && shells && *shells >= Cost() && !at<bool>(app, App_mIsScreenSaver))
    {
        static char text[200];
        snprintf(text, sizeof text, "All of your fish have died! Spend %d shells to get two guppies and keep going? (You have %d.)",
                 Cost(), *shells);
        MsvcString header = MakeString("GAME OVER"), lines = MakeString(text), footer = MakeString("");
        origDialog(app, DialogId, modal, &header, &lines, &footer, 1);   // 1 = Yes / No
        return;
    }
    origDialog(app, id, modal, h, l, f, buttons);
}

static void __fastcall ButtonHook(void* app, void*, int id)
{
    if (id == DialogId + 2000 || id == DialogId + 3000)
    {
        void** vt = *reinterpret_cast<void***>(app);
        reinterpret_cast<KillFn>(vt[App_vKillDialog / 4])(app, DialogId);
        void* b = api->board();
        int* shells = Shells(app);
        if (id == DialogId + 2000 && b && shells && *shells >= Cost())
        {
            *shells -= Cost();
            bought++;
            reinterpret_cast<SpawnFn>(Board_SpawnGuppy)(b);
            reinterpret_cast<SpawnFn>(Board_SpawnGuppy)(b);
            reinterpret_cast<bool(__thiscall*)(void*)>(App_SaveProfile)(app);
            api->log("continues: continue %d bought", bought);
        }
        else origButton(app, 0x11 + 2000);   // gave up: the game's own "game over" (back to the main menu)
        return;
    }
    origButton(app, id);
}

static void __fastcall SelectorHook(void* app, void*) { bought = 0; origSelector(app); }

static void Load()
{
    enabled = api->config_int("continues", "enabled", 1) != 0;
    price = api->config_int("continues", "price", 500);
}

extern "C" __declspec(dllexport) const char* RemodDescribe() { return "When all your fish die: spend shells for two guppies and keep going."; }

extern "C" __declspec(dllexport) int RemodInit(const RemodApi* a)
{
    if (a->version < REMOD_API_VERSION) return 0;
    api = a;
    Load();
    if (api->version >= 3) api->on_config(Load);
    if (!api->hook((void*)App_DoTimedDialog, (void*)&DialogHook, (void**)&origDialog)
        || !api->hook((void*)App_ButtonDepress, (void*)&ButtonHook, (void**)&origButton)
        || !api->hook((void*)App_ShowGameSelector, (void*)&SelectorHook, (void**)&origSelector)) return 0;
    api->log("continues: %d shells, +%d each", price, price);
    return 1;
}
