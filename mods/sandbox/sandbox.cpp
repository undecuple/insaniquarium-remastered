// sandbox: the game's hidden sandbox (game mode 3: $999,999, normally reached with a secret code) as a proper mode,
// started from Extra Modes. A palette (the "Palette" button in the tank, or Tab) places fish, aliens and pets where
// you click; a Remove brush (or Delete over a thing), Clear, backdrop switching, a Hunger switch (off: fish never get
// hungry) and three save slots (userdata\sandbox1-3.txt: one line per thing).
#include "remod.h"
#include "game.h"
#include "remodui.h"
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>

using namespace game;
static const RemodApi* api;
static std::string userdata;
static bool paletteOpen, hunger;
static std::string brush, brushLabel;

struct Cell { RECT r; std::string brush, label; };
static const RECT PaletteButton = { 0x26, 0x4c, 0x26 + 0x8c, 0x4c + 0x16 };
static const int PalX = 0x1c, PalY = 0x6a, PalW = 0x248, Cols = 5, CellW = PalW / Cols, CellH = 0x13, HeadH = 0x12;

static const char* FishItems[][2] = { { "guppy", "Guppy" }, { "bigguppy", "Big guppy" }, { "star", "King guppy" }, { "breeder", "Breeder" },
    { "oscar", "Carnivore" }, { "ultra", "Ultravore" }, { "penta", "Starcatcher" }, { "grubber", "Beetlemuncher" }, { "gekko", "Guppycruncher" } };
static const char* AlienItems[][2] = { { "alien2", "Sylvester" }, { "alien3", "Balrog" }, { "alien4", "Gus" }, { "alien5", "Destructor" },
    { "alien6", "Ulysses" }, { "alien7", "Psychosquid" }, { "alien8", "Bilaterus" } };
static const char* PetNames[24] = { "Stinky", "Niko", "Itchy", "Prego", "Zorf", "Clyde", "Vert", "Rufus", "Meryl", "Wadsworth", "Seymour", "Shrapnel",
    "Gumbo", "Blip", "Rhubarb", "Nimbus", "Amp", "Gash", "Angie", "Presto", "Brinkley", "Nostradamus", "Stanley", "Walter" };
static const char* ToolItems[][2] = { { "feed", "Feed (no brush)" }, { "remove", "Remove (Del)" }, { "clear", "Clear tank" }, { "prevtank", "< Backdrop" },
    { "nexttank", "Backdrop >" }, { "save1", "Save 1" }, { "save2", "Save 2" }, { "save3", "Save 3" }, { "hunger", "Hunger" }, { "close", "Close" },
    { "load1", "Load 1" }, { "load2", "Load 2" }, { "load3", "Load 3" } };

static bool On()
{
    void* a = api->app();
    return a && api->board() && at<int>(a, App_mGameMode) == 3 && !at<bool>(a, App_mIsScreenSaver);
}

static std::vector<Cell> Cells(int& height, std::vector<std::pair<int, const char*>>& heads)
{
    std::vector<Cell> cells;
    heads.clear();
    int y = PalY + 6;
    auto section = [&](const char* head, std::vector<std::pair<std::string, std::string>> items) {
        heads.push_back({ y, head });
        y += HeadH;
        for (size_t i = 0; i < items.size(); i++)
        {
            int x = PalX + (int)(i % Cols) * CellW, cy = y + (int)(i / Cols) * CellH;
            cells.push_back({ { x, cy, x + CellW, cy + CellH }, items[i].first, items[i].second });
        }
        y += (int)((items.size() + Cols - 1) / Cols) * CellH + 4;
    };
    std::vector<std::pair<std::string, std::string>> v;
    for (auto& f : FishItems) v.push_back({ f[0], f[1] });
    section("Fish", v); v.clear();
    for (auto& a : AlienItems) v.push_back({ a[0], a[1] });
    section("Aliens", v); v.clear();
    for (int i = 0; i < 24; i++) v.push_back({ "pet" + std::to_string(i), PetNames[i] });
    section("Pets", v); v.clear();
    for (auto& t : ToolItems) v.push_back({ t[0], t[1] });
    section("Tools", v);
    height = y - PalY + 2;
    return cells;
}

// ---- placing things ------------------------------------------------------------------------------------------------------
typedef void(__thiscall* XYFacingFn)(void*, int, int, bool);
typedef void(__thiscall* XFn)(void*, int);
static void* Last(void* b, int list) { int n = Count(b, list); return n ? Item(b, list, n - 1) : nullptr; }

