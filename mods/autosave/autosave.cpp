// autosave: the game saves a level only when you leave it or quit; this also saves the level in progress and the
// profile every few minutes and whenever the game pauses (the window loses focus, a menu opens), so a crash or power
// cut loses little. Uses the game's own save functions. [autosave] minutes=2 (0 = only on pause), label=1.
#include "remod.h"
#include "game.h"
#include <windows.h>

using namespace game;
static const RemodApi* api;
static DWORD interval, last, labelUntil;
static bool label = true, pending;

typedef void(__thiscall* PauseFn)(void*, bool);
typedef void(__thiscall* VoidFn)(void*);
typedef bool(__thiscall* BoolFn)(void*);
static PauseFn origPause;

// in co-op the session decides about saving (and the timer would fire at different moments on each player's machine)
static bool Coop()
{
    static int (*f)() = nullptr;
    static bool looked;
    if (!looked) { looked = true; if (HMODULE m = GetModuleHandleA("coop.dll")) f = reinterpret_cast<int (*)()>(GetProcAddress(m, "CoopActive")); }
    return f && f();
}

static void SaveNow()
{
    last = GetTickCount();
    if (Coop()) return;
    void* app = api->app();
    void* b = api->board();
    if (!app || !b || at<bool>(app, App_mIsScreenSaver) || at<bool>(app, App_mBoardInactive)) return;
    if (!reinterpret_cast<BoolFn>(Board_CanSaveOnQuit)(b)) return;   // nothing worth resuming (sandbox, level over)
    reinterpret_cast<BoolFn>(App_SaveProfile)(app);
    reinterpret_cast<VoidFn>(Board_SaveOrDeleteGame)(b);
    labelUntil = GetTickCount() + 2000;
    api->log("autosave: saved");
}

static void __fastcall PauseHook(void* board, void*, bool on)
{
    bool was = at<bool>(board, Board_mPaused);
    origPause(board, on);
    // saved on the next frame, not here: at a game over the game pauses first and only then marks the level as not
    // worth saving
    if (on && !was && board == api->board()) pending = true;
}

static void Tick(void*)
{
    if (interval && GetTickCount() - last >= interval) SaveNow();
}

static void Overlay(void* g)
{
    if (pending) { pending = false; SaveNow(); }
    if (!label || !labelUntil) return;
    if (GetTickCount() >= labelUntil) { labelUntil = 0; api->redraw(); return; }
    api->draw_text(g, "Autosaved", 8, 470, 0xc0ffffff);
}

static void Load()
{
    interval = (DWORD)api->config_int("autosave", "minutes", 2) * 60000;
    label = api->config_int("autosave", "label", 1) != 0;
}

extern "C" __declspec(dllexport) const char* RemodDescribe() { return "Saves the level and your profile whenever the game pauses and every few minutes."; }

extern "C" __declspec(dllexport) int RemodInit(const RemodApi* a)
{
    if (a->version < REMOD_API_VERSION) return 0;
    api = a;
    Load();
    last = GetTickCount();
    if (api->version >= 3) api->on_config(Load);
    if (!api->hook((void*)Board_Pause, (void*)&PauseHook, (void**)&origPause)) return 0;
    api->on_tick(Tick);
    api->on_overlay(Overlay);
    api->log("autosave: every %lu min and on pause", interval / 60000);
    return 1;
}
