// screensaver: watch your Virtual Tank as the game's own screensaver, started from the game. The Mods page's Open button
// (or F11 on the main menu) saves your profile, starts WinFish_Scr.exe -screensaver from the game folder and closes the game
// (both use the same profile, so only one runs at a time). Because the screensaver starts from inside the running game,
// on Linux and the Steam Deck it runs in the game's own Proton prefix and finds your saves: no shortcut of its own, no
// shared folders to set up. [screensaver] key=122 (F11; 0 = no key), quit=1 (0 = keep the game running).
#include "remod.h"
#include "game.h"
#include "remodui.h"
#include <windows.h>
#include <string>

using namespace game;
static const RemodApi* api;
static int key = VK_F11;
static bool quitGame = true;

static void Load()
{
    key = api->config_int("screensaver", "key", VK_F11);
    quitGame = api->config_int("screensaver", "quit", 1) != 0;
}

// the game folder: this DLL is <game>\mods\screensaver.dll
static std::string GameFolder()
{
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&GameFolder, &self);
    char path[MAX_PATH];
    GetModuleFileNameA(self, path, MAX_PATH);
    std::string p = path;
    p = p.substr(0, p.find_last_of("\\/"));    // ...\mods
    return p.substr(0, p.find_last_of("\\/"));  // the game folder
}

static void Start()
{
    if (ui::CoopPlaying()) { api->toast("Not during a co-op game"); return; }
    std::string dir = GameFolder(), exe = dir + "\\WinFish_Scr.exe";
    if (GetFileAttributesA(exe.c_str()) == INVALID_FILE_ATTRIBUTES) { api->toast("The game's screensaver (WinFish_Scr.exe) isn't in the game folder"); api->log("screensaver: no %s", exe.c_str()); return; }
    if (void* a = api->app()) reinterpret_cast<bool(__thiscall*)(void*)>(App_SaveProfile)(a);   // the screensaver reads the same profile
    std::string cmd = "\"" + exe + "\" -screensaver";   // what the game's Insaniquarium.scr passes it
    STARTUPINFOA si{}; si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi))
    {
        api->toast("Couldn't start the screensaver");
        api->log("screensaver: CreateProcess failed (%lu)", GetLastError());
        return;
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    api->log("screensaver: started %s", exe.c_str());
    if (quitGame)
        if (void* a = api->app()) PostMessageA(at<HWND>(a, 0x350), WM_CLOSE, 0, 0);   // the game saves and closes as usual
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
    return 1;
}
