// fps: a frame counter in the top-left corner, toggled with F3 ([fps] key=114, shown=0 to start hidden).
// Counts the frames the game actually draws each second.
#include "remod.h"
#include <windows.h>
#include <stdio.h>

static const RemodApi* api;
static int key = 0x72, frames, shown_fps;
static bool on;
static DWORD second;

static int Key(int vk, int down)
{
    if (!down || vk != key) return 0;
    on = !on;
    if (api->version >= 3) api->config_set("fps", "shown", on ? "1" : "0");   // remembered, and the settings page agrees
    api->redraw();
    return 1;
}

static void Overlay(void* g)
{
    frames++;
    DWORD now = GetTickCount();
    if (now - second >= 1000) { shown_fps = frames * 1000 / (int)(now - second); frames = 0; second = now; }
    if (!on) return;
    char s[32];
    snprintf(s, sizeof s, "%d FPS", shown_fps);
    api->fill_rect(g, 4, 4, api->text_width(s) + 10, 18, 0x90000000);
    api->draw_text(g, s, 9, 18, 0xffffff60);
}

static void Load()
{
    key = api->config_int("fps", "key", 0x72);
    on = api->config_int("fps", "shown", 0) != 0;
}

extern "C" __declspec(dllexport) const char* RemodDescribe() { return "A frame counter in the corner (F3)."; }

extern "C" __declspec(dllexport) int RemodInit(const RemodApi* a)
{
    if (a->version < REMOD_API_VERSION) return 0;
    api = a;
    Load();
    if (api->version >= 3) api->on_config(Load);
    second = GetTickCount();
    api->on_key(Key);
    api->on_overlay(Overlay);
    return 1;
}
