// Native-resolution window (core/display.cpp).
#pragma once
#include <windows.h>

void DisplayInit();
void DisplayInitScreenSaver();                 // the screensaver copy: a borderless window covering the screen                             // reads [display], forces windowed mode for the next start
void DisplayOnDirectDraw(void* dd7, void* dd1); // a DirectDraw object was created: wrap CreateSurface
void DisplaySetWindow(HWND w);
void DisplayPlayerMovesWindow();                // the player started moving or resizing the window: leave its place alone                  // the game's window
int DisplayNeedsSwitch();                       // once: the game started in the other screen mode: 1 = switch to windowed, 0 = to fullscreen, -1 = nothing
bool DisplayEnabled();
bool DisplayCaptureFrame(unsigned* out640x480);                          // [display] window=native is on for this session
bool DisplayActive();                           // the native window is in use
LPARAM DisplayMapMouse(LPARAM lp);              // window -> game coordinates for mouse messages
void DisplayAtExit();                           // the game is closing: the chosen screen mode for the next start
