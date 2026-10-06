// crashtest (tests only, never shipped): crashes on purpose so the core's crash containment can be tested.
// F12 = null-pointer write inside on_key; [crashtest] tick=N in remastered-mod.ini = crash in on_tick after N updates.
#include "remod.h"

static const RemodApi* api;
static int crashTick, ticks;

static int Key(int vk, int down)
{
    if (down && vk == 0x7B) { api->log("crashtest: crashing in on_key"); *(volatile int*)0 = 1; }
    return 0;
}

static void Tick(void*)
{
    if (crashTick > 0 && ++ticks == crashTick) { api->log("crashtest: crashing in on_tick"); volatile int z = 0; ticks /= z; }
}

static void Overlay(void* g)
{
    if (api->board()) api->draw_text(g, "crashtest alive", 10, 300, 0xffff8080);
}

extern "C" __declspec(dllexport) int RemodInit(const RemodApi* a)
{
    api = a;
    crashTick = api->config_int("crashtest", "tick", 0);
    api->on_key(Key);
    api->on_tick(Tick);
    api->on_overlay(Overlay);
    return 1;
}
