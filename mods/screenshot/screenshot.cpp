// screenshot: F12 saves what's in the game window as a PNG in the game folder's screenshots\ folder
// ([screenshot] key=123). Captured from the screen with GDI and saved with GDI+ (both part of Windows and Wine).
#include "remod.h"
#include <windows.h>
#include <string>

static const RemodApi* api;
static int key = 0x7b;
static std::string folder;

// GDI+ flat API, loaded at run time (no import library needed)
struct StartupInput { UINT32 version; void* callback; BOOL noThread, noCodecs; };
typedef int(WINAPI* StartupFn)(ULONG_PTR*, const StartupInput*, void*);
typedef int(WINAPI* FromHbitmapFn)(HBITMAP, HPALETTE, void**);
typedef int(WINAPI* SaveFn)(void*, const WCHAR*, const CLSID*, const void*);
typedef int(WINAPI* DisposeFn)(void*);
static FromHbitmapFn pFromHbitmap;
static SaveFn pSave;
static DisposeFn pDispose;
static const CLSID PngEncoder = { 0x557cf406, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };

static bool GdiPlus()
{
    if (pSave) return true;
    HMODULE m = LoadLibraryA("gdiplus.dll");
    if (!m) return false;
    auto start = (StartupFn)GetProcAddress(m, "GdiplusStartup");
    pFromHbitmap = (FromHbitmapFn)GetProcAddress(m, "GdipCreateBitmapFromHBITMAP");
    pSave = (SaveFn)GetProcAddress(m, "GdipSaveImageToFile");
    pDispose = (DisposeFn)GetProcAddress(m, "GdipDisposeImage");
    ULONG_PTR token;
    StartupInput in{ 1, nullptr, FALSE, FALSE };
    if (!start || !pFromHbitmap || !pSave || !pDispose || start(&token, &in, nullptr) != 0) { pSave = nullptr; return false; }
    return true;
}

static HWND GameWindow() { void* app = api->app(); return app ? *reinterpret_cast<HWND*>(static_cast<char*>(app) + 0x350) : nullptr; }

static void Take()
{
    HWND w = GameWindow();
    if (!w || !GdiPlus()) { api->toast("Screenshot failed"); return; }
    RECT r;
    GetClientRect(w, &r);
    POINT o{ 0, 0 };
    ClientToScreen(w, &o);
    int cw = r.right, ch = r.bottom;
    HBITMAP bmp = nullptr;
    // the game's own 640x480 frame (works everywhere, also under Proton and in fullscreen); else what's on the screen
    static unsigned frame[640 * 480];
    if (api->version >= 6 && api->capture_frame(frame))
    {
        BITMAPINFO bi{};
        bi.bmiHeader = { sizeof(BITMAPINFOHEADER), 640, -480, 1, 32, BI_RGB };
        void* bits = nullptr;
        bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (bmp && bits) memcpy(bits, frame, sizeof frame);
    }
    if (!bmp)
    {
        HDC screen = GetDC(nullptr), mem = CreateCompatibleDC(screen);
        bmp = CreateCompatibleBitmap(screen, cw, ch);
        HGDIOBJ old = SelectObject(mem, bmp);
        BitBlt(mem, 0, 0, cw, ch, screen, o.x, o.y, SRCCOPY);
        SelectObject(mem, old);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
    }

    CreateDirectoryA(folder.c_str(), nullptr);
    SYSTEMTIME t;
    GetLocalTime(&t);
    char name[64];
    wsprintfA(name, "insaniquarium-%04d%02d%02d-%02d%02d%02d.png", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    std::string path = folder + "\\" + name;
    WCHAR wpath[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, wpath, MAX_PATH);
    void* img = nullptr;
    bool ok = pFromHbitmap(bmp, nullptr, &img) == 0 && pSave(img, wpath, &PngEncoder, nullptr) == 0;
    if (img) pDispose(img);
    DeleteObject(bmp);
    api->log(ok ? "screenshot: %s" : "screenshot: couldn't save %s", path.c_str());
    api->toast(ok ? (std::string("Screenshot: screenshots\\") + name).c_str() : "Screenshot failed");
}

static int Key(int vk, int down)
{
    if (!down || vk != key) return 0;
    Take();
    return 1;
}

static void Load() { key = api->config_int("screenshot", "key", 0x7b); }

extern "C" __declspec(dllexport) const char* RemodDescribe() { return "F12 saves a screenshot in the game folder's screenshots folder."; }

extern "C" __declspec(dllexport) int RemodInit(const RemodApi* a)
{
    if (a->version < REMOD_API_VERSION) return 0;
    api = a;
    Load();
    if (api->version >= 3) api->on_config(Load);
    // <game folder>\screenshots: this DLL is in <game folder>\mods
    char p[MAX_PATH];
    HMODULE self;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&Take, &self);
    GetModuleFileNameA(self, p, MAX_PATH);
    std::string dir = p;
    dir = dir.substr(0, dir.find_last_of("\\/"));   // ...\mods
    folder = dir.substr(0, dir.find_last_of("\\/")) + "\\screenshots";
    api->on_key(Key);
    return 1;
}
