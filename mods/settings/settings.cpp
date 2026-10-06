// settings: the in-game settings page for Insaniquarium - Remastered Mod (F2 anywhere), and the main menu's second
// page: a "Remastered" button on the main menu turns the menu's button panel into one for the mod's screens (co-op,
// extra modes, achievements, settings, what's new, the loaded mods, records), drawn with the game's own buttons.
// The settings page is one of the game's dialogs (modal; the tank pauses behind it; DONE closes it). Everything it
// changes is written to mods/remastered-mod.ini; most changes apply at once, the ones marked "restart" from the next start.
#include "remod.h"
#include "game.h"
#include "version.h"
#include "remodui.h"
#include <windows.h>
#include <string>
#include <string.h>
#include <vector>

using namespace game;
static const RemodApi* api;
static const int DialogId = 0x48;
static const int DX = 30, DY = 20, DW = 580, DH = 440;          // the dialog
static const int CX = DX + 40, CW = DW - 80, TabY = DY + 66;    // content

struct Setting
{
    const char* label;
    const char* section;
    const char* key;
    const char* help;
    std::vector<std::pair<const char*, const char*>> choices;   // shown, stored; empty = a checkbox (1/0)
    const char* def;
    bool restart;
};

static std::vector<Setting> tabs[5];
static const char* TabNames[5] = { "Display", "Gameplay", "Mutators", "Mods", "About" };
static const int TabCount = 5, AboutTab = 4;
static int tab, hover = -1;
static bool open, needRestart;
static std::string modsDir;
static std::vector<int> modState;   // REMOD_MOD_* per row of the Mods tab

static std::string Get(const Setting& it)
{
    char v[64];
    api->config_string(it.section, it.key, it.def, v, sizeof v);
    return v;
}

static void Build()
{
    tabs[0] = {
        { "Window", "display", "window", "Large: a window as big as the screen allows (whole multiples of 640x480), resizable. Borderless: fills the screen, black bars at the sides. Game's own: 640x480, or its fullscreen mode.",
          { { "Game's own", "normal" }, { "Large", "native" }, { "Borderless", "borderless" } }, "native", true },
        { "Scaling", "display", "scale", "Fit: as large as the screen allows. Whole multiples: 2x, 3x... only (sharpest pixels, wider bars).",
          { { "Fit", "fit" }, { "Whole multiples", "integer" } }, "fit", true },
        { "Frame counter", "fps", "shown", "Frames per second in the top-left corner (F3 shows or hides it).", {}, "0", false },
    };
    tabs[1] = {
        { "Autosave", "autosave", "minutes", "Saves the level in progress and your profile whenever the game pauses, and every few minutes.",
          { { "On pause", "0" }, { "1 min", "1" }, { "2 min", "2" }, { "5 min", "5" } }, "2", false },
        { "\"Autosaved\" note", "autosave", "label", "A small note in the corner when the game was saved.", {}, "1", false },
        { "Continue with shells", "continues", "enabled", "When all your fish die: spend shells (500, then 1,000...) for two guppies and keep going.", {}, "1", false },
        { "Hover to collect", "hovercoins", "enabled", "Coins are picked up when the pointer passes over them: no need to click.", {}, "1", false },
        { "Achievement banners", "achievements", "banners", "A banner at the top when you unlock an achievement.", {}, "1", false },
        { "Hungry-fish marker", "accessibility", "hungry", "A red ! over hungry fish (the game only tints them green).", {}, "1", false },
        { "Coin values", "accessibility", "coins", "Each coin's value under it, so silver, gold and gems don't rely on colour.", {}, "1", false },
    };
    tabs[2] = {
        { "Hungry fish", "mutators", "hungry", "Fish get hungry twice as fast.", {}, "0", false },
        { "Double trouble", "mutators", "double", "Every alien arrives with a twin.", {}, "0", false },
        { "Glass cannon", "mutators", "glass", "Your laser hits twice as hard, but aliens are faster.", {}, "0", false },
        { "No pets", "mutators", "nopets", "No pets come into the tank.", {}, "0", false },
        { "Pacifist", "mutators", "pacifist", "Your laser can't hurt aliens. Pets and food only.", {}, "0", false },
        { "Heavy coins", "mutators", "heavycoins", "Coins sink faster.", {}, "0", false },
        { "Tiny wallet", "mutators", "tinywallet", "You can't hold more than $2,500.", {}, "0", false },
        { "Rich start", "mutators", "richstart", "Start each level with $1,000.", {}, "0", false },
        { "Hard mode", "mutators", "hard", "Higher prices, tougher aliens that come sooner, hungrier fish.", {}, "0", false },
        { "Golden guppies", "mutators", "golden", "1 in 50 guppies you buy is golden: coins twice as often.", {}, "0", false },
        { "Coin combos", "mutators", "combos", "Quick coin pickups build a bonus of up to double value.", {}, "0", false },
        { "Tank events", "mutators", "events", "Feeding frenzies, power cuts and currents now and then.", {}, "0", false },
        { "Overeating", "mutators", "overeat", "Feeding a full fish stuffs it (no coins for a while); three times and it pops.", {}, "0", false },
        { "Decay", "mutators", "decay", "Dead fish float until you click them away, and make the others hungry.", {}, "0", false },
        { "Gadgets", "mutators", "gadgets", "Buy an auto-feeder, a coin magnet and an alien alarm in each tank (top left).", {}, "0", false },
    };
    // the loaded-mods screen: every DLL in the mods folder with its state (the core's list)
    static std::vector<std::string> names, descriptions;
    names.clear(); descriptions.clear(); modState.clear();
    for (int i = 0;; i++)
    {
        const char *n = nullptr, *d = nullptr;
        int st = api->mod_list(i, &n, &d);
        if (st < 0) break;
        if (!strcmp(n, "settings")) continue;   // this page itself (switch it off in the ini: [mods] settings=0)
        names.push_back(n);
        descriptions.push_back(d && *d ? d : "A mod without a description.");
        modState.push_back(st);
    }
    tabs[3].clear();
    for (size_t i = 0; i < names.size(); i++)
        tabs[3].push_back({ names[i].c_str(), "mods", names[i].c_str(), descriptions[i].c_str(), {}, "1", true });
}


