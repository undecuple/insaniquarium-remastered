// UI for mods, in the game's own look: screens on the game's dialog (modal: the game below gets no clicks and the
// tank pauses), the game's strip buttons (normal / over / down images, Jungle Fever labels: yellow, white when the
// pointer is over), the main menu's big buttons, its edit box, and tooltips. Header-only; needs remod.h version 3.
#pragma once
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <algorithm>
#include "remod.h"
#include "game.h"

namespace ui {

inline bool In(const RECT& r, int x, int y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
inline void* Font(uintptr_t global) { return *reinterpret_cast<void**>(global); }
inline void* Image(uintptr_t global) { return *reinterpret_cast<void**>(global); }
inline void** Vt(void* o) { return *reinterpret_cast<void***>(o); }
inline int W(const RECT& r) { return r.right - r.left; }
inline int H(const RECT& r) { return r.bottom - r.top; }
inline int ImgW(void* img) { return img ? game::at<int>(img, game::Image_mWidth) : 0; }
inline int ImgH(void* img) { return img ? game::at<int>(img, game::Image_mHeight) : 0; }
inline int Ascent(void* f) { return f ? reinterpret_cast<int(__thiscall*)(void*)>(Vt(f)[game::Font_vGetAscent / 4])(f) : 10; }
inline int AscentPadding(void* f) { return f ? reinterpret_cast<int(__thiscall*)(void*)>(Vt(f)[game::Font_vGetAscentPadding / 4])(f) : 0; }
inline int FontHeight(void* f) { return f ? reinterpret_cast<int(__thiscall*)(void*)>(Vt(f)[game::Font_vGetHeight / 4])(f) : 12; }
inline void* F10() { return Font(game::FONT_JUNGLEFEVER10OUTLINE); }
inline void* F12() { return Font(game::FONT_JUNGLEFEVER12OUTLINE); }
inline void* F15() { return Font(game::FONT_JUNGLEFEVER15OUTLINE); }
const unsigned Yellow = 0xfffff000, White = 0xffffffff, Lilac = 0xffc8c8ff, Grey = 0xff9a8a70, Cream = 0xffffe8a0;

inline void Mouse(const RemodApi* api, int& x, int& y) { api->mouse_pos(&x, &y); }
inline bool Hover(const RemodApi* api, const RECT& r) { int x, y; Mouse(api, x, y); return In(r, x, y); }
inline bool Pressed() { return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0; }

// Graphics::DrawImageBox: an image (or a cel of it) stretched to a box: corners as they are, edges and centre tiled
inline void ImageBox(const RemodApi* api, void* g, void* img, int sx, int sy, int sw, int sh, const RECT& d)
{
    if (!img) return;
    int cw = sw / 3, ch = sh / 3, mw = sw - 2 * cw, mh = sh - 2 * ch, dw = W(d), dh = H(d);
    if (dw < 2 * cw || dh < 2 * ch || mw <= 0 || mh <= 0) { api->draw_image_part(g, img, d.left, d.top, sx, sy, sw, sh); return; }
    auto part = [&](int x, int y, int px, int py, int pw, int ph) { if (pw > 0 && ph > 0) api->draw_image_part(g, img, x, y, px, py, pw, ph); };
    // corners
    part(d.left, d.top, sx, sy, cw, ch);
    part(d.right - cw, d.top, sx + cw + mw, sy, cw, ch);
    part(d.left, d.bottom - ch, sx, sy + ch + mh, cw, ch);
    part(d.right - cw, d.bottom - ch, sx + cw + mw, sy + ch + mh, cw, ch);
    // edges (the last tile cut to fit)
    for (int x = d.left + cw; x < d.right - cw; x += mw)
    {
        int w = (std::min)(mw, (int)(d.right - cw - x));
        part(x, d.top, sx + cw, sy, w, ch);
        part(x, d.bottom - ch, sx + cw, sy + ch + mh, w, ch);
    }
    for (int y = d.top + ch; y < d.bottom - ch; y += mh)
    {
        int h = (std::min)(mh, (int)(d.bottom - ch - y));
        part(d.left, y, sx, sy + ch, cw, h);
        part(d.right - cw, y, sx + cw + mw, sy + ch, cw, h);
        for (int x = d.left + cw; x < d.right - cw; x += mw)
            part(x, y, sx + cw, sy + ch, (std::min)(mw, (int)(d.right - cw - x)), h);
    }
}

// ---- tooltips (one per frame: set while drawing, shown by DrawTooltip at the end of the overlay) ----------------------------
inline std::string& TipText() { static std::string t; return t; }
inline void Tooltip(const std::string& text) { TipText() = text; }

// splits text into lines no wider than width (on spaces; '\n' breaks too)
inline std::vector<std::string> Wrap(const RemodApi* api, void* font, const std::string& text, int width)
{
    std::vector<std::string> lines;
    std::string line, word;
    auto flushWord = [&]() {
        if (word.empty()) return;
        std::string t = line.empty() ? word : line + " " + word;
        if (!line.empty() && api->text_width_font(font, t.c_str()) > width) { lines.push_back(line); line = word; }
        else line = t;
        word.clear();
    };
    for (char c : text)
    {
        if (c == ' ') flushWord();
        else if (c == '\n') { flushWord(); lines.push_back(line); line.clear(); }
        else word += c;
    }
    flushWord();
    if (!line.empty()) lines.push_back(line);
    return lines;
}

// text in lines no wider than width from (x, baseline y); returns the y after the last line
inline int WrapText(const RemodApi* api, void* g, void* font, const std::string& text, int x, int y, int width, unsigned color, int maxLines = 99)
{
    int lh = FontHeight(font) + 1, n = 0;
    for (auto& l : Wrap(api, font, text, width))
    {
        if (++n > maxLines) break;
        api->draw_text_font(g, font, l.c_str(), x, y, color);
        y += lh;
    }
    return y;
}

// a line of text that fits in width: cut with "..." when too long, the whole text as a tooltip under the pointer
inline void FitText(const RemodApi* api, void* g, void* font, const std::string& text, int x, int y, int width, unsigned color)
{
    if (api->text_width_font(font, text.c_str()) <= width) { api->draw_text_font(g, font, text.c_str(), x, y, color); return; }
    std::string t = text;
    while (!t.empty() && api->text_width_font(font, (t + "...").c_str()) > width) t.pop_back();
    while (!t.empty() && t.back() == ' ') t.pop_back();
    api->draw_text_font(g, font, (t + "...").c_str(), x, y, color);
    if (Hover(api, RECT{ x, y - Ascent(font), x + width, y + 4 })) Tooltip(text);
}

// the tooltip of this frame, near the pointer: a dark panel with a brass edge, the text wrapped
inline void DrawTooltip(const RemodApi* api, void* g)
{
    std::string& t = TipText();
    if (t.empty()) return;
    void* f = F10();
    auto lines = Wrap(api, f, t, 260);
    int lh = FontHeight(f) + 1, w = 0;
    for (auto& l : lines) w = (std::max)(w, api->text_width_font(f, l.c_str()));
    int mx, my;
    Mouse(api, mx, my);
    int bw = w + 16, bh = (int)lines.size() * lh + 10;
    int x = (std::min)(mx + 14, 636 - bw), y = my + 20 + bh > 478 ? my - bh - 8 : my + 20;
    api->fill_rect(g, x, y, bw, bh, 0xffb08a40);
    api->fill_rect(g, x + 1, y + 1, bw - 2, bh - 2, 0xf0281808);
    int ty = y + 5 + Ascent(f);
    for (auto& l : lines) { api->draw_text_font(g, f, l.c_str(), x + 8, ty, White); ty += lh; }
    t.clear();
}

// ---- buttons ---------------------------------------------------------------------------------------------------------------
// a label centred in a box the way the game's buttons do it
inline void Label(const RemodApi* api, void* g, void* font, const char* text, const RECT& r, unsigned color, int dx = 0, int dy = 0)
{
    int a = Ascent(font);
    int x = r.left + (W(r) - api->text_width_font(font, text)) / 2 + dx;
    int y = r.top + (H(r) + a - AscentPadding(font) - a / 6 - 1) / 2 + dy;
    api->draw_text_font(g, font, text, x, y, color);
}

enum class Look { Center, Main, Left, Right, Dialog, Fat };   // which of the game's strip images
inline void* LookImage(Look l)
{
    switch (l)
    {
        case Look::Main: return Image(game::IMAGE_MAINBUTTON);
        case Look::Left: return Image(game::IMAGE_LEFTBUTTON);
        case Look::Right: return Image(game::IMAGE_RIGHTBUTTON);
        case Look::Dialog: return Image(game::IMAGE_DIALOGBUTTON);
        case Look::Fat: return Image(game::IMAGE_FATBUTTON);
        default: return Image(game::IMAGE_CENTERBUTTON);
    }
}

// one of the game's strip buttons (DialogButton): selected draws it held down (tabs, toggles); disabled: greyed label.
// The box is as high as the image. Returns whether the pointer is over it.
inline bool Button(const RemodApi* api, void* g, const RECT& r0, const char* text, Look look = Look::Center, bool enabled = true,
                   bool selected = false, void* font = nullptr)
{
    void* img = LookImage(look);
    if (!font) font = F10();
    int cw = ImgW(img) / 3, ch = ImgH(img);
    RECT r = { r0.left, r0.top + (H(r0) - ch) / 2, r0.right, r0.top + (H(r0) - ch) / 2 + ch };
    bool over = enabled && Hover(api, r0), down = (over && Pressed()) || selected;
    ImageBox(api, g, img, down ? cw * 2 : over ? cw : 0, 0, cw, ch, r);
    Label(api, g, font, text, r, !enabled ? Grey : over ? White : Yellow, down ? 1 : 0, (down ? 1 : 0) - 2);
    return over;
}

// the main menu's big buttons (ButtonWidget over the background's own button shapes): an over / down image when the
// pointer is on it, the label in Jungle Fever 15 (yellow; lilac when over)
inline bool MenuButton(const RemodApi* api, void* g, const RECT& r, const char* text, bool big, bool enabled = true, const char* sub = nullptr)
{
    bool over = enabled && Hover(api, r), down = over && Pressed();
    void* img = Image(big ? (down ? game::IMAGE_BATTLETANKBUTTOND : game::IMAGE_BATTLETANKBUTTON) : (down ? game::IMAGE_MIDDLEBUTTOND : game::IMAGE_MIDDLEBUTTON));
    if (over) api->draw_image(g, img, r.left, r.top);
    unsigned c = !enabled ? Grey : over ? Lilac : Yellow;
    if (sub)
    {
        RECT top = { r.left, r.top, r.right, r.top + H(r) * 2 / 3 };
        Label(api, g, F15(), text, top, c, 0, 4);
        void* f = F10();
        api->draw_text_font(g, f, sub, r.left + (W(r) - api->text_width_font(f, sub)) / 2, r.bottom - 12, c);
    }
    else Label(api, g, F15(), text, r, c);
    return over;
}

// the game's edit box (as in the name dialog), with the text inside
inline void EditBox(const RemodApi* api, void* g, const RECT& r, const std::string& text, bool editing, const char* hint)
{
    void* img = Image(game::IMAGE_EDITBOX);
    ImageBox(api, g, img, 0, 0, ImgW(img), ImgH(img), r);
    void* f = F12();
    std::string shown = text.empty() && !editing ? hint : text + (editing && (GetTickCount() / 400) % 2 ? "_" : "");
    int y = r.top + (H(r) + Ascent(f)) / 2 - 2;
    FitText(api, g, f, shown, r.left + 10, y, W(r) - 20, text.empty() && !editing ? Grey : White);
}

// the folder the game keeps its saves in (userdata in the game's data folder: ProgramData\Steam\Insaniquarium for the
// Steam release); mods keep their own files there too
inline std::string UserData()
{
    std::string d = game::ReadString(game::G_AppDataFolder);
    if (d.empty())
    {
        char base[MAX_PATH];
        if (SHGetFolderPathA(nullptr, 0x23 /* CSIDL_COMMON_APPDATA */, nullptr, 0, base) != S_OK) return "userdata";
        d = std::string(base) + "\\PopCap Games\\Insaniquarium\\";
    }
    if (d.back() != '\\' && d.back() != '/') d += '\\';
    return d + "userdata";
}

// ---- screens on the game's dialog ----------------------------------------------------------------------------------------
inline void* OpenDialog(const RemodApi* api, int id, const char* header, const char* footer, int x, int y, int w, int h)
{
    void* a = api->app();
    if (!a) return nullptr;
    game::MsvcString hs = game::MakeString(header), ls = game::MakeString(""), fs = game::MakeString(footer);
    typedef void*(__thiscall* DoDialogFn)(void*, int, bool, const game::MsvcString*, const game::MsvcString*, const game::MsvcString*, int);
    void* d = reinterpret_cast<DoDialogFn>(Vt(a)[game::App_vDoDialog / 4])(a, id, true, &hs, &ls, &fs, 3);
    if (d) reinterpret_cast<void(__thiscall*)(void*, int, int, int, int)>(Vt(d)[game::Widget_vResize / 4])(d, x, y, w, h);
    api->redraw();
    return d;
}
inline void* GetDialog(const RemodApi* api, int id)
{
    void* a = api->app();
    return a ? reinterpret_cast<void*(__thiscall*)(void*, int)>(Vt(a)[game::App_vGetDialog / 4])(a, id) : nullptr;
}
inline void KillDialog(const RemodApi* api, int id)
{
    void* a = api->app();
    if (a) reinterpret_cast<bool(__thiscall*)(void*, int)>(Vt(a)[game::App_vKillDialog / 4])(a, id);
    api->redraw();
}
inline int DialogCount(const RemodApi* api) { void* a = api->app(); return a ? game::at<int>(a, game::App_mDialogCount) : 0; }
inline bool OnMainMenu(const RemodApi* api)   // the main menu is showing (no tank, no dialog)
{
    void* a = api->app();
    return a && !api->board() && game::at<void*>(a, game::App_mGameSelector) && DialogCount(api) == 0;
}
// the menu's second page (the settings mod) covers the main menu: other mods keep their hands off it
inline bool MenuPageOpen()
{
    static int (*f)() = nullptr;
    static bool looked;
    if (!looked) { looked = true; if (HMODULE m = GetModuleHandleA("settings.dll")) f = reinterpret_cast<int (*)()>(GetProcAddress(m, "MenuPageOpen")); }
    return f && f();
}

// a co-op game is running (coop.dll): mods that act on local input or open dialogs stand down
inline bool CoopPlaying()
{
    static int (*f)() = nullptr;
    static bool looked;
    if (!looked) { looked = true; if (HMODULE m = GetModuleHandleA("coop.dll")) f = reinterpret_cast<int (*)()>(GetProcAddress(m, "CoopPlaying")); }
    return f && f();
}

// calls an exported void function of another mod (opening its screen); false when it isn't installed
inline bool Call(const char* dll, const char* name)
{
    HMODULE m = GetModuleHandleA(dll);
    auto f = m ? reinterpret_cast<void (*)()>(GetProcAddress(m, name)) : nullptr;
    if (f) f();
    return f != nullptr;
}
inline bool Has(const char* dll, const char* name) { HMODULE m = GetModuleHandleA(dll); return m && GetProcAddress(m, name); }

}  // namespace ui