static void Place(void* b, const std::string& what, int x, int y)
{
    int fx = std::min(std::max(x - 40, 0x14), 0x22c), fy = std::min(std::max(y - 40, 0x46), 0x17c);
    if (what == "guppy" || what == "bigguppy" || what == "star")
    {
        void* f = reinterpret_cast<void*(__thiscall*)(void*, int, int)>(Board_AddGuppyAt)(b, fx, fy);
        if (f && what != "guppy") at<int>(f, 0x1a0) = what == "bigguppy" ? 2 : 3;   // mSize
    }
    else if (what == "breeder") reinterpret_cast<void*(__thiscall*)(void*, int, int)>(Board_AddBreederAt)(b, fx, fy);
    else if (what == "oscar") reinterpret_cast<XYFacingFn>(Board_AddOscarAt)(b, fx, fy, true);
    else if (what == "ultra") reinterpret_cast<XYFacingFn>(Board_AddUltraAt)(b, fx - 40, fy - 40, true);
    else if (what == "gekko") reinterpret_cast<XYFacingFn>(Board_AddGekkoAt)(b, fx, fy, true);
    else if (what == "penta") reinterpret_cast<XFn>(Board_AddPentaAt)(b, fx);
    else if (what == "grubber") reinterpret_cast<XFn>(Board_AddGrubberAt)(b, fx);
    else if (what.rfind("alien", 0) == 0)
    {
        int type = atoi(what.c_str() + 5);
        if (Count(b, Board_mAliens) > 0 && at<int>(Item(b, Board_mAliens, 0), Alien_mAlienType) == 4 && type != 4) { api->toast("Gus has to go first"); return; }
        at<int>(b, Board_mAlienType) = type;
        reinterpret_cast<void(__thiscall*)(void*, int, int, int, bool)>(Board_SpawnAlien)(b, type, std::min(std::max(x - 80, 0x14), 0x1d6), std::min(std::max(y - 80, 0x46), 0x12c), true);
    }
    else if (what.rfind("pet", 0) == 0)
        reinterpret_cast<void*(__thiscall*)(void*, int, int, int, bool, bool)>(Board_AddPet)(b, atoi(what.c_str() + 3), fx, fy, false, false);
}

static const int Placeable[] = { Board_mGuppies, Board_mOscars, Board_mUltras, Board_mGekkos, Board_mPentas, Board_mGrubbers, Board_mBreeders,
                                 Board_mFishPets, 0xb0 /* mOtherPets */, Board_mAliens };
static void Remove(void* o) { reinterpret_cast<void(__thiscall*)(void*, bool)>(GameObject_RemoveFromGame)(o, true); }

static void RemoveAt(void* b, int x, int y)
{
    for (int l : Placeable)
        for (int i = Count(b, l) - 1; i >= 0; i--)
        {
            void* o = Item(b, l, i);
            int ox = at<int>(o, Widget_mX), oy = at<int>(o, Widget_mY);
            if (x >= ox && x < ox + at<int>(o, Widget_mWidth) && y >= oy && y < oy + at<int>(o, Widget_mHeight)) { Remove(o); return; }
        }
}

static void Clear(void* b)
{
    for (int l : Placeable) while (Count(b, l)) Remove(Last(b, l));
    for (int l : { Board_mCoins, Board_mFood }) while (Count(b, l)) Remove(Last(b, l));
}

// ---- save slots (one line per thing: "guppy SIZE GOLDEN X Y", "fish TYPE X Y", "pet N X Y", "alien TYPE X Y", "backdrop N")
static std::string SlotFile(int n) { return userdata + "\\sandbox" + std::to_string(n) + ".txt"; }

static void Save(void* b, int slot)
{
    std::vector<std::string> lines = { "# Insaniquarium - Remastered Mod sandbox layout", "backdrop " + std::to_string(at<int>(b, Board_mBackdrop)) };
    auto pos = [](void* o) { return " " + std::to_string(at<int>(o, Widget_mX)) + " " + std::to_string(at<int>(o, Widget_mY)); };
    for (int i = 0; i < Count(b, Board_mGuppies); i++) { void* o = Item(b, Board_mGuppies, i); lines.push_back("guppy " + std::to_string(at<int>(o, 0x1a0)) + " 0" + pos(o)); }
    for (int l : { Board_mOscars, Board_mUltras, Board_mGekkos, Board_mPentas, Board_mGrubbers, Board_mBreeders })
        for (int i = 0; i < Count(b, l); i++) { void* o = Item(b, l, i); lines.push_back("fish " + std::to_string(at<int>(o, GameObject_mType)) + pos(o)); }
    for (int i = 0; i < Count(b, Board_mFishPets); i++) { void* o = Item(b, Board_mFishPets, i); lines.push_back("pet " + std::to_string(at<int>(o, 0x234)) + pos(o)); }
    for (int i = 0; i < Count(b, 0xb0); i++) { void* o = Item(b, 0xb0, i); lines.push_back("pet " + std::to_string(at<int>(o, 0x1c4)) + pos(o)); }
    for (int i = 0; i < Count(b, Board_mAliens); i++) { void* o = Item(b, Board_mAliens, i); lines.push_back("alien " + std::to_string(at<int>(o, Alien_mAlienType)) + pos(o)); }
    CreateDirectoryA(userdata.c_str(), nullptr);
    std::ofstream out(SlotFile(slot), std::ios::trunc);
    for (auto& l : lines) out << l << "\n";
    char t[64];
    snprintf(t, sizeof t, "Saved to slot %d (%d things)", slot, (int)lines.size() - 2);
    api->toast(t);
}

