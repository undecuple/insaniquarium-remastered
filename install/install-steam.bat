@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem Insaniquarium - Remastered Mod for the Steam release of Insaniquarium Deluxe on Windows.
rem Double-click this in the unzipped download: it finds the game in your Steam libraries, copies the mods into the game
rem folder, and puts the loader next to the real game. The Steam Insaniquarium.exe is a launcher that runs the game from
rem %ProgramData%\PopCap Games\Insaniquarium\popcapgame1.exe, so the loader (ddraw.dll) has to be next to that file.
rem   install-steam.bat            install (or update: run it again after unzipping a new version)
rem   install-steam.bat uninstall  remove the loader and the mod's files again (your settings stay); uninstall-steam.bat
rem                                does the same on a double-click
rem The game folder is found in this order: this file's own folder when it is the game folder (copied there by hand, as
rem older versions needed), GAME_DIR if set, else the Steam libraries (Steam's registry entry and libraryfolders.vdf).
rem Paths are only used outside ( ) blocks or quoted: the default Steam folder is under "Program Files (x86)", and an
rem unquoted ")" inside a block ends it, so cmd stops with a syntax error before anything runs.
set "APP=3320"
set "SRC=%~dp0"
set "LOADER=%ProgramData%\PopCap Games\Insaniquarium"
set "GAME="
rem files in use: replacing them under a running game can crash it
tasklist /fi "imagename eq popcapgame1.exe" 2>nul | find /i "popcapgame1.exe" >nul && goto :running
if exist "%SRC%Insaniquarium.exe" set "GAME=%SRC:~0,-1%"
if not defined GAME if defined GAME_DIR set "GAME=%GAME_DIR%"
if not defined GAME call :findgame
if not defined GAME goto :notfound
if not exist "%GAME%\Insaniquarium.exe" goto :notfound
echo game folder:   %GAME%
echo loader folder: %LOADER%
if /i "%~1"=="uninstall" goto :uninstall

if not exist "%SRC%ddraw.dll" goto :nodll
if not exist "%SRC%mods\remod-files.txt" goto :nodll
rem the mods, unless this file already sits in the game folder next to them
if /i "%SRC%"=="%GAME%\" goto :loader
if not exist "%GAME%\mods" mkdir "%GAME%\mods" 2>nul
copy /y "%SRC%mods\*.dll" "%GAME%\mods\" >nul 2>&1 || goto :nowrite
copy /y "%SRC%mods\remastered-mod.default.ini" "%GAME%\mods\" >nul 2>&1 || goto :nowrite
copy /y "%SRC%mods\remod-files.txt" "%GAME%\mods\" >nul 2>&1 || goto :nowrite
if exist "%SRC%mods\coop" xcopy /e /i /y /q "%SRC%mods\coop" "%GAME%\mods\coop" >nul 2>&1
:loader
if not exist "%LOADER%" mkdir "%LOADER%"
copy /y "%SRC%ddraw.dll" "%LOADER%\ddraw.dll" >nul || goto :nowriteloader
echo Insaniquarium - Remastered Mod installed. Start the game from Steam as usual.
goto :done

:uninstall
del "%LOADER%\ddraw.dll" 2>nul
rem only the mod's own files, listed in mods\remod-files.txt: other mods stay, and so do your settings and the log
if exist "%GAME%\mods\remod-files.txt" for /f "usebackq tokens=* delims=" %%f in ("%GAME%\mods\remod-files.txt") do call :remove "%%f"
del "%GAME%\mods\remod-files.txt" "%GAME%\mods\remod-files.installed" 2>nul
rd "%GAME%\mods\coop" 2>nul
echo Insaniquarium - Remastered Mod removed: the loader, and its files from the game's mods folder
echo (kept mods\remastered-mod.ini and the log; delete the mods folder to remove them too).
if exist "%GAME%\ddraw.dll" echo A ddraw.dll is still in the game folder (copied there by hand for older versions): delete it if it is the mod's.
goto :done

:running
echo Insaniquarium is running: close it first, then run this again.
goto :done
:notfound
echo Insaniquarium Deluxe wasn't found in your Steam libraries. Install it from Steam, or set GAME_DIR to its folder:
echo   set GAME_DIR=D:\SteamLibrary\steamapps\common\Insaniquarium Deluxe
echo and run install-steam.bat again from that command prompt.
goto :done
:nodll
echo ddraw.dll or mods\ is missing next to this file: unzip the whole download, then run this again.
goto :done
:nowrite
echo Couldn't copy the mods into the game folder. Right-click install-steam.bat and pick "Run as administrator".
goto :done
:nowriteloader
echo Couldn't copy ddraw.dll to the loader folder. Right-click install-steam.bat and pick "Run as administrator".
goto :done

:done
rem double-clicked (no arguments): keep the window open so the result can be read
if "%~1"=="" pause
goto :eof

:findgame
rem Steam's folder from the registry (forward slashes there), then every library in its libraryfolders.vdf
set "STEAM="
for /f "tokens=2,*" %%a in ('reg query "HKCU\Software\Valve\Steam" /v SteamPath 2^>nul ^| find /i "SteamPath"') do set "STEAM=%%b"
if not defined STEAM for /f "tokens=2,*" %%a in ('reg query "HKLM\SOFTWARE\WOW6432Node\Valve\Steam" /v InstallPath 2^>nul ^| find /i "InstallPath"') do set "STEAM=%%b"
if not defined STEAM goto :eof
set "STEAM=%STEAM:/=\%"
call :trylib "%STEAM%"
if not exist "%STEAM%\steamapps\libraryfolders.vdf" goto :eof
rem lines like:   "path"   "D:\\SteamLibrary"  (backslashes doubled; the second word keeps its quotes)
for /f "usebackq tokens=1,*" %%a in ("%STEAM%\steamapps\libraryfolders.vdf") do if /i "%%~a"=="path" call :trylib %%b
goto :eof

:trylib
if defined GAME goto :eof
set "L=%~1"
set "L=%L:\\=\%"
if exist "%L%\steamapps\appmanifest_%APP%.acf" if exist "%L%\steamapps\common\Insaniquarium Deluxe\Insaniquarium.exe" set "GAME=%L%\steamapps\common\Insaniquarium Deluxe"
goto :eof

:remove
set "F=%~1"
if "%F:~0,8%"=="version " goto :eof
if "%F%"=="" goto :eof
del "%GAME%\mods\%F%" 2>nul
goto :eof
