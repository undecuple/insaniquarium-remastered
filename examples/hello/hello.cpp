// hello: the smallest mod, an example for mod authors (not part of a release). Shows a toast when a tank starts and a
// corner tag while playing; [hello] tag=0 in mods/remastered-mod.ini turns the tag off. It also shows how a mod appears
// on the settings' Mods tab: RemodDescribe is its description there, RemodOpen gives its row an Open button.
#include "remod.h"

static const RemodApi* api;
static void* lastBoard;
static bool tag = true;

static void Tick(void* board)
{
    if (board != lastBoard) { lastBoard = board; api->toast("Remastered Mod is on"); }
}

static void Overlay(void* g)
{
    if (tag && api->board()) api->draw_text(g, "Remastered Mod", 528, 470, 0xa0ffffff);
}

// the Mods tab: the line shown for this mod, and what its Open button does (here: switch the tag, and say so)
extern "C" __declspec(dllexport) const char* RemodDescribe() { return "Example: a toast when a tank starts, a corner tag."; }
extern "C" __declspec(dllexport) void RemodOpen()
{
    tag = !tag;
    api->config_set("hello", "tag", tag ? "1" : "0");
    api->toast(tag ? "hello: tag on" : "hello: tag off");
}

extern "C" __declspec(dllexport) int RemodInit(const RemodApi* a)
{
    if (a->version < REMOD_API_VERSION) return 0;
    api = a;
    tag = api->config_int("hello", "tag", 1) != 0;
    api->on_tick(Tick);
    api->on_overlay(Overlay);
    api->log("hello: ready");
    return 1;
}