static void Load(void* b, int slot)
{
    std::ifstream in(SlotFile(slot));
    if (!in) { api->toast(("Slot " + std::to_string(slot) + " is empty").c_str()); return; }
    Clear(b);
    std::string line;
    int n = 0;
    while (std::getline(in, line))
    {
        std::istringstream s(line);
        std::string kind;
        if (!(s >> kind) || kind[0] == '#') continue;
        int v[4] = {};
        for (int& x : v) s >> x;
        if (kind == "backdrop") reinterpret_cast<XFn>(Board_SetBackdrop)(b, v[0]);
        else if (kind == "guppy") { Place(b, v[0] >= 3 ? "star" : v[0] == 2 ? "bigguppy" : "guppy", v[2] + 40, v[3] + 40); n++; }
        else if (kind == "fish")
        {
            const char* names[] = { nullptr, nullptr, nullptr, nullptr, nullptr, "oscar", "ultra", "gekko", "penta", "grubber", "breeder" };
            if (v[0] >= 5 && v[0] <= 10) { Place(b, names[v[0]], v[1] + 40 + (v[0] == 6 ? 40 : 0), v[2] + 40 + (v[0] == 6 ? 40 : 0)); n++; }
        }
        else if (kind == "pet" && v[0] >= 0 && v[0] < 24) { Place(b, "pet" + std::to_string(v[0]), v[1] + 40, v[2] + 40); n++; }
        else if (kind == "alien" && v[0] >= 1 && v[0] <= 8) { Place(b, "alien" + std::to_string(v[0]), v[1] + 80, v[2] + 80); n++; }
    }
    char t[64];
    snprintf(t, sizeof t, "Loaded slot %d (%d things)", slot, n);
    api->toast(t);
}

// ---- input ---------------------------------------------------------------------------------------------------------------
static void Choose(void* b, const std::string& what, const std::string& label)
{
    if (what == "feed") { brush.clear(); paletteOpen = false; }
    else if (what == "close") paletteOpen = false;
    else if (what == "hunger")
    {
        hunger = !hunger;
        api->config_set("sandbox", "hunger", hunger ? "1" : "0");
        api->toast(hunger ? "Hunger on: feed your fish" : "Hunger off: fish stay fed");
    }
    else if (what == "clear") { Clear(b); api->toast("Tank cleared"); }
    else if (what == "prevtank" || what == "nexttank")
    {
        int bd = at<int>(b, Board_mBackdrop) + (what == "nexttank" ? 1 : -1);
        reinterpret_cast<XFn>(Board_SetBackdrop)(b, (bd + 5) % 5);
    }
    else if (what.rfind("save", 0) == 0) Save(b, what[4] - '0');
    else if (what.rfind("load", 0) == 0) { Load(b, what[4] - '0'); paletteOpen = false; }
    else { brush = what; brushLabel = label; paletteOpen = false; }
    api->play_sound(13);
    api->redraw();
}

static int Mouse(int x, int y, int button, int down)
{
    if (!On() || ui::DialogCount(api) > 0) return 0;
    void* b = api->board();
    if (!down) return 0;
    if (button == 1 && !brush.empty()) { brush.clear(); api->redraw(); return 1; }   // right click drops the brush
    if (button != 0) return 0;
    if (ui::In(PaletteButton, x, y)) { paletteOpen = !paletteOpen; api->play_sound(13); api->redraw(); return 1; }
    if (paletteOpen)
    {
        int h;
        std::vector<std::pair<int, const char*>> heads;
        for (auto& c : Cells(h, heads)) if (ui::In(c.r, x, y)) { Choose(b, c.brush, c.label); return 1; }
        if (!ui::In(RECT{ PalX, PalY, PalX + PalW, PalY + h }, x, y)) { paletteOpen = false; api->redraw(); }
        return 1;
    }
    if (brush.empty() || y < 0x46 || y > 0x1d0 || x < 0x14 || x > 0x26c) return 0;
    if (brush == "remove") RemoveAt(b, x, y); else Place(b, brush, x, y);
    return 1;
}

static int Key(int vk, int down)
{
    if (!down || !On() || ui::DialogCount(api) > 0) return 0;
    if (vk == VK_TAB) { paletteOpen = !paletteOpen; api->redraw(); return 1; }
    if (vk == VK_DELETE && !paletteOpen) { int x, y; api->mouse_pos(&x, &y); RemoveAt(api->board(), x, y); return 1; }
    if (vk == VK_ESCAPE && (paletteOpen || !brush.empty())) { paletteOpen = false; brush.clear(); api->redraw(); return 1; }
    return 0;
}

