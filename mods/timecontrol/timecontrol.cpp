// timecontrol: time control for the original game. F5 pauses/resumes, F6 toggles 0.75x, F8 toggles 2x. Speed works by changing the
// game's own update interval (app +0x454 mFrameTime, 28 ms), which its main loop already uses for timing; pause goes
// through the game's Board::Pause (@0053db80), which is hooked so the game's own unpausing (focus, dialogs closing)
// can't end a pause the player asked for. Keys: [timecontrol] pause/slow/fast as
// Windows virtual-key codes in mods/remastered-mod.ini.
#include "remod.h"
#include "game.h"
#include <stdio.h>
#include <windows.h>

static const RemodApi* api;
static double scale = 1.0;
static bool paused;
static int kPause, kSlow, kFast;

typedef void(__thiscall* PauseFn)(void*, bool);
static PauseFn origPause;
static bool inOwnPause;

// the game asked to pause or resume: keep the board paused while our pause is on
static void __fastcall PauseHook(void* board, void*, bool on)
{
    if (paused && !on && !inOwnPause) on = true;
    origPause(board, on);
}

static bool Coop()
{
    static int (*f)() = nullptr;
    static bool looked;
    if (!looked) { looked = true; if (HMODULE m = GetModuleHandleA("coop.dll")) f = reinterpret_cast<int (*)()>(GetProcAddress(m, "CoopPlaying")); }
    return f && f();
}

// co-op: the host's speed, applied by coop.dll on every machine at the same tick (CoopRequestSpeed); guests can't change it
template <typename F> static F CoopFn(const char* name) { HMODULE m = GetModuleHandleA("coop.dll"); return m ? reinterpret_cast<F>(GetProcAddress(m, name)) : nullptr; }
static bool CoopHost() { auto f = CoopFn<int (*)()>("CoopIsHost"); return f && f(); }
static void CoopState(double& s, bool& p)
{
    int ft = 28, pz = 0;
    if (auto f = CoopFn<void (*)(int*, int*)>("CoopSpeed")) f(&ft, &pz);
    s = ft > 0 ? 28.0 / ft : 1.0; p = pz != 0;
}

static void Apply()
{
    void* app = api->app();
    if (!app) return;
    if (Coop()) return;   // co-op: coop.dll sets the speed for everyone
    game::at<int>(app, game::App_mFrameTime) = (int)(28 / scale + 0.5);
    if (void* b = api->board()) { inOwnPause = true; origPause(b, paused); inOwnPause = false; }
}

static int Key(int vk, int down)
{
    if (!down || !api->board()) return 0;   // only in a tank
    if (Coop())
    {
        if (vk != kPause && vk != kSlow && vk != kFast) return 0;
        if (!CoopHost()) { api->toast("Only the host controls the speed in co-op"); return 1; }
        double s; bool p;
        CoopState(s, p);
        if (vk == kPause) p = !p;
        else if (vk == kSlow) { s = s == 0.75 ? 1.0 : 0.75; p = false; }
        else { s = s == 2.0 ? 1.0 : 2.0; p = false; }
        if (auto f = CoopFn<void (*)(int, int)>("CoopRequestSpeed")) f((int)(28 / s + 0.5), p ? 1 : 0);   // everyone, a moment later
        char t[64];
        snprintf(t, sizeof t, p ? "Paused for everyone (F5)" : "Speed %gx for everyone", s);
        api->toast(t);
        return 1;
    }
    if (vk == kPause) paused = !paused;
    else if (vk == kSlow) { scale = scale == 0.75 ? 1.0 : 0.75; paused = false; }   // again: back to normal
    else if (vk == kFast) { scale = scale == 2.0 ? 1.0 : 2.0; paused = false; }
    else return 0;
    Apply();
    api->redraw();   // the indicator changed (and a paused tank doesn't repaint by itself)
    char s[64];
    snprintf(s, sizeof s, paused ? "Paused (F5)" : "Speed %gx", scale);
    api->toast(s);
    return 1;
}

static void Tick(void*)
{
    // a new tank starts unpaused at the chosen speed
    static void* last;
    void* b = api->board();
    if (b != last) { last = b; paused = false; Apply(); api->redraw(); }
    if (Coop()) { scale = 1.0; paused = false; }   // in co-op the label shows the host's speed (coop.dll applies it)
}

static void Overlay(void* g)
{
    if (!api->board())
    {
        // menus and other screens always run at normal speed; the chosen speed comes back in the next tank (Tick)
        void* app = api->app();
        if (app && game::at<int>(app, game::App_mFrameTime) != 28) game::at<int>(app, game::App_mFrameTime) = 28;
        return;
    }
    double sc = scale; bool pz = paused;
    if (Coop()) CoopState(sc, pz);
    if (!pz && sc == 1.0) return;
    const char* s = pz ? "PAUSED (F5)" : sc > 1 ? ">> 2x" : "> 0.75x";
    int w = api->text_width(s) + 12;
    api->fill_rect(g, 640 - w - 8, 430, w, 20, 0x90000000);
    api->draw_text(g, s, 640 - w - 2, 445, 0xffffffff);
}

static void Load()
{
    kPause = api->config_int("timecontrol", "pause", 0x74);   // VK_F5
    kSlow = api->config_int("timecontrol", "slow", 0x75);     // VK_F6
    kFast = api->config_int("timecontrol", "fast", 0x77);     // VK_F8
}

extern "C" __declspec(dllexport) const char* RemodDescribe() { return "Pause with F5, play at 0.75x (F6) or 2x (F8)."; }

extern "C" __declspec(dllexport) int RemodInit(const RemodApi* a)
{
    if (a->version < REMOD_API_VERSION) return 0;
    api = a;
    Load();
    if (api->version >= 3) api->on_config(Load);
    if (!api->hook((void*)game::Board_Pause, (void*)&PauseHook, (void**)&origPause)) return 0;
    api->on_key(Key);
    api->on_tick(Tick);
    api->on_overlay(Overlay);
    api->log("timecontrol: F5 pause, F6 0.75x, F8 2x");
    return 1;
}
