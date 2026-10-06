// The original Insaniquarium.exe (Deluxe 1.1, MSVC 7.1) as seen from a mod: addresses and field offsets.
// Found by reverse-engineering the exe (Ghidra; class layouts from its RTTI and the SexyApp framework it is built on).
// Valid only for the game build whose code hash is below; the loader refuses any other.
#pragma once
#include <stdint.h>
#include <string.h>

// The game is recognised by its code in memory, not the exe file: the Steam release's Insaniquarium.exe is a launcher
// that writes the same game build out as ProgramData\PopCap Games\Insaniquarium\popcapgame1.exe (different header and
// resources, identical code), so both pass. FNV-1a 64 of .text (0x401000, 0x1948fc bytes), image size 0x24c000.
#define GAME_IMAGE_SIZE 0x24c000
#define GAME_TEXT_RVA   0x1000
#define GAME_TEXT_SIZE  0x1948fc
#define GAME_TEXT_HASH  0xd24e61422309ae8cULL

namespace game {

// ---- globals ---------------------------------------------------------------------------------------------------
constexpr uintptr_t gApp = 0x005eb6a4;                    // WinFishApp* (DAT_005eb6a4)
constexpr uintptr_t FONT_JUNGLEFEVER10OUTLINE = 0x005e8cf0;  // Font** (resource globals)
constexpr uintptr_t FONT_JUNGLEFEVER12OUTLINE = 0x005e8a5c;
constexpr uintptr_t FONT_JUNGLEFEVER15OUTLINE = 0x005e8e18;
constexpr uintptr_t FONT_JUNGLEFEVER17OUTLINE = 0x005e8e40;
constexpr uintptr_t FONT_CONTINUUMBOLD12OUTLINE = 0x005e8cd0;
constexpr uintptr_t FONT_CONTINUUMBOLD14OUTLINE = 0x005e8d1c;
constexpr uintptr_t IMAGE_DIALOG = 0x005e8cc0;               // Image**
constexpr uintptr_t IMAGE_DIALOGBUTTON = 0x005e8c54;
constexpr uintptr_t IMAGE_MAINBUTTON = 0x005e8c1c;
constexpr uintptr_t IMAGE_CENTERBUTTON = 0x005e8d28;       // strip buttons, 3 states side by side (normal, over, down)
constexpr uintptr_t IMAGE_LEFTBUTTON = 0x005e8ad8;
constexpr uintptr_t IMAGE_RIGHTBUTTON = 0x005e8dec;
constexpr uintptr_t IMAGE_FATBUTTON = 0x005e8ed8;
constexpr uintptr_t IMAGE_BATTLETANKBUTTON = 0x005e8cf8;   // the main menu's big buttons: over / down images
constexpr uintptr_t IMAGE_BATTLETANKBUTTOND = 0x005e8e00;
constexpr uintptr_t IMAGE_MIDDLEBUTTON = 0x005e8b88;
constexpr uintptr_t IMAGE_MIDDLEBUTTOND = 0x005e8e9c;
constexpr uintptr_t IMAGE_SELECTORSCREEN = 0x005e8ba8;     // the main menu's background (the button panel is part of it)
constexpr uintptr_t IMAGE_EDITBOX = 0x005e8dbc;
constexpr uintptr_t IMAGE_CHECKED = 0x005e8c64;
constexpr uintptr_t IMAGE_UNCHECKED = 0x005e8d5c;

// ---- functions (all __thiscall) ---------------------------------------------------------------------------------
constexpr uintptr_t Board_Update    = 0x00547610;          // void (Board*)
constexpr uintptr_t Board_Draw      = 0x0053f5f0;          // void (Board*, Graphics*)
constexpr uintptr_t Board_Pause     = 0x0053db80;          // void (Board*, bool): the game's own pause/resume (resume
                                                           // restarts timers and sounds); called by the app on focus/dialogs
constexpr uintptr_t Graphics_SetFont    = 0x00455880;      // void (Graphics*, Font*)
constexpr uintptr_t Graphics_SetColor   = 0x00455890;      // void (Graphics*, const Color&)
constexpr uintptr_t Graphics_FillRect   = 0x00455920;      // void (Graphics*, int x, int y, int w, int h)
constexpr uintptr_t Graphics_DrawString = 0x00455cf0;      // void (Graphics*, const std::string&, int x, int y)
constexpr uintptr_t WidgetManager_DrawScreen = 0x0046d460; // bool (): this = WidgetManager* in EDI (called from
                                                           // SexyAppBase @004810e9 with edi = app+0x320), result in AL
constexpr uintptr_t Board_SaveOrDeleteGame = 0x005497a0;   // void (Board*): save the level in progress (or delete a stale save)
constexpr uintptr_t Board_CanSaveOnQuit = 0x00537bf0;      // bool (Board*): mNeedSave && mode != 3 (sandbox)
constexpr uintptr_t Board_SpawnGuppy = 0x00546d70;         // Fish* (Board*): a new small guppy
constexpr uintptr_t Board_AddCoin = 0x00544430;            // Coin* (Board*, int x, int y, int type, int arg, double fallSpeed
                                                           //   (-1 = default), int priority)
constexpr uintptr_t Board_AddMoney = 0x0053c1e0;           // void (Board*, int): earn money (shells in the Virtual Tank)
constexpr uintptr_t Board_UpdateMoneyLabel = 0x0053a360;   // void (Board*)
constexpr uintptr_t Shot_ctorKind = 0x004ec660;            // Shot* (Shot*, int x, int y, int kind): an effect; reads +0x160 (kind) before setting it
constexpr uintptr_t Coin_ReceiveMoney = 0x004d54e0;        // void (Coin*): pays the coin's value (Board_AddMoney); from Coin::Update (a clicked coin arrives) and PetCollected
constexpr uintptr_t Board_InitLevel = 0x00541a20;          // void (Board*): money, store, alien timer for a new level
constexpr uintptr_t Board_StartLevel = 0x005498b0;         // void (Board*): puts the starting fish and the pets in
constexpr uintptr_t Board_AddPet = 0x00544a90;             // GameObject* (Board*, int pet, int x, int y, bool special, bool notVirtual)
constexpr uintptr_t Board_SpawnAlien = 0x00545620;         // void (Board*, int type, int x, int y, bool sound); 9-12 = pairs
constexpr uintptr_t Board_BuyItem = 0x00549380;            // void (Board*, int item): a store button (0 = guppy)
constexpr uintptr_t Alien_ctor = 0x004ecfa0;               // Alien* (Alien*, int x, int y, int type)
constexpr uintptr_t Alien_Shoot = 0x004fa6f0;              // bool (Alien*, int x, int y): a laser click; true = it died
constexpr uintptr_t Fish_DropCoin = 0x004f1400;            // void (Fish*): coin timer (vtable 0x144)
constexpr uintptr_t GameObject_TickHunger = 0x004d6ef0;    // void (GameObject*): one hunger tick
constexpr uintptr_t GameObject_SetHungryFlash = 0x004d6920; // void (GameObject*, bool)
constexpr uintptr_t Coin_MouseDown = 0x004e5450;           // void (Coin*, int x, int y, int clicks): the player picks it up
constexpr uintptr_t Coin_Update = 0x004f5050;              // void (Coin*): falls 1.5 px a tick (0.8 in some cases)
constexpr uintptr_t Board_PlaySample = 0x00538230;         // void (Board*, int resourceId, int minDelay, double volume)
constexpr uintptr_t Board_SpendMoney = 0x00540b30;         // bool (Board*, int amount, bool loud): false (and a buzz) when broke
constexpr uintptr_t Board_DropFood = 0x00543280;           // void (Board*, int x, int y, int drift, bool silent, int extra, int quality)
constexpr uintptr_t Board_AddTextCoin = 0x005444e0;        // Coin* (Board*, int x, int y, int style, const string& text): floating text
constexpr uintptr_t Board_AddDeadFish = 0x005441f0;        // void (Board*, int x, int y, double vx, double vy, double speed, int size,
                                                           //   bool facingRight, GameObject* shadow)
constexpr uintptr_t Board_DropBonusCoins = 0x005460f0;     // void (Board*): one or two treasures rain in
constexpr uintptr_t Fish_Eat = 0x004f1ba0;                 // void (Fish*, Food*): vtable 0x134
constexpr uintptr_t DeadFish_Update = 0x004f64e0;          // void (DeadFish*)
constexpr uintptr_t DeadFish_Remove = 0x004db250;          // void (DeadFish*): vtable 0x144
constexpr uintptr_t DeadFish_vtable = 0x00597bfc;
constexpr uintptr_t Widget_MouseDown = 0x0046e3a0;         // void (Widget*, int x, int y, int clicks): shared by every widget
                                                           //   that doesn't override it (DeadFish...)
constexpr uintptr_t Board_BankCoins = 0x0053c2d0;          // void (Board*): coins still flying to the counter are banked
                                                           //   (at a game over, and when the level is won: then 4 egg pieces)
constexpr uintptr_t Alien_Die = 0x004f9c70;                // void (Alien*, bool killed): vtable Die
constexpr uintptr_t StoreScreen_Purchase = 0x0052bd70;     // GameObject* (StoreScreen*): a Virtual Tank purchase confirmed
constexpr uintptr_t Board_UnlockStoreItem = 0x005409b0;    // void (Board*, int item, bool animate): 0xb = egg piece
constexpr uintptr_t App_StartGame = 0x00552380;            // void (WinFishApp*, bool checkContinue, bool showHelp): mode
                                                           //   (App_mGameMode) and tank (+0x888 mSelectedTank) set first
constexpr uintptr_t App_RemoveGameSelector = 0x0054b360;   // void (WinFishApp*): the main menu's buttons call it (and clear
                                                           //   app +0x880) before App_StartGame
constexpr uintptr_t HighScore_AddTimeTrial = 0x00514470;   // bool (HighScoreMgr*, int tank, Profile*, int money)
constexpr uintptr_t Board_AddGuppyAt = 0x00546ef0;         // Fish* (Board*, int x, int y)
constexpr uintptr_t Board_AddBreederAt = 0x005471e0;       // Breeder* (Board*, int x, int y)
constexpr uintptr_t Board_AddOscarAt = 0x00544d80;         // void (Board*, int x, int y, bool facingRight)
constexpr uintptr_t Board_AddUltraAt = 0x00544fe0;         // void (Board*, int x, int y, bool facingRight)
constexpr uintptr_t Board_AddGekkoAt = 0x005451c0;         // void (Board*, int x, int y, bool facingRight)
constexpr uintptr_t Board_AddPentaAt = 0x00545360;         // void (Board*, int x): on the floor
constexpr uintptr_t Board_AddGrubberAt = 0x005454f0;       // void (Board*, int x): on the floor
constexpr uintptr_t Board_SetBackdrop = 0x00538a10;        // void (Board*, int)
constexpr uintptr_t GameObject_RemoveFromGame = 0x004d6830; // void (GameObject*, bool delete)
constexpr uintptr_t Alien_Think = 0x004f03d0;              // bool (Alien*): chases prey (the boss: spawns minions); false = wander
constexpr uintptr_t Alien_TryEat = 0x004e8f70;             // void (Alien*): eats the fish (or food, for Gus) it touches
constexpr uintptr_t App_StartBoard = 0x0054cbf0;           // void (WinFishApp*): a new board for the current mode and tank
constexpr uintptr_t App_RemoveBoard = 0x0054bc30;          // void (WinFishApp*)
constexpr uintptr_t App_LoadBoardGame = 0x0054cb20;        // bool (WinFishApp*): CreateBoard, Board::LoadGame (@00546aa0) from the profile's save path, then DoContinueDialog
constexpr uintptr_t App_DoContinueDialog = 0x0054b3a0;     // void (WinFishApp*): the "continue your game?" dialog (id 0x1e)
constexpr uintptr_t App_ReadBufferFromFile = 0x0047f5d0;   // bool (SexyAppBase*, const std::string& path, Buffer*, bool dontWriteToDemo)
constexpr uintptr_t App_WriteBytesToFile = 0x0047f310;     // bool (SexyAppBase*, const std::string& path, const void* data, size_t len)
constexpr uintptr_t Buffer_WriteByte = 0x00413020;         // void __stdcall (Buffer*, uchar)
constexpr uintptr_t Board_SetupLevel = 0x00541200;         // void (Board*): the level's store, prices, alien schedule, backdrop
constexpr uintptr_t App_ProcessDeferredMessage = 0x004846c0; // bool __stdcall (SexyAppBase*): handles one queued window message
constexpr uintptr_t App_UpdateFrames = 0x0054bb30;         // void (WinFishApp*): one update tick (vtable 0x20); the base one
                                                           //   (@004802d0) counts app +0x484 and updates the widgets
constexpr uintptr_t MTRand_Next = 0x0040aeb0;              // uint (MTRand*): the game's random numbers (MTRand at app +0x7b0)
constexpr uintptr_t App_LostFocus = 0x00551d50;            // void (WinFishApp*): the window lost focus (pauses the tank; vtable 0x14c)
constexpr uintptr_t App_SaveProfile = 0x0054afd0;          // bool (WinFishApp*): saves the current profile
constexpr uintptr_t App_DoTimedDialog = 0x0054b5b0;        // void (WinFishApp*, int id, bool modal, const string& header,
                                                           //   const string& lines, const string& footer, int buttons):
                                                           //   DoDialog + buttons disabled for 30 ticks (game over etc.)
constexpr uintptr_t App_ButtonDepress = 0x005527c0;        // void (WinFishApp*, int id): app buttons; dialog buttons arrive
                                                           //   as dialogId + 2000 (yes/ok) or + 3000 (no/cancel)
constexpr uintptr_t App_ShowGameSelector = 0x00552100;     // void (WinFishApp*): the main menu
constexpr uintptr_t GameObject_PetSleepy = 0x004d6b70;    // void (GameObject*, bool* sleepy): pets nap after 6480 updates without input (calls Rand)
constexpr uintptr_t GameObject_IsHungry = 0x004d6f30;      // bool (GameObject*): hungry sprite / chasing food
constexpr uintptr_t Coin_GetValue = 0x004d53a0;            // int (Coin*): money (or shells) it's worth
constexpr uintptr_t Graphics_DrawImage = 0x00455d20;      // void (Graphics*, Image*, int x, int y)
constexpr uintptr_t Graphics_DrawImageSrc = 0x00455e40;   // void (Graphics*, Image*, int x, int y, const Rect& src)
constexpr uintptr_t Graphics_DrawImageCel = 0x00456950;   // void (Graphics*, Image*, int x, int y, int cel)
constexpr uintptr_t Graphics_ctorImage = 0x00455610;       // Graphics* (Graphics* this, Image*)
constexpr uintptr_t Graphics_dtor = 0x004556f0;            // void (Graphics*)

// ---- field offsets ------------------------------------------------------------------------------------------------
constexpr int App_mFrameTime = 0x454;   // int: ms per update tick (28; the framework's default is 10)
constexpr int App_mBoard     = 0x730;   // Board*
constexpr int App_mIsWindowed = 0x343;  // bool: windowed (not fullscreen); set from the registry's ScreenMode == 0 (ReadFromRegistry @0047eda0)
constexpr int App_mIsScreenSaver = 0x358; // bool
constexpr int App_mDeferredCount = 0x364; // uint: queued window messages (std::list<MSG> mDeferredMessages, head at +0x360)
constexpr int App_mDeferredHead = 0x360;  // the list's sentinel node: +0 next, +4 prev, +8 MSG (ProcessDeferredMessage @004846c0)
constexpr uintptr_t Game_OperatorDelete = 0x00563f06;   // void __cdecl (void*): the game's operator delete (frees list nodes)
constexpr int App_mWidgetManager = 0x320; // WidgetManager*
constexpr int WM_mLastDownWidget = 0x8c;  // Widget*: the pressed widget; while set, MouseDown (@0046d940) sends every click to it
constexpr int WM_mDownButtons = 0xe8;     // uint: buttons down as the game saw them (1 left, 2 right, 4 middle); moves become drags
constexpr int WM_mActualDownButtons = 0xec; // uint
constexpr int Widget_mIsDown = 0x58;      // bool
constexpr int WM_mUpdateCnt = 0x28;       // int: the widget manager's update count
constexpr int WM_mLastInputUpdateCnt = 0xf0; // int: mUpdateCnt at the last input (also set by local-only paths: the cursor leaving the window, keys)
constexpr int App_mUpdateCount = 0x484; // int: update ticks so far
// game globals a co-op snapshot carries (not in the board's save)
constexpr uintptr_t G_FoodQuality = 0x005e89dc, G_FoodQuantity = 0x005df58c, G_Unk89c0 = 0x005e89c0;   // int
constexpr uintptr_t G_FastCoins = 0x005e89cc, G_BonesMode = 0x005e89ce;                              // bool
constexpr uintptr_t G_WadsworthHiding = 0x005e89d0, G_WadsworthX = 0x005e89d4, G_WadsworthY = 0x005e89d8; // int
constexpr int App_mMTRand = 0x7b0;      // MTRand*
constexpr int App_mDialogCount = 0x32c; // int: open dialogs (size of SexyAppBase::mDialogMap, std::map at +0x324)
constexpr int App_mGameMode  = 0x87c;   // int: 0 Adventure, 1 Time Trial, 3 sandbox, 4 Challenge, 5 Virtual Tank
constexpr int App_mBoardInactive = 0x884; // bool: a board exists but isn't being played (screens between levels)
constexpr int App_mProfile   = 0x8b8;   // Profile*
constexpr int App_vKillDialog = 0x134;  // vtable: bool KillDialog(int id)
constexpr int App_vDoDialog = 0x120;    // vtable: Dialog* DoDialog(int id, bool modal, const string& header, const string& lines,
                                        //   const string& footer, int buttons)
constexpr int App_vSwitchScreenMode = 0x114; // vtable: void SwitchScreenMode(bool windowed): as the Options' Fullscreen box
constexpr int App_vShutdown = 0xa8;    // vtable: void Shutdown() (the Quit button)
constexpr int App_vGetDialog = 0x124;   // vtable: Dialog* GetDialog(int id)
constexpr int App_mGameSelector = 0x738; // GameSelector*: the main menu while it's shown
constexpr int Widget_vResize = 0xa0;    // vtable: void Resize(int x, int y, int w, int h)
constexpr int Image_mWidth = 0x24, Image_mHeight = 0x28;
constexpr int Profile_mShells = 0x48;   // int
constexpr int Board_mPaused  = 0x94;    // bool
constexpr int Board_mGuppies = 0xa0;    // ObjVec*: the board's object lists (vector of GameObject*), see ObjVec
constexpr int Board_mOscars  = 0xa4;
constexpr int Board_mCoins   = 0xa8;
constexpr int Board_mFishPets = 0xb4;
constexpr int Board_mBreeders = 0xc0;
constexpr int Board_mGrubbers = 0xc4;
constexpr int Board_mGekkos  = 0xcc;
constexpr int Board_mPentas  = 0xd0;
constexpr int Board_mUltras  = 0xd4;
constexpr int Board_mSpecialFish = 0xf8;
constexpr int Board_mBonusRound = 0x2a5; // bool: the shell-collecting bonus round
constexpr int Board_mAlienTimer = 0x2c0; // int: ticks to the next alien (3000 after each wave and at the start)
constexpr int Board_mPrice   = 0x314;   // int[12]: store prices
constexpr int Board_mWeaponLevel = 0x3e4; // int: laser level (damage = level * 3)
constexpr int Board_mMoney   = 0x3f0;   // int
constexpr int Board_mFoodPrice = 0x4ac; // int: a pellet's price
constexpr int Board_mShouldSave = 0x4ee; // bool: SaveOrDeleteGame saves (else erases the save file)
constexpr int Board_mHoldFeed = 0x4ec;  // bool: the button is held after dropping food: Update keeps buying pellets
constexpr int Board_mTick    = 0x444;
constexpr int Board_mAlienType = 0x2bc; // int: the next alien to come (0 = none)
constexpr int Board_mAlienWaves = 0x45c; // int: waves so far
constexpr int Board_mLevelStartTime = 0x3b8; // int: game time (ms) when the level started   // int: ticks since the level started
constexpr int Board_mStoreSlot = 0x2e4; // int[12]: what each store button sells (-1 = empty); [0] guppy
constexpr int Board_mTank    = 0x3c8;
constexpr int Board_mBackdrop = 0x3e8;  // int   // int: 1-4, 5 = the pets-only bonus tank
constexpr int Widget_mX = 0x30, Widget_mY = 0x34, Widget_mWidth = 0x38, Widget_mHeight = 0x3c;
constexpr int GameObject_mType = 0x8c;  // 0 guppy, 5 oscar, 6 ultra, 7 gekko, 8 penta, 9 grubber, 10 breeder, 0x19 coin...
constexpr int GameObject_mHungerTimer = 0x9c;   // int: below 1 = starved; the hungry flash starts at mHungryThreshold + 4
constexpr int GameObject_mHungryThreshold = 0xa4;
constexpr int GameObject_mSongId = 0x11c;      // int: -1 unless singing (Virtual Tank)
constexpr int Alien_mSpeedDiv = 0x1b8;  // double: higher = slower
constexpr int Alien_mAlienType = 0x1ec; // int
constexpr int Alien_mHealth = 0x1f0, Alien_mMaxHealth = 0x1f8;   // double
constexpr int Coin_mXD = 0x158, Coin_mYD = 0x160;   // double: position
constexpr int Food_mXD = 0x158;         // double
constexpr int GameObject_vDie = 0x160;  // vtable: void Die(bool killed)
constexpr int Widget_mMouseVisible = 0x55; // bool: gets mouse clicks
constexpr int DeadFish_mYD = 0x160, DeadFish_mVY = 0x178;   // double
constexpr int DeadFish_mFloatUp = 0x188; // bool
constexpr int DeadFish_mLife = 0x1a0;   // int: counts down (fades out below 0x69)
constexpr int Board_mDeadFish = 0x98;   // ObjVec*
constexpr int Board_mFood    = 0xac;
constexpr int Board_mAliens  = 0xb8;
constexpr int Board_mBilaterus = 0xf4;
constexpr int Coin_mCoinType = 0x194;   // int: 1-7 coins/gems/treasure (+7: dropped by pets), 0xf.. specials
constexpr int Coin_mCollected = 0x198;  // bool
constexpr int WM_mImage      = 0x60;    // Image*: the screen image DrawScreen draws into
constexpr int Container_vMarkAllDirty = 0x28; // WidgetContainer vtable: MarkAllDirty()
constexpr int Graphics_size  = 0x60;
constexpr int Graphics_mColor = 0x30;   // int[4] r,g,b,a
constexpr int Graphics_mFont  = 0x40;   // Font*
constexpr int Font_vGetAscent = 0x04, Font_vGetAscentPadding = 0x08, Font_vGetHeight = 0x10;
constexpr int Font_vStringWidth = 0x1c; // virtual int StringWidth(const std::string&): Font vtable dtor,GetAscent,..,GetLineSpacing(0x18),StringWidth(0x1c),CharWidth,CharWidthKern(0x24),DrawString(0x28)

// ---- types ----------------------------------------------------------------------------------------------------
struct ObjVec { uint32_t alloc; void** first; void** last; void** end; };   // std::vector<GameObject*>
struct Color { int r, g, b, a; };

// std::string of MSVC 7.1 (0x1c bytes): allocator, 16-byte buffer or pointer, size, capacity. Capacity < 16 means the
// text is in the buffer. Built here only for passing to the game by const reference (the game never frees it).
struct MsvcString {
    uint32_t alloc;
    union { char buf[16]; const char* ptr; } bx;
    uint32_t size;
    uint32_t res;
};

// the objects in one of the board's lists (Board_mGuppies...)
inline int Count(void* board, int list) { ObjVec* v = *reinterpret_cast<ObjVec**>(static_cast<char*>(board) + list); return v && v->first ? int(v->last - v->first) : 0; }
inline void* Item(void* board, int list, int i) { return (*reinterpret_cast<ObjVec**>(static_cast<char*>(board) + list))->first[i]; }

template <typename T> inline T& at(void* base, int off) { return *reinterpret_cast<T*>(static_cast<char*>(base) + off); }

// a std::string for passing text to the game by const reference (it copies or reads it, never frees it); `s` must
// outlive the call
inline MsvcString MakeString(const char* s)
{
    MsvcString r{};
    size_t n = strlen(s);
    r.size = (uint32_t)n;
    if (n < 16) { memcpy(r.bx.buf, s, n + 1); r.res = 15; }
    else { r.bx.ptr = s; r.res = (uint32_t)n; }
    return r;
}

}  // namespace game
