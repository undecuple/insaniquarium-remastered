// Native-resolution window ([display] window=native in remastered-mod.ini; applies from the next start).
//
// The game draws a 640x480 frame and, in windowed mode, copies it each frame onto the DirectDraw primary surface (the
// screen) at its window's position. With this setting on, the game's window becomes a normal, resizable window sized
// to the largest whole multiple of 640x480 that fits the screen (window=native; window=borderless: a borderless window
// covering the whole monitor), and that copy is stretched into the largest 4:3 rectangle that fits the window
// (scale=fit) or the largest whole-number multiple (scale=integer), centred, with black bars around it. Mouse positions
// are mapped back to the game's 640x480 coordinates: in its mouse messages, and in its own GetCursorPos (the game
// decides from it whether the pointer is over its 640x480 client area, and shows its own cursor only then). Everything is done at the DirectDraw level, through the proxy: the
// DirectDraw object's CreateSurface is wrapped to spot the primary surface, and the primary surface's Blt to redirect
// the copy. The game must be in windowed mode: the setting writes the game's own ScreenMode=0 to the registry, which it
// reads at start, hence the restart.
#include <windows.h>
#include <string>
#include <string.h>
#include "display.h"
#include "game.h"
#include <vector>

void CoreLog(const char* fmt, ...);
int CoreConfigInt(const char* section, const char* key, int def);
std::string CoreConfigString(const char* section, const char* key, const char* def);
void CoreToast(const char* text);
bool CoreGameWindowed();
void* CoreApp();