static void* App() { return api->app(); }
static void* Dialog() { return ui::GetDialog(api, DialogId); }
using ui::In;

// rows (44 px: the game's checkbox is 46x45): Mutators and Mods in three columns of 5, Gameplay in two, Display one
// the Mods tab: two columns (three past 16 mods), rows as tall as fit above the help line (44 px at most)
static int ModCols() { return modState.size() <= 16 ? 2 : 3; }
static int ModRows() { int c = ModCols(); return (std::max)(1, ((int)modState.size() + c - 1) / c); }
static int ModRowH() { return (std::min)(44, (DY + DH - 92 - 22 - (TabY + 36)) / ModRows()); }
static int Columns(int t) { return t == 3 ? ModCols() : t == 2 ? 3 : t == 0 ? 1 : 2; }
static RECT RowRect(int t, int i)
{
    if (t == 3)
    {
        int rows = ModRows(), h = ModRowH(), w = CW / ModCols(), x = CX + (i / rows) * w, y = TabY + 36 + (i % rows) * h;
        return { x, y, x + w - 6, y + h };
    }
    int cols = Columns(t), col = i / 5, row = i % 5;
    if (cols == 1) { col = 0; row = i; }
    int w = CW / cols, x = CX + col * w, y = TabY + 36 + row * 44;
    return { x, y, x + w - 6, y + 44 };
}
// top right of the Mods tab (its third column is free up to 10 mods; with more, below the rows)
static RECT RestartRect() { int y = DY + DH - 92 - 20; return { CX + CW - 200, y, CX + CW, y + 26 }; }   // under the rows, right
static bool Pending(size_t i)   // a mod's switch differs from what's running: a restart applies it
{
    if (i >= modState.size()) return false;
    bool want = Get(tabs[3][i]) != "0";
    return (modState[i] == REMOD_MOD_ON) != want && modState[i] != REMOD_MOD_CRASHED && modState[i] != REMOD_MOD_BROKEN;
}
// the Mods tab: a running mod that exports RemodOpen() (its own screen or settings) gets an Open button on its row
static std::string DllOf(size_t i) { return std::string(tabs[3][i].label) + ".dll"; }
static bool CanOpen(size_t i) { return i < modState.size() && modState[i] == REMOD_MOD_ON && ui::Has(DllOf(i).c_str(), "RemodOpen"); }
static RECT OpenRect(const RECT& r) { int h = (std::min)(28, (int)(r.bottom - r.top) - 4); int y = r.top + ((r.bottom - r.top) - h) / 2; return { r.right - 54, y, r.right, y + h }; }
static bool AnyPending() { for (size_t i = 0; i < modState.size(); i++) if (Pending(i)) return true; return false; }
static const char* StateText(size_t i)
{
    bool want = Get(tabs[3][i]) != "0";
    switch (modState[i])
    {
        case REMOD_MOD_ON: return want ? "on" : "off after restart";
        case REMOD_MOD_OFF: return want ? "on after restart" : "off";
        case REMOD_MOD_DECLINED: return want ? "didn't start (see the log)" : "off after restart";
        case REMOD_MOD_CRASHED: return "crashed: off until restart";
        default: return "not a mod";
    }
}
static RECT TabRect(int i) { int w = CW / TabCount; return { CX + i * w, TabY, CX + (i + 1) * w - 4, TabY + 29 }; }
// the value button of a setting with choices: right-aligned in its row, as wide as its text needs
static RECT ValueRect(const RECT& r, const char* text) { int w = (std::max)(api->text_width_font(ui::F10(), text) + 34, 80); return { r.right - w, r.top + 7, r.right, r.top + 36 }; }

