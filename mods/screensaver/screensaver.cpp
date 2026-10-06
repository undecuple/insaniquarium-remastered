// screensaver: watch your Virtual Tank as the game's own screensaver, started from the game. The Mods page's Open button
// (or F11 on the main menu) saves your profile, starts the game's own exe in its screensaver mode (-screensaver: what
// WinFish_Scr.exe is built from) and minimises the game (which pauses itself) until the screensaver ends, then brings it
// back. Because the screensaver starts from inside
// the running game, on Linux and the Steam Deck it runs in the game's own Proton prefix and finds your saves: no shortcut
// of its own, no shared folders to set up. The game keeps running on purpose: under Steam, once the game closes Steam ends
// everything it started, the screensaver included. [screensaver] key=122 (F11; 0 = no key), quit=0 (1 = close the game).
#include "remod.h"
#include "game.h"
#include "remodui.h"
#include <windows.h>
#include <string>
#include <thread>
#include <atomic>

using namespace game;
static const RemodApi* api;
static int key = VK_F11;
static bool quitGame = false;
static std::atomic<int> ended{ -1 };   // the screensaver's run time in ms when it ended (-1: not yet), for the main thread
static std::atomic<unsigned> endCode;

static void Load()
{
    key = api->config_int("screensaver", "key", VK_F11);
    quitGame = api->config_int("screensaver", "quit", 0) != 0;
}

static void Start()
{
    if (ui::CoopPlaying()) { api->toast("Not during a co-op game"); return; }
    if (void* a = api->app()) reinterpret_cast<bool(__thiscall*)(void*)>(App_SaveProfile)(a);   // the screensaver reads the same profile
    // the game's own exe in its screensaver mode, with the same command line (the Steam release's -changedir included):
    // Insaniquarium - Remastered Mod loads in it too and keeps it in a window covering the screen (the screensaver's
    // fullscreen mode switch fails on some Linux desktops), with no mods
    std::string cmd = std::string(GetCommandLineA()) + " -screensaver";
    STARTUPINFOA si{}; si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
    {
        api->toast("Couldn't start the screensaver");
        api->log("screensaver: CreateProcess failed (%lu)", GetLastError());
        return;
    }
    AllowSetForegroundWindow(pi.dwProcessId);
    CloseHandle(pi.hThread);
    api->log("screensaver: started %s", cmd.c_str());
    HWND wnd = nullptr;
    if (void* a = api->app()) wnd = at<HWND>(a, 0x350);
    if (quitGame)
    {
        CloseHandle(pi.hProcess);
        if (wnd) PostMessageA(wnd, WM_CLOSE, 0, 0);   // the game saves and closes as usual
        return;
    }
    if (wnd) ShowWindowAsync(wnd, SW_MINIMIZE);   // out of the way (and paused) while the screensaver shows
    HANDLE proc = pi.hProcess;
    DWORD started = GetTickCount();
    std::thread([proc, wnd, started] {
        WaitForSingleObject(proc, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(proc, &code);
        CloseHandle(proc);
        endCode = code;
        ended = (int)(GetTickCount() - started);
        if (wnd) { ShowWindowAsync(wnd, SW_RESTORE); SetForegroundWindow(wnd); }
    }).detach();
}

// back in the game: what happened to the screensaver (from the main thread)
static void Overlay(void*)
{
    int ms = ended.exchange(-1);
    if (ms < 0) return;
    unsigned code = endCode;
    api->log("screensaver: ended after %d s (exit code 0x%x)", ms / 1000, code);
    if (code >= 0xc0000000u) api->toast("The screensaver crashed (see mods\\remastered-mod.log)");
    else if (ms < 3000) api->toast("The screensaver closed straight away");
}

static int Key(int vk, int down)
{
    if (!down || !key || vk != key || !ui::OnMainMenu(api) || ui::DialogCount(api) > 0) return 0;
    Start();
    return 1;
}

extern "C" __declspec(dllexport) const char* RemodDescribe() { return "Your Virtual Tank as the game's screensaver: Open here, or F11 on the main menu."; }
extern "C" __declspec(dllexport) void RemodOpen() { Start(); }

extern "C" __declspec(dllexport) int RemodInit(const RemodApi* a)
{
    if (a->version < 3) return 0;
    api = a;
    Load();
    api->on_config(Load);
    api->on_key(Key);
    api->on_overlay(Overlay);
    return 1;
}
