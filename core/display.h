// Native-resolution window (core/display.cpp).
#pragma once
#include <windows.h>

void DisplayInit();
void DisplayInitScreenSaver();                 // the screensaver copy: a borderless window covering the screen                             // reads [display], forces windowed mode for the next start
void DisplayOnDirectDraw(void* dd7, void* dd1); // a DirectDraw object was created: wrap CreateSurface
void DisplaySetWindow(HWND w);                  // the game's window
bool DisplayNeedsSwitch();                      // once: the game started fullscreen: switch it to windowed now
bool DisplayEnabled();
bool DisplayCaptureFrame(unsigned* out640x480);                          // [display] window=native is on for this session
bool DisplayActive();                           // the native window is in use
LPARAM DisplayMapMouse(LPARAM lp);              // window -> game coordinates for mouse messages
void DisplayAtExit();                           // the game is closing: keep it windowed for the next start