static bool Coop() { return ui::CoopPlaying(); }   // in a co-op game a dialog on one machine would pause only that tank

static void Open(int startTab = -1)
{
    if (open || !App() || ui::DialogCount(api) > 0) return;
    Build();
    if (startTab >= 0) tab = startTab;
    if (!ui::OpenDialog(api, DialogId, "REMASTERED MOD", "DONE", DX, DY, DW, DH)) return;
    open = true;
    needRestart = false;
}

static void Closed()
{
    open = false;
    api->config_changed();
    if (needRestart) api->toast("Some changes apply after you restart the game");
    api->redraw();
}

typedef void(__thiscall* ButtonFn)(void*, int);
static ButtonFn oButton;
static void __fastcall ButtonHook(void* app, void*, int id)
{
    if (id == DialogId + 2000 || id == DialogId + 3000) { ui::KillDialog(api, DialogId); Closed(); return; }
    oButton(app, id);
}

static void Toggle(Setting& it)
{
    std::string v = Get(it);
    if (it.choices.empty()) v = v == "0" ? "1" : "0";
    else
    {
        size_t i = 0;
        while (i < it.choices.size() && v != it.choices[i].second) i++;
        v = it.choices[(i + 1) % it.choices.size()].second;
    }
    api->config_set(it.section, it.key, v.c_str());
    if (it.restart) needRestart = true;
}

// ---- the main menu's second page ----------------------------------------------------------------------------------------
static bool page;
static const RECT EntryRect = { 20, 412, 20 + 150, 412 + 29 };   // on the main menu, level with Options / Help / Quit
static const RECT PanelRect = { 318, 6, 612, 450 };               // the button panel of the menu's background
static const RECT Big1 = { 357, 48, 357 + 217, 48 + 66 }, Mid1 = { 359, 142, 359 + 213, 142 + 48 }, Mid2 = { 359, 212, 359 + 213, 212 + 48 },
                  Big2 = { 357, 287, 357 + 217, 287 + 66 }, Pill = { 401, 380, 401 + 127, 380 + 29 },
                  Left = { 325, 412, 325 + 92, 412 + 29 }, Center = { 419, 412, 419 + 93, 412 + 29 }, Right = { 514, 412, 514 + 89, 412 + 29 };
extern "C" __declspec(dllexport) int MenuPageOpen() { return page ? 1 : 0; }
extern "C" __declspec(dllexport) void SettingsOpen() { page = false; Open(); }

static void PageClick(int x, int y)
{
    auto go = [](const char* dll, const char* fn) { if (ui::Call(dll, fn)) api->play_sound(13); };
    if (In(Big1, x, y)) go("coop.dll", "CoopOpen");
    else if (In(Mid1, x, y)) go("extramodes.dll", "ExtraModesOpen");
    else if (In(Mid2, x, y)) go("achievements.dll", "AchievementsOpen");
    else if (In(Big2, x, y)) Open();
    else if (In(Pill, x, y)) { page = false; api->redraw(); }
    else if (In(Left, x, y)) Open(AboutTab);
    else if (In(Center, x, y)) Open(3);
    else if (In(Right, x, y)) go("extramodes.dll", "RecordsOpen");
}