namespace {

constexpr int GameW = 640, GameH = 480;
constexpr DWORD DDSCAPS_PRIMARYSURFACE = 0x200;
constexpr DWORD DDBLT_COLORFILL = 0x400, DDBLT_WAIT = 0x1000000;
constexpr int VtCreateSurface = 6, VtBlt = 5;   // IDirectDraw(7)::CreateSurface, IDirectDrawSurface(7)::Blt

bool gOn, gInteger, gBorderless, gWindowDone;
RECT gWantRect;        // where the window was put (some desktops ignore a move the first time): re-applied for a few seconds
DWORD gPlacedAt;       // GetTickCount() when, 0 = done (or the player moved the window)
int gPlaceTries;
HWND gWnd;
void* gPrimary[2];
void* gFrame;          // the surface the game copies its frame from (its 640x480 draw surface)
int gFrameVersion;     // the primary surface, as created (IDirectDrawSurface7) and as the game uses it (IDirectDrawSurface)
RECT gTarget;          // where the game's frame goes, in client coordinates
float gScale = 1;

using CreateSurfaceFn = HRESULT(WINAPI*)(void* dd, void* desc, void** surface, void* outer);
using BltFn = HRESULT(WINAPI*)(void* self, RECT* dst, void* src, RECT* srcRect, DWORD flags, void* fx);
CreateSurfaceFn gOrigCreate7, gOrigCreate1;
BltFn gOrigBlt[2];     // each interface version has its own Blt

void** Vtable(void* com) { return *reinterpret_cast<void***>(com); }

// replaces one slot of a COM object's vtable (shared by every object of that type); returns the old function
void* PatchSlot(void* com, int slot, void* fn)
{
    void** vt = Vtable(com);
    if (vt[slot] == fn) return nullptr;
    DWORD old;
    VirtualProtect(&vt[slot], sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
    void* prev = vt[slot];
    vt[slot] = fn;
    VirtualProtect(&vt[slot], sizeof(void*), old, &old);
    return prev;
}

void Layout()
{
    RECT c;
    GetClientRect(gWnd, &c);
    int w = c.right - c.left, h = c.bottom - c.top;
    float s = (float)w / GameW < (float)h / GameH ? (float)w / GameW : (float)h / GameH;
    if (gInteger && s >= 1) s = (float)(int)s;
    gScale = s;
    int tw = (int)(GameW * s + 0.5f), th = (int)(GameH * s + 0.5f);
    gTarget = { (w - tw) / 2, (h - th) / 2, (w - tw) / 2 + tw, (h - th) / 2 + th };
}

// the game's window: a resizable window of the largest whole multiple of 640x480 that fits the screen (taskbar and title
// bar included), centred; or (borderless) a borderless window covering the monitor it's on
void MakeWindowNative()
{
    gWindowDone = true;
    HMONITOR mon = MonitorFromWindow(gWnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof mi };
    GetMonitorInfoA(mon, &mi);
    if (gBorderless)
    {
        SetWindowLongA(gWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(gWnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    else
    {
        DWORD style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
        const RECT& wa = mi.rcWork;
        RECT r{};
        int k = 1;
        for (int n = 2; n <= 16; n++)
        {
            RECT t{ 0, 0, GameW * n, GameH * n };
            AdjustWindowRect(&t, style, FALSE);
            if (t.right - t.left > wa.right - wa.left || t.bottom - t.top > wa.bottom - wa.top) break;
            k = n;
        }
        r = { 0, 0, GameW * k, GameH * k };
        AdjustWindowRect(&r, style, FALSE);
        int w = r.right - r.left, h = r.bottom - r.top;
        // hidden while it changes: shown again at the new place, which desktops that ignore a mapped window's move honour
        gWantRect = { wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, 0, 0 };
        gWantRect.right = gWantRect.left + w; gWantRect.bottom = gWantRect.top + h;
        ShowWindow(gWnd, SW_HIDE);
        SetWindowLongA(gWnd, GWL_STYLE, style & ~WS_VISIBLE);
        SetWindowPos(gWnd, HWND_TOP, gWantRect.left, gWantRect.top, w, h, SWP_FRAMECHANGED | SWP_NOACTIVATE);
        ShowWindow(gWnd, SW_SHOW);
        SetForegroundWindow(gWnd);
        gPlacedAt = GetTickCount(); gPlaceTries = 0;
        RECT got;
        GetWindowRect(gWnd, &got);
        CoreLog("native window: monitor %ld,%ld %ldx%ld, work area %ld,%ld %ldx%ld; window wanted at %ld,%ld, is at %ld,%ld %ldx%ld",
                mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                wa.left, wa.top, wa.right - wa.left, wa.bottom - wa.top, gWantRect.left, gWantRect.top, got.left, got.top,
                got.right - got.left, got.bottom - got.top);
    }
    Layout();
    RECT c;
    GetClientRect(gWnd, &c);
    CoreLog("native window: %s %ldx%ld, game frame at %ld,%ld scale %.2f%s", gBorderless ? "borderless" : "window",
            c.right, c.bottom, gTarget.left, gTarget.top, gScale, gInteger ? " (integer)" : "");
}

// the game's own GetCursorPos (its import, so Windows' window dragging and resizing still see the real pointer): the
// pointer's position inside the scaled frame, as if the window were 640x480; on the black bars, a point outside it
using GetCursorPosFn = BOOL(WINAPI*)(POINT*);
GetCursorPosFn gRealGetCursorPos;
BOOL WINAPI GameGetCursorPos(POINT* p)
{
    BOOL ok = gRealGetCursorPos(p);
    if (!ok || !p || !gOn || !gWindowDone || !gWnd || gScale <= 0) return ok;
    POINT c = *p;
    ScreenToClient(gWnd, &c);
    if (c.x < gTarget.left || c.y < gTarget.top || c.x >= gTarget.right || c.y >= gTarget.bottom) return ok;   // on a bar or outside
    POINT g{ (LONG)((c.x - gTarget.left) / gScale), (LONG)((c.y - gTarget.top) / gScale) };
    if (g.x > GameW - 1) g.x = GameW - 1;
    if (g.y > GameH - 1) g.y = GameH - 1;
    ClientToScreen(gWnd, &g);
    *p = g;
    return ok;
}

// The game's own cursor (the fish pointer, the hand...) is drawn by the game straight onto the screen at 640x480
// positions, which the scaling can't follow. In a native window the game's cursor pictures are taken away from it each
// frame (so it falls back to Windows cursors) and its SetCursor calls get Windows cursors made from those same pictures
// (images\cursor_*.gif with their _cursor_*.gif alpha masks), scaled like the frame: the same cursor, at the right size.
constexpr int CursorKinds = 4;   // pointer, hand, dragging, text
const char* const CursorFile[CursorKinds] = { "cursor_pointer", "cursor_hand", "cursor_dragging", "cursor_text" };
void* gGameCursorImage[CursorKinds];   // the game's pictures, handed back when the window isn't native any more
HCURSOR gCursor[CursorKinds];
float gCursorScale;
bool gCursorFailed;

struct Picture { int w = 0, h = 0; std::vector<unsigned> argb; };
// a picture through GDI+ (loaded when first needed): ARGB pixels
bool LoadPicture(const char* name, Picture& out)
{
    using StartupFn = int(WINAPI*)(ULONG_PTR*, const void*, void*);
    using FromFileFn = int(WINAPI*)(const wchar_t*, void**);
    using SizeFn = int(WINAPI*)(void*, UINT*);
    using PixelFn = int(WINAPI*)(void*, int, int, unsigned*);
    using DisposeFn = int(WINAPI*)(void*);
    static HMODULE m;
    static FromFileFn fromFile; static SizeFn width, height; static PixelFn pixel; static DisposeFn dispose;
    if (!m)
    {
        if (!(m = LoadLibraryA("gdiplus.dll"))) return false;
        struct { UINT32 version = 1; void* callback = nullptr; BOOL noThread = FALSE, noCodecs = FALSE; } in;
        ULONG_PTR token;
        auto start = (StartupFn)GetProcAddress(m, "GdiplusStartup");
        fromFile = (FromFileFn)GetProcAddress(m, "GdipCreateBitmapFromFile");
        width = (SizeFn)GetProcAddress(m, "GdipGetImageWidth");
        height = (SizeFn)GetProcAddress(m, "GdipGetImageHeight");
        pixel = (PixelFn)GetProcAddress(m, "GdipBitmapGetPixel");
        dispose = (DisposeFn)GetProcAddress(m, "GdipDisposeImage");
        if (!start || !fromFile || !width || !height || !pixel || !dispose || start(&token, &in, nullptr) != 0) { fromFile = nullptr; return false; }
    }
    if (!fromFile) return false;
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"images\\%hs.gif", name);
    void* bmp = nullptr;
    if (fromFile(path, &bmp) != 0 || !bmp) return false;
    UINT w = 0, h = 0;
    width(bmp, &w); height(bmp, &h);
    out.w = (int)w; out.h = (int)h;
    out.argb.assign(w * h, 0);
    for (UINT y = 0; y < h; y++)
        for (UINT x = 0; x < w; x++) pixel(bmp, (int)x, (int)y, &out.argb[y * w + x]);
    dispose(bmp);
    return true;
}

// a Windows cursor from the game's picture and its alpha mask, scaled (nearest pixel); the hot spot is the middle, where
// the game centres its cursor pictures on the pointer
HCURSOR MakeCursor(int kind, float scale)
{
    Picture pic, mask;
    if (!LoadPicture(CursorFile[kind], pic) || pic.w <= 0) return nullptr;
    bool hasMask = LoadPicture((std::string("_") + CursorFile[kind]).c_str(), mask) && mask.w == pic.w && mask.h == pic.h;
    int w = (int)(pic.w * scale + 0.5f), h = (int)(pic.h * scale + 0.5f);
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof bi; bi.bV5Width = w; bi.bV5Height = -h; bi.bV5Planes = 1; bi.bV5BitCount = 32; bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00ff0000; bi.bV5GreenMask = 0x0000ff00; bi.bV5BlueMask = 0x000000ff; bi.bV5AlphaMask = 0xff000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color || !bits) return nullptr;
    auto out = static_cast<unsigned*>(bits);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            int sx = (int)(x / scale), sy = (int)(y / scale);
            if (sx >= pic.w) sx = pic.w - 1;
            if (sy >= pic.h) sy = pic.h - 1;
            unsigned p = pic.argb[sy * pic.w + sx];
            unsigned a = hasMask ? (mask.argb[sy * pic.w + sx] >> 16) & 0xff : p >> 24;
            out[y * w + x] = (a << 24) | (p & 0xffffff);
        }
    HBITMAP monochrome = CreateBitmap(w, h, 1, 1, nullptr);
    ICONINFO ii{ FALSE, (DWORD)(w / 2), (DWORD)(h / 2), monochrome, color };
    HCURSOR c = (HCURSOR)CreateIconIndirect(&ii);
    DeleteObject(monochrome);
    DeleteObject(color);
    return c;
}

// each frame of a native window: the game's cursor pictures out of its hands, the cursors made for this scale
void TakeGameCursors(bool native)
{
    void* app = CoreApp();
    if (!app) return;
    auto images = &game::at<void*>(app, game::App_mCursorImages);
    for (int i = 0; i < CursorKinds; i++)
    {
        if (native && images[i]) { gGameCursorImage[i] = images[i]; images[i] = nullptr; }
        else if (!native && !images[i] && gGameCursorImage[i]) images[i] = gGameCursorImage[i];
    }
    if (!native || gCursorFailed || gScale <= 0 || gCursorScale == gScale) return;
    gCursorScale = gScale;
    for (int i = 0; i < CursorKinds; i++)
    {
        if (gCursor[i]) DestroyCursor(gCursor[i]);
        gCursor[i] = MakeCursor(i, gScale);
    }
    if (!gCursor[0]) { gCursorFailed = true; CoreLog("native window: couldn't make the game's cursors (Windows' own are used)"); }
}

using SetCursorFn = HCURSOR(WINAPI*)(HCURSOR);
SetCursorFn gRealSetCursor;
HCURSOR WINAPI GameSetCursor(HCURSOR c)
{
    void* app = CoreApp();
    if (gOn && gWindowDone && app && game::at<bool>(app, game::App_mMouseIn))
    {
        int kind = game::at<int>(app, game::App_mCursorNum);
        if (kind >= 0 && kind < CursorKinds && gCursor[kind]) c = gCursor[kind];
    }
    return gRealSetCursor(c);
}

// replaces one import of the game's exe (by DLL and function name); returns the old address
void* PatchImport(const char* dll, const char* fn, void* to)
{
    auto base = reinterpret_cast<unsigned char*>(GetModuleHandleA(nullptr));
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return nullptr;
    for (auto imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; imp++)
    {
        if (lstrcmpiA(reinterpret_cast<char*>(base + imp->Name), dll) != 0 || !imp->OriginalFirstThunk) continue;
        auto names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk);
        auto addrs = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, addrs++)
        {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (strcmp(reinterpret_cast<char*>(byName->Name), fn) != 0) continue;
            DWORD old;
            VirtualProtect(&addrs->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
            void* prev = reinterpret_cast<void*>(addrs->u1.Function);
            addrs->u1.Function = reinterpret_cast<uintptr_t>(to);
            VirtualProtect(&addrs->u1.Function, sizeof(void*), old, &old);
            return prev;
        }
    }
    return nullptr;
}

struct DDBltFX { DWORD dwSize; DWORD pad[24]; };   // DDBLTFX: 100 bytes; dwFillColor is at offset 0x50
void FillBlack(BltFn blt, void* primary, RECT r)
{
    if (r.right <= r.left || r.bottom <= r.top) return;
    unsigned char fx[100] = {};
    *reinterpret_cast<DWORD*>(fx) = sizeof fx;
    *reinterpret_cast<DWORD*>(fx + 0x50) = 0;
    blt(primary, &r, nullptr, nullptr, DDBLT_COLORFILL | DDBLT_WAIT, fx);
}

HRESULT BltCommon(int v, void* self, RECT* dst, void* src, RECT* srcRect, DWORD flags, void* fx)
{
    BltFn blt = gOrigBlt[v];
    if (self == gPrimary[v] && src && !(flags & DDBLT_COLORFILL)) { gFrame = src; gFrameVersion = v; }   // the game's frame, for screenshots
    if (self != gPrimary[v] || !gOn || !gWnd || !dst || !src) return blt(self, dst, src, srcRect, flags, fx);
    // switched to fullscreen in the game's options: leave it alone, and redo the window when it comes back windowed
    if (!CoreGameWindowed()) { gWindowDone = false; TakeGameCursors(false); return blt(self, dst, src, srcRect, flags, fx); }
    if (gWindowDone && IsIconic(gWnd)) return blt(self, dst, src, srcRect, flags, fx);   // minimised: nothing to show, keep the layout
    if (!gWindowDone) MakeWindowNative();
    else Layout();
    // the first seconds: back where it was put, if the desktop or the game moved it (not once the player moves it)
    if (gPlacedAt && GetTickCount() - gPlacedAt < 3000)
    {
        RECT got;
        GetWindowRect(gWnd, &got);
        if ((got.left != gWantRect.left || got.top != gWantRect.top) && gPlaceTries < 5)
        {
            gPlaceTries++;
            CoreLog("native window: moved to %ld,%ld: putting it back at %ld,%ld", got.left, got.top, gWantRect.left, gWantRect.top);
            SetWindowPos(gWnd, nullptr, gWantRect.left, gWantRect.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    else gPlacedAt = 0;
    if (gRealSetCursor) TakeGameCursors(true);
    POINT o{ 0, 0 };
    ClientToScreen(gWnd, &o);
    // the game aimed at its 640x480 client area: turn its destination back into frame coordinates, then scale
    RECT frame = srcRect ? *srcRect : RECT{ dst->left - o.x, dst->top - o.y, dst->right - o.x, dst->bottom - o.y };
    RECT d{ o.x + gTarget.left + (LONG)(frame.left * gScale), o.y + gTarget.top + (LONG)(frame.top * gScale),
            o.x + gTarget.left + (LONG)(frame.right * gScale + 0.5f), o.y + gTarget.top + (LONG)(frame.bottom * gScale + 0.5f) };
    HRESULT r = blt(self, &d, src, srcRect ? srcRect : &frame, flags, fx);
    // the bars around the frame (the game never draws there)
    RECT c;
    GetClientRect(gWnd, &c);
    OffsetRect(&c, o.x, o.y);
    RECT t = gTarget;
    OffsetRect(&t, o.x, o.y);
    FillBlack(blt, self, { c.left, c.top, c.right, t.top });
    FillBlack(blt, self, { c.left, t.bottom, c.right, c.bottom });
    FillBlack(blt, self, { c.left, t.top, t.left, t.bottom });
    FillBlack(blt, self, { t.right, t.top, c.right, t.bottom });
    return r;
}
HRESULT WINAPI BltHook7(void* self, RECT* dst, void* src, RECT* sr, DWORD f, void* fx) { return BltCommon(0, self, dst, src, sr, f, fx); }
HRESULT WINAPI BltHook1(void* self, RECT* dst, void* src, RECT* sr, DWORD f, void* fx) { return BltCommon(1, self, dst, src, sr, f, fx); }

constexpr GUID IID_IDirectDrawSurface_ = { 0x6C14DB81, 0xA733, 0x11CE, { 0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60 } };
using QIFn = HRESULT(WINAPI*)(void*, const GUID*, void**);
using ReleaseFn = ULONG(WINAPI*)(void*);

void HookPrimary(int v, void* surface, void* hook)
{
    gPrimary[v] = surface;
    void* prev = PatchSlot(surface, VtBlt, hook);
    if (prev && !gOrigBlt[v]) gOrigBlt[v] = (BltFn)prev;
}

HRESULT CreateCommon(CreateSurfaceFn orig, void* dd, void* desc, void** surface, void* outer)
{
    HRESULT r = orig(dd, desc, surface, outer);
    if (r == 0 && desc && surface && *surface)
    {
        DWORD caps = *reinterpret_cast<DWORD*>(static_cast<char*>(desc) + 0x68);   // DDSURFACEDESC(2)::ddsCaps.dwCaps
        if (caps & DDSCAPS_PRIMARYSURFACE)
        {
            // SexyAppFramework creates it through IDirectDraw7, then uses it as an IDirectDrawSurface (DDInterface::CreateSurface)
            void* s1 = nullptr;
            bool via7 = orig == gOrigCreate7;
            HookPrimary(via7 ? 0 : 1, *surface, via7 ? (void*)&BltHook7 : (void*)&BltHook1);
            if (via7 && reinterpret_cast<QIFn>(Vtable(*surface)[0])(*surface, &IID_IDirectDrawSurface_, &s1) == 0 && s1)
            {
                HookPrimary(1, s1, (void*)&BltHook1);
                reinterpret_cast<ReleaseFn>(Vtable(s1)[2])(s1);
            }
            CoreLog("native window: primary surface %p / %p", gPrimary[0], gPrimary[1]);
        }
    }
    return r;
}
HRESULT WINAPI CreateSurface7Hook(void* dd, void* desc, void** s, void* outer) { return CreateCommon(gOrigCreate7, dd, desc, s, outer); }
HRESULT WINAPI CreateSurface1Hook(void* dd, void* desc, void** s, void* outer) { return CreateCommon(gOrigCreate1, dd, desc, s, outer); }

constexpr GUID IID_IDirectDraw_ = { 0x6C14DB80, 0xA733, 0x11CE, { 0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60 } };

}  // namespace

// [display] window: native (the default) = a large window; normal = the game's own 640x480 window; borderless; fullscreen =
// the game's own fullscreen mode. The game reads its screen mode (registry ScreenMode: 0 windowed, 1 fullscreen) at start and
// saves it when it closes, so the mode is written for every start, and switched once at the first frame if this start
// came up the other way (the Steam release starts fullscreen unless told otherwise)
static int gScreenMode = 0;       // what this setting wants: 0 windowed, 1 fullscreen
static int gNeedSwitch = -1;      // once: switch to windowed (1) or fullscreen (0); -1 nothing to do
static std::string gMode;         // the window mode in use (from the settings, or the game's own Fullscreen switch)

static void ApplyMode(const std::string& mode)
{
    gMode = mode;
    gBorderless = mode == "borderless";
    gOn = mode == "native" || gBorderless;
    gScreenMode = mode == "fullscreen" ? 1 : 0;
}

static void SetScreenModeForNextStart(DWORD* previous)
{
    HKEY k;
    DWORD size = sizeof(DWORD);
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\PopCap\\Insaniquarium", 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    if (previous) RegQueryValueExA(k, "ScreenMode", nullptr, nullptr, (BYTE*)previous, &size);
    DWORD v = (DWORD)gScreenMode;
    RegSetValueExA(k, "ScreenMode", 0, REG_DWORD, (const BYTE*)&v, sizeof v);
    RegCloseKey(k);
}

void DisplayInit()
{
    ApplyMode(CoreConfigString("display", "window", "native"));
    gInteger = CoreConfigString("display", "scale", "fit") == "integer";
    // always (they pass everything through while the native window is off): the mode can change while the game runs
    if (void* prev = PatchImport("user32.dll", "GetCursorPos", (void*)&GameGetCursorPos)) gRealGetCursorPos = (GetCursorPosFn)prev;
    else CoreLog("native window: the game's GetCursorPos not found (its own cursor may show only in the top-left corner)");
    if (void* prev = PatchImport("user32.dll", "SetCursor", (void*)&GameSetCursor)) gRealSetCursor = (SetCursorFn)prev;
    DWORD was = (DWORD)gScreenMode;
    SetScreenModeForNextStart(&was);
    if ((was != 0) != (gScreenMode != 0))
    {
        gNeedSwitch = gScreenMode ? 0 : 1;
        CoreLog("display: the game starts %s this time; switching it to %s", was ? "fullscreen" : "windowed", gScreenMode ? "fullscreen" : "windowed");
    }
}

// the game's own Fullscreen switch (core.cpp) changed the mode: the game makes a new window, sized at its first frame
const std::string& DisplayMode() { return gMode; }
void DisplaySetMode(const std::string& mode)
{
    ApplyMode(mode);
    gWindowDone = false;
    if (!gOn) TakeGameCursors(false);   // the game draws its own cursor again
    SetScreenModeForNextStart(nullptr);
}

void DisplayInitScreenSaver()
{
    gOn = gBorderless = true;
    gInteger = CoreConfigString("display", "scale", "fit") == "integer";
    if (void* prev = PatchImport("user32.dll", "GetCursorPos", (void*)&GameGetCursorPos)) gRealGetCursorPos = (GetCursorPosFn)prev;
    if (void* prev = PatchImport("user32.dll", "SetCursor", (void*)&GameSetCursor)) gRealSetCursor = (SetCursorFn)prev;
}

void DisplayOnDirectDraw(void* dd7, void* dd1)
{
    // always: the frame is also needed for screenshots (the Blt wrapper passes everything through when not native)
    if (dd7) { void* p = PatchSlot(dd7, VtCreateSurface, (void*)&CreateSurface7Hook); if (p) gOrigCreate7 = (CreateSurfaceFn)p; }
    void* v1 = dd1;
    if (!v1 && dd7 && reinterpret_cast<QIFn>(Vtable(dd7)[0])(dd7, &IID_IDirectDraw_, &v1) != 0) v1 = nullptr;
    if (v1)
    {
        void* p = PatchSlot(v1, VtCreateSurface, (void*)&CreateSurface1Hook);
        if (p) gOrigCreate1 = (CreateSurfaceFn)p;
        if (!dd1) reinterpret_cast<ReleaseFn>(Vtable(v1)[2])(v1);
    }
}

void DisplaySetWindow(HWND w) { gWnd = w; }
void DisplayPlayerMovesWindow() { gPlacedAt = 0; }

bool DisplayEnabled() { return gOn; }

// the game's last frame (640x480) into a 32-bit top-down buffer, through the surface's own GetDC (vtable 17/26 in every
// IDirectDrawSurface version); works where a screen capture sees nothing (Proton, fullscreen)
bool DisplayCaptureFrame(unsigned* out)
{
    void* s = gFrame;
    if (!s) return false;
    void** vt = *reinterpret_cast<void***>(s);
    HDC sdc = nullptr;
    if (reinterpret_cast<HRESULT(WINAPI*)(void*, HDC*)>(vt[17])(s, &sdc) != 0 || !sdc) return false;
    BITMAPINFO bi{};
    bi.bmiHeader = { sizeof(BITMAPINFOHEADER), GameW, -GameH, 1, 32, BI_RGB };
    void* bits = nullptr;
    HDC mem = CreateCompatibleDC(sdc);
    HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, dib);
    BOOL ok = BitBlt(mem, 0, 0, GameW, GameH, sdc, 0, 0, SRCCOPY);
    reinterpret_cast<HRESULT(WINAPI*)(void*, HDC)>(vt[26])(s, sdc);
    if (ok && bits) memcpy(out, bits, GameW * GameH * 4);
    SelectObject(mem, old);
    DeleteObject(dib);
    DeleteDC(mem);
    return ok && bits;
}
int DisplayNeedsSwitch() { int r = gNeedSwitch; gNeedSwitch = -1; return r; }

// the game saves its own ScreenMode when it closes (fullscreen if this session was): set windowed again after that
void DisplayAtExit() { SetScreenModeForNextStart(nullptr); }

bool DisplayActive() { return gOn && gWindowDone; }

// a mouse message's position in the window -> in the game's 640x480 frame
LPARAM DisplayMapMouse(LPARAM lp)
{
    if (!DisplayActive() || gScale <= 0) return lp;
    int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
    int gx = (int)((x - gTarget.left) / gScale), gy = (int)((y - gTarget.top) / gScale);
    gx = gx < 0 ? 0 : gx > GameW - 1 ? GameW - 1 : gx;
    gy = gy < 0 ? 0 : gy > GameH - 1 ? GameH - 1 : gy;
    return MAKELPARAM(gx, gy);
}
