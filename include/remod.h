/* Insaniquarium - Remastered Mod: the mod API for the original Insaniquarium Deluxe 1.1 (Insaniquarium.exe).
 *
 * A mod is a 32-bit DLL in the game's mods/ folder that exports
 *     int RemodInit(const RemodApi* api);
 * returning 1 when it loaded. The loader (ddraw.dll next to the game) calls it on the main thread at the game's first
 * DirectDrawCreate (during start-up, before the game loads its resources), so mods can install hooks before anything runs.
 *
 * Plain C, so mods can be written in C or C++ and built with any 32-bit Windows compiler. Pointers to game objects
 * are opaque here; mods that need fields use the offsets in game.h.
 *
 * Compatibility rules:
 * - api->version is the core's REMOD_API_VERSION. New functions are only ever appended to RemodApi, never reordered
 *   or removed, so a mod built for an older version keeps working with a newer core.
 * - A mod built for a newer version than the running core must check api->version before using a newer function
 *   (each one says the version that added it below): skip that feature, or return 0 from RemodInit to decline.
 * - Everything runs on the game's main thread (callbacks too). Strings passed to the API are copied; the names
 *   mod_list hands out stay valid for the whole session.
 * - Persist only in your own files (the ini through config_set, or files of your own next to the saves); never write
 *   the game's save files. */
#ifndef REMOD_H
#define REMOD_H

#ifdef __cplusplus
extern "C" {
#endif

#define REMOD_API_VERSION 7   /* 2: on_mouse, mouse_pos, play_sound, config_string; 3: images, fonts, config_set, on_config;
                                  4: mod_list, restart; 5: input filter and injection (co-op);
                                  6: capture_frame; 7: draw_image_scaled */

/* colours are 0xAARRGGBB */
typedef struct RemodApi {
    int version;                          /* REMOD_API_VERSION */

    /* log a line to mods/remastered-mod.log (printf-style) */
    void (*log)(const char* fmt, ...);

    /* hook a function of the game: target = its address, detour = yours; *original receives a pointer that calls the
       game's own code. Game methods are __thiscall: write detours as __fastcall(this, edx_unused, args...) and call
       the original through a __thiscall pointer. Several mods may hook the same function (they're chained, the mod
       loaded last runs first). Returns 1 on success. */
    int (*hook)(void* target, void* detour, void** original);

    /* callbacks */
    void (*on_tick)(void (*cb)(void* board));                 /* after every game update in a tank (Board::Update, 28 ms) */
    void (*on_draw)(void (*cb)(void* board, void* graphics)); /* after the tank is drawn (Board::Draw), under fish and coins */
    void (*on_key)(int (*cb)(int vk, int down));              /* key presses while the game has focus; return 1 = handled */

    /* game state */
    void* (*app)(void);                   /* the WinFishApp (gApp) */
    void* (*board)(void);                 /* the current Board, or 0 outside a tank */

    /* drawing, for on_draw (uses the game's own Graphics and fonts) */
    void (*fill_rect)(void* graphics, int x, int y, int w, int h, unsigned argb);
    void (*draw_text)(void* graphics, const char* text, int x, int y, unsigned argb);
    int (*text_width)(const char* text);

    /* settings from mods/remastered-mod.ini: [section] key=value */
    int (*config_int)(const char* section, const char* key, int def);   /* read each time: changes apply at once */

    /* show a line of text over the game for a few seconds (every screen) */
    void (*toast)(const char* text);

    /* drawn on top of everything, on every screen, after the game has drawn a frame */
    void (*on_overlay)(void (*cb)(void* graphics));
    /* repaint the whole screen next frame: call after what your overlay shows changes, so nothing stale stays */
    void (*redraw)(void);

    /* ---- version 2 (check api->version >= 2 before using) ---- */
    /* mouse buttons in the game window, in game coordinates (640x480): button 0 left, 1 right, 2 middle; down 1/0.
       Return 1 to keep the click from the game (its release is kept from the game too). */
    void (*on_mouse)(int (*cb)(int x, int y, int button, int down));
    /* where the pointer is, in game coordinates */
    void (*mouse_pos)(int* x, int* y);
    /* play one of the game's sounds by resource id (SOUND_BUY = 273...; only while a tank is open) */
    void (*play_sound)(int resource_id);
    /* a text setting from mods/remastered-mod.ini; copies at most size-1 characters into out */
    void (*config_string)(const char* section, const char* key, const char* def, char* out, int size);

    /* ---- version 3 ---- */
    /* draw one of the game's images (an Image*, e.g. *(void**)game::IMAGE_CHECKED), whole or a part of it */
    void (*draw_image)(void* graphics, void* image, int x, int y);
    void (*draw_image_part)(void* graphics, void* image, int x, int y, int sx, int sy, int sw, int sh);
    /* text in one of the game's fonts (a Font*, e.g. *(void**)game::FONT_JUNGLEFEVER12OUTLINE; 0 = the default) */
    void (*draw_text_font)(void* graphics, void* font, const char* text, int x, int y, unsigned argb);
    int (*text_width_font)(void* font, const char* text);
    /* change a setting in mods/remastered-mod.ini (then call config_changed so mods pick it up) */
    void (*config_set)(const char* section, const char* key, const char* value);
    /* tells every mod that settings changed (on_config callbacks run) */
    void (*config_changed)(void);
    void (*on_config)(void (*cb)(void));

    /* ---- version 4 ---- */
    /* the DLLs in the mods folder, in load order: i = 0, 1, ... until it returns -1. Returns the state (REMOD_MOD_*)
       and sets the name (file name without .dll) and description (from the mod's optional RemodDescribe export) */
    int (*mod_list)(int i, const char** name, const char** description);
    /* saves the profile and restarts the game (the same exe and command line) */
    void (*restart)(void);

    /* ---- version 5 (co-op lockstep) ---- */
    /* every mouse/keyboard message of the game window (mouse positions in game coordinates), before the game and before
       on_mouse/on_key; return 1 to keep it from the game. One filter (the last set wins); 0 clears it. */
    void (*set_input_filter)(int (*cb)(unsigned msg, unsigned wparam, long lparam));
    /* gives the game an input message as if it came from the window (bypassing the filter), and lets the game handle
       it right away (its deferred-message queue is processed) */
    void (*inject_input)(unsigned msg, unsigned wparam, long lparam);

    /* ---- version 6 ---- */
    /* the game's last frame, 640x480, as 0x00RRGGBB pixels (top row first) into out (640*480 entries); 0 if none yet */
    int (*capture_frame)(unsigned* out);

    /* ---- version 7 ---- */
    /* a part of one of the game's images (sx, sy, sw, sh) drawn scaled by `scale` with its top left at (x, y), through
       the game's own stretched drawing: e.g. an animation cel shrunk to fit a small box */
    void (*draw_image_scaled)(void* graphics, void* image, int x, int y, int sx, int sy, int sw, int sh, float scale);
} RemodApi;

#define REMOD_MOD_ON 0         /* loaded and running */
#define REMOD_MOD_OFF 1        /* switched off in the settings ([mods] name=0) */
#define REMOD_MOD_DECLINED 2   /* its RemodInit returned 0 (an older API, a setting, a hook it couldn't make) */
#define REMOD_MOD_BROKEN 3     /* not a mod (no RemodInit) or failed to load */
#define REMOD_MOD_CRASHED 4    /* crashed and was switched off for this session */

/* optional: a mod may export   const char* RemodDescribe(void)   returning one line about what it does */
typedef const char* (*RemodDescribeFn)(void);

typedef int (*RemodInitFn)(const RemodApi* api);

#ifdef __cplusplus
}
#endif

#endif