static void DrawPage(void* g)
{
    void* bg = ui::Image(IMAGE_SELECTORSCREEN);
    api->draw_image_part(g, bg, PanelRect.left, PanelRect.top, PanelRect.left, PanelRect.top, ui::W(PanelRect), ui::H(PanelRect));
    ui::MenuButton(api, g, Big1, "Co-op", true, ui::Has("coop.dll", "CoopOpen"), "2-4 players");
    ui::MenuButton(api, g, Mid1, "Extra Modes", false, ui::Has("extramodes.dll", "ExtraModesOpen"));
    ui::MenuButton(api, g, Mid2, "Achievements", false, ui::Has("achievements.dll", "AchievementsOpen"));
    ui::MenuButton(api, g, Big2, "Mod Settings", true, true, "F2");
    ui::Button(api, g, Pill, "Main Menu", ui::Look::Main);
    ui::Button(api, g, Left, "About", ui::Look::Left);
    ui::Button(api, g, Center, "Mods", ui::Look::Center);
    ui::Button(api, g, Right, "Records", ui::Look::Right, ui::Has("extramodes.dll", "RecordsOpen"));
}

// ---- input ---------------------------------------------------------------------------------------------------------------
static int Mouse(int x, int y, int button, int down)
{
    if (!open)
    {
        if (!ui::OnMainMenu(api)) { if (page && !App()) page = false; return 0; }
        if (page)
        {
            if (!In(PanelRect, x, y)) return 0;   // the rest of the menu (the "not you?" link) still works
            if (down && button == 0) PageClick(x, y);
            api->redraw();
            return 1;
        }
        if (down && button == 0 && In(EntryRect, x, y)) { page = true; api->play_sound(13); api->redraw(); return 1; }
        return In(EntryRect, x, y) ? 1 : 0;
    }
    if (!Dialog()) { open = false; return 0; }
    if (y >= DY + DH - 70) return 0;   // the DONE button belongs to the dialog
    if (!down || button != 0) return In(RECT{ DX, DY, DX + DW, DY + DH }, x, y) ? 1 : 0;
    for (int i = 0; i < TabCount; i++) if (In(TabRect(i), x, y)) { tab = i; hover = -1; api->redraw(); return 1; }
    if (tab == 3 && AnyPending() && In(RestartRect(), x, y))
    {
        ui::KillDialog(api, DialogId);
        open = false;
        api->config_changed();
        api->restart();
        return 1;
    }
    if (tab == 3)
        for (size_t i = 0; i < tabs[3].size(); i++)
            if (CanOpen(i) && In(OpenRect(RowRect(3, (int)i)), x, y))
            {
                ui::KillDialog(api, DialogId);
                open = false;
                std::string dll = DllOf(i);
                ui::Call(dll.c_str(), "RemodOpen");
                return 1;
            }
    if (tab != AboutTab)
        for (size_t i = 0; i < tabs[tab].size(); i++)
            if (In(RowRect(tab, (int)i), x, y)) { Toggle(tabs[tab][i]); api->redraw(); return 1; }
    return 1;
}

static int Key(int vk, int down)
{
    if (!down) return 0;
    if (vk == VK_F2 && !open && !Coop()) { page = false; Open(); return 1; }
    if (vk == VK_ESCAPE && page && !open && ui::DialogCount(api) == 0) { page = false; api->redraw(); return 1; }
    return 0;
}