// hunger off: the hunger timer stays full
static void(__thiscall* oTickHunger)(void*);
static void __fastcall TickHunger(void* o, void*)
{
    if (!hunger && On()) { int& t = at<int>(o, GameObject_mHungerTimer); if (t < 400) t = 400; return; }
    oTickHunger(o);
}

// an empty tank isn't a game over in the sandbox: no dialog, the tank keeps running
static void(__thiscall* oDialog)(void*, int, bool, const MsvcString*, const MsvcString*, const MsvcString*, int);
static void __fastcall DialogHook(void* a, void*, int id, bool modal, const MsvcString* h, const MsvcString* l, const MsvcString* f, int buttons)
{
    if ((id == 0x10 || id == 0x11) && On())
    {
        void* b = api->board();
        at<bool>(b, 0x4ee) = true;   // mNeedSave: the game cleared it for the game over
        reinterpret_cast<void(__thiscall*)(void*, bool)>(Board_Pause)(b, false);
        return;
    }
    oDialog(a, id, modal, h, l, f, buttons);
}

static void Overlay(void* g)
{
    if (!On() || ui::DialogCount(api) > 0) return;
    void* f10 = ui::Font(FONT_JUNGLEFEVER10OUTLINE), *f12 = ui::Font(FONT_JUNGLEFEVER12OUTLINE);
    ui::Button(api, g, PaletteButton, paletteOpen ? "Close palette" : "Palette (Tab)", ui::Look::Main, true, paletteOpen, f10);
    if (!brush.empty() && !paletteOpen)
    {
        std::string s = "Placing: " + brushLabel + "  (right click to stop)";
        int w = api->text_width_font(f10, s.c_str()) + 12;
        api->fill_rect(g, PaletteButton.right + 8, PaletteButton.top + 2, w, 18, 0x8c000000);
        api->draw_text_font(g, f10, s.c_str(), PaletteButton.right + 14, PaletteButton.bottom - 6, 0xffffffff);
    }
    if (!paletteOpen) return;
    int h, mx, my;
    std::vector<std::pair<int, const char*>> heads;
    auto cells = Cells(h, heads);
    api->mouse_pos(&mx, &my);
    api->fill_rect(g, PalX - 4, PalY, PalW + 8, h, 0xe1101840);
    for (auto& hd : heads) api->draw_text_font(g, f12, hd.second, PalX + 4, hd.first + HeadH - 3, 0xffffff64);
    for (auto& c : cells)
    {
        bool over = ui::In(c.r, mx, my), sel = c.brush == brush;
        if (over || sel) api->fill_rect(g, c.r.left + 1, c.r.top + 1, CellW - 2, CellH - 2, sel ? 0xa040a040 : 0x32ffffff);
        std::string label = c.brush == "hunger" ? (hunger ? "Hunger: on" : "Hunger: off") : c.label;
        api->draw_text_font(g, f10, label.c_str(), c.r.left + 4, c.r.bottom - 5, over ? 0xffffff80 : 0xffffffff);
    }
}

// started from Extra Modes
extern "C" __declspec(dllexport) void SandboxStart()
{
    void* a = api->app();
    if (!a) return;
    paletteOpen = true; brush.clear();
    at<bool>(a, 0x880) = false;
    reinterpret_cast<void(__thiscall*)(void*)>(App_RemoveGameSelector)(a);
    at<int>(a, App_mGameMode) = 3;
    reinterpret_cast<void(__thiscall*)(void*, bool, bool)>(App_StartGame)(a, false, false);
}

static void Settings() { hunger = api->config_int("sandbox", "hunger", 0) != 0; }

extern "C" __declspec(dllexport) const char* RemodDescribe() { return "Build your own tank: place fish, pets and aliens (Extra Modes > Sandbox)."; }

extern "C" __declspec(dllexport) int RemodInit(const RemodApi* a)
{
    if (a->version < 4) return 0;
    api = a;
    char base[MAX_PATH];
    if (SHGetFolderPathA(nullptr, CSIDL_COMMON_APPDATA, nullptr, 0, base) != S_OK) return 0;
    userdata = std::string(base) + "\\PopCap Games\\Insaniquarium\\userdata";
    Settings();
    if (!api->hook((void*)GameObject_TickHunger, (void*)&TickHunger, (void**)&oTickHunger)
        || !api->hook((void*)App_DoTimedDialog, (void*)&DialogHook, (void**)&oDialog)) return 0;
    api->on_config(Settings);
    api->on_mouse(Mouse);
    api->on_key(Key);
    api->on_overlay(Overlay);
    return 1;
}
