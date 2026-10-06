// Native-resolution window ([display] window=native in remastered-mod.ini; applies from the next start).
//
// The game draws a 640x480 frame and, in windowed mode, copies it each frame onto the DirectDraw primary surface (the
// screen) at its window's position. With this setting on, the game's window becomes a borderless window covering the
// whole monitor at its native resolution, and that copy is stretched into the largest 4:3 rectangle that fits (scale=fit)
// or the largest whole-number multiple (scale=integer), centred, with black bars around it. Mouse positions are mapped
// back to the game's 640x480 coordinates. Everything is done at the DirectDraw level, through the proxy: the
// DirectDraw object's CreateSurface is wrapped to spot the primary surface, and the primary surface's Blt to redirect
// the copy. The game must be in windowed mode: the setting writes the game's own ScreenMode=0 to the registry, which it
// reads at start, hence the restart.
#include <windows.h>
#include <string>
#include "display.h"

void CoreLog(const char* fmt, ...);
int CoreConfigInt(const char* section, const char* key, int def);
std::string CoreConfigString(const char* section, const char* key, const char* def);
void CoreToast(const char* text);
bool CoreGameWindowed();

namespace {

constexpr int GameW = 640, GameH = 480;
constexpr DWORD DDSCAPS_PRIMARYSURFACE = 0x200;
constexpr DWORD DDBLT_COLORFILL = 0x400, DDBLT_WAIT = 0x1000000;
constexpr int VtCreateSurface = 6, VtBlt = 5;   // IDirectDraw(7)::CreateSurface, IDirectDrawSurface(7)::Blt

bool gOn, gInteger, gWindowDone;
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

// the game's window: borderless, covering the monitor it's on
void MakeWindowNative()
{
    gWindowDone = true;
    HMONITOR mon = MonitorFromWindow(gWnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof mi };
    GetMonitorInfoA(mon, &mi);
    SetWindowLongA(gWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowPos(gWnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                 mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    Layout();
    CoreLog("native window: %ldx%ld, game frame at %ld,%ld scale %.2f%s", mi.rcMonitor.right - mi.rcMonitor.left,
            mi.rcMonitor.bottom - mi.rcMonitor.top, gTarget.left, gTarget.top, gScale, gInteger ? " (integer)" : "");
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
    if (!CoreGameWindowed()) { gWindowDone = false; return blt(self, dst, src, srcRect, flags, fx); }
    if (!gWindowDone) MakeWindowNative();
    else Layout();
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

static bool gWanted, gNeedSwitch;   // the setting is on (even if this session started fullscreen)

static void SetWindowedForNextStart(DWORD* previous)
{
    HKEY k;
    DWORD size = sizeof(DWORD);
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\PopCap\\Insaniquarium", 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    if (previous) RegQueryValueExA(k, "ScreenMode", nullptr, nullptr, (BYTE*)previous, &size);
    DWORD zero = 0;
    RegSetValueExA(k, "ScreenMode", 0, REG_DWORD, (const BYTE*)&zero, sizeof zero);
    RegCloseKey(k);
}

void DisplayInit()
{
    std::string mode = CoreConfigString("display", "window", "native");   // the default: native (window=normal turns it off)
    gOn = mode == "native";
    gInteger = CoreConfigString("display", "scale", "fit") == "integer";
    gWanted = gOn;
    if (!gOn) return;
    // the game must start windowed: its own setting, read at start (so it counts from the next start if it was off)
    DWORD screenMode = 0;
    SetWindowedForNextStart(&screenMode);
    if (screenMode != 0)
    {
        gNeedSwitch = true;   // this session starts fullscreen: the core switches the game to windowed once it runs
        CoreLog("native window: the game was set to fullscreen; switching it to windowed");
    }
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
bool DisplayNeedsSwitch() { bool r = gNeedSwitch; gNeedSwitch = false; return r; }

// the game saves its own ScreenMode when it closes (fullscreen if this session was): set windowed again after that
void DisplayAtExit() { if (gWanted) SetWindowedForNextStart(nullptr); }

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