// ---- drawing -------------------------------------------------------------------------------------------------------------
static void DrawSettings(void* g)
{
    void* f12 = ui::F12(), *f10 = ui::F10();
    for (int i = 0; i < TabCount; i++) ui::Button(api, g, TabRect(i), TabNames[i], ui::Look::Main, true, i == tab);
    int helpY = DY + DH - 92;
    if (tab == AboutTab)
    {
        int y = TabY + 54;
        api->draw_text_font(g, f12, "Insaniquarium - Remastered Mod " REMOD_VERSION, CX, y, ui::Yellow);
        y = ui::WrapText(api, g, f10, "An unofficial fan mod for Insaniquarium! Deluxe 1.1, not affiliated with PopCap Games or EA.", CX, y + 18, CW, ui::White) + 6;
        static const char* const Lines[] = {
            "F2: these settings (anywhere).  F7: co-op (on the main menu).",
            "Hold Shift while the game starts to play without mods this time.",
            "Everything the mod does is written to mods\\remastered-mod.log in the game folder.",
            "MIT licence. Uses MinHook (BSD-2-Clause). Insaniquarium and its art belong to PopCap Games.",
        };
        for (const char* l : Lines) y = ui::WrapText(api, g, f10, l, CX, y + 4, CW, ui::Cream, 2) + 2;
        return;
    }
    const char* help = nullptr;
    void* checked = ui::Image(IMAGE_CHECKED), *unchecked = ui::Image(IMAGE_UNCHECKED);
    int cols = Columns(tab);
    for (size_t i = 0; i < tabs[tab].size(); i++)
    {
        Setting& it = tabs[tab][i];
        RECT r = RowRect(tab, (int)i);
        bool over = ui::Hover(api, r);
        if (over) help = it.help;
        std::string v = Get(it);
        int rh = r.bottom - r.top;
        int tx = r.left + 6, labelY = tab == 3 ? r.top + (rh >= 40 ? 22 : rh / 2 - 1) : r.top + 28, right = r.right;
        if (it.choices.empty())
        {
            void* img = v != "0" ? checked : unchecked;
            if (rh >= 44 || api->version < 7) { api->draw_image(g, img, r.left, r.top + (44 - ui::ImgH(img)) / 2); tx = r.left + ui::ImgW(img) + 4; }
            else
            {   // a compact row: the game's checkbox drawn smaller
                float sc = (float)(rh - 2) / ui::ImgH(img);
                api->draw_image_scaled(g, img, r.left, r.top + 1, 0, 0, ui::ImgW(img), ui::ImgH(img), sc);
                tx = r.left + (int)(ui::ImgW(img) * sc) + 4;
            }
        }
        else
        {
            const char* shown = it.choices[0].first;
            for (auto& c : it.choices) if (v == c.second) shown = c.first;
            RECT vr = ValueRect(r, shown);
            ui::Button(api, g, vr, shown, ui::Look::Center);
            right = vr.left - 6;
        }
        if (tab == 3 && CanOpen(i)) { RECT orr = OpenRect(r); ui::Button(api, g, orr, "Open", ui::Look::Center); right = orr.left - 4; }
        void* font = cols == 3 || tab == 3 ? f10 : f12;
        ui::FitText(api, g, font, it.label, tx, labelY, right - tx, over ? ui::White : ui::Cream);
        if (tab == 3 && i < modState.size())
        {
            unsigned col = modState[i] == REMOD_MOD_ON && !Pending(i) ? 0xff9cf09c : Pending(i) ? 0xffffe060 : 0xffffb0a0;
            ui::FitText(api, g, f10, StateText(i), tx, labelY + (rh >= 40 ? 14 : 12), right - tx, col);
        }
    }
    if (tab == 3 && tabs[3].empty()) api->draw_text_font(g, f12, "No other mods installed.", CX, TabY + 60, ui::White);
    if (tab == 3 && AnyPending()) ui::Button(api, g, RestartRect(), "Restart the game now", ui::Look::Center);
    // the help line for the row under the pointer (wrapped, two lines at most)
    std::string line = help ? help : (tab == 2 ? "Mutators apply from the next level you start."
                                    : tab == 3 ? "Click a mod to switch it off or on: it changes when the game restarts. Open: the mod's own screen." : "Click a setting to change it.");
    if (help && tab != 2 && tab != 3)
        for (auto& it : tabs[tab]) if (it.help == help && it.restart) line += " (applies after a restart)";
    ui::WrapText(api, g, f10, line, CX, helpY, tab == 3 && AnyPending() ? CW - 210 : CW, ui::Cream, 2);
}

static int lastHover = -1;
static void Overlay(void* g)
{
    if (!App()) return;
    if (open)
    {
        if (Dialog()) DrawSettings(g); else open = false;
    }
    else if (ui::OnMainMenu(api))
    {
        if (page) DrawPage(g);
        else ui::Button(api, g, EntryRect, "Remastered", ui::Look::Main);
    }
    else if (page && !api->board() && !at<void*>(App(), App_mGameSelector)) page = false;
    ui::DrawTooltip(api, g);
    // the hover look changes with the pointer: repaint when what's under it changes
    int mx, my;
    api->mouse_pos(&mx, &my);
    int h = (mx / 8) * 1000 + my / 8;
    if (h != lastHover) { lastHover = h; if (open || page) api->redraw(); }
}

static void Changed() {}

extern "C" __declspec(dllexport) const char* RemodDescribe() { return "The settings page (F2) and the main menu's Remastered page."; }

extern "C" __declspec(dllexport) int RemodInit(const RemodApi* a)
{
    if (a->version < 4) return 0;
    api = a;
    char p[MAX_PATH];
    HMODULE self;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&Open, &self);
    GetModuleFileNameA(self, p, MAX_PATH);
    modsDir = p;
    modsDir = modsDir.substr(0, modsDir.find_last_of("\\/"));
    if (!api->hook((void*)App_ButtonDepress, (void*)&ButtonHook, (void**)&oButton)) return 0;
    api->on_mouse(Mouse);
    api->on_key(Key);
    api->on_overlay(Overlay);
    api->on_config(Changed);
    return 1;
}
