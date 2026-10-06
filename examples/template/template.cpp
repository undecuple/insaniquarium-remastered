// template: a starting point for a new mod (not part of a release). Copy this file to mods/<yourname>/<yourname>.cpp,
// rename what you need and delete what you don't. It shows every callback, a hook on a game function, settings, a file
// of the mod's own, the Mods page exports and how to stay out of co-op games. What it does as it stands: counts the
// money you earn in each tank and shows the total in the corner ([template] shown=1), with F9 to show or hide it.
#include "remod.h"
#include "game.h"
#include "remodui.h"
#include <windows.h>
#include <stdio.h>
#include <string>

using namespace game;
static const RemodApi* api;

// ---- settings: [template] in mods/remastered-mod.ini (read again when they change: on_config) -------------------------
static bool shown = true;
static int key = VK_F9;
static void LoadSettings()
{
    shown = api->config_int("template", "shown", 1) != 0;
    key = api->config_int("template", "key", VK_F9);
}

// ---- state, and a file of the mod's own (never the game's saves) ---------------------------------------------------
static int earnedThisTank, earnedEver;
static std::string StatePath()
{
    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);   // the game's folder: <game>\mods\template.txt
    std::string p = exe;
    return p.substr(0, p.find_last_of("\\/") + 1) + "mods\\template.txt";
}
static void LoadState() { if (FILE* f = fopen(StatePath().c_str(), "r")) { if (fscanf(f, "%d", &earnedEver) != 1) earnedEver = 0; fclose(f); } }
static void SaveState() { if (FILE* f = fopen(StatePath().c_str(), "w")) { fprintf(f, "%d\n", earnedEver); fclose(f); } }

// ---- a hook on a game function: Board::AddMoney (@0053c1e0, void __thiscall (Board*, int amount)) --------------------
// Game methods are __thiscall: the detour is __fastcall with an unused second parameter (EDX); the original is called
// through a __thiscall pointer. Hooks chain: other mods may hook the same function before or after this one.
static void(__thiscall* oAddMoney)(void*, int);
static void __fastcall AddMoney(void* board, void*, int amount)
{
    oAddMoney(board, amount);   // the game's own code (and other mods' hooks) first
    if (amount > 0 && !ui::CoopPlaying()) { earnedThisTank += amount; earnedEver += amount; }   // co-op: see DEVELOPING.md
}

// ---- callbacks -----------------------------------------------------------------------------------------------------
static void Tick(void* board)   // every game update in a tank (28 ms)
{
    static void* last;
    if (board != last) { last = board; earnedThisTank = 0; }   // a new tank
}
static void DrawTank(void* board, void* g) { (void)board; (void)g; }   // after the tank is drawn, under fish and coins
static void Overlay(void* g)                                            // on top of every screen
{
    if (!shown || !api->board()) return;
    char t[64];
    snprintf(t, sizeof t, "Earned: $%d", earnedThisTank);
    api->fill_rect(g, 6, 452, api->text_width(t) + 10, 20, 0x90000000);
    api->draw_text(g, t, 11, 467, 0xffffffff);
}
static int Key(int vk, int down)   // return 1 when the key was yours (the game doesn't see it)
{
    if (!down || vk != key) return 0;
    shown = !shown;
    api->config_set("template", "shown", shown ? "1" : "0");
    api->redraw();   // the overlay changed: repaint the whole screen once
    return 1;
}
static int Mouse(int x, int y, int button, int down) { (void)x; (void)y; (void)button; (void)down; return 0; }   // 1 = keep the click from the game

// ---- the Mods page (docs/MOD-MANAGER.md) -----------------------------------------------------------------------------
extern "C" __declspec(dllexport) const char* RemodDescribe() { return "Template: counts the money earned in each tank (F9)."; }
extern "C" __declspec(dllexport) void RemodOpen()
{
    char t[96];
    snprintf(t, sizeof t, "Template: $%d earned in all, $%d in this tank", earnedEver, earnedThisTank);
    api->toast(t);
    SaveState();
}

// ---- start-up: called once, on the game's main thread, before the game loads its resources ---------------------------
extern "C" __declspec(dllexport) int RemodInit(const RemodApi* a)
{
    if (a->version < 3) return 0;   // config_set and on_config came with version 3: decline on an older core
    api = a;
    LoadSettings();
    LoadState();
    if (!api->hook((void*)Board_AddMoney, (void*)&AddMoney, (void**)&oAddMoney)) return 0;
    api->on_tick(Tick);
    api->on_draw(DrawTank);
    api->on_overlay(Overlay);
    api->on_key(Key);
    api->on_mouse(Mouse);
    api->on_config(LoadSettings);
    api->log("template: ready ($%d earned so far)", earnedEver);
    return 1;
}
