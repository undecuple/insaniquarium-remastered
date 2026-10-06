@echo off
rem Insaniquarium - Remastered Mod for the Steam release of Insaniquarium Deluxe on Windows.
rem Run this from the Steam game folder (Steam > Insaniquarium Deluxe > Manage > Browse local files) after copying
rem ddraw.dll, mods\ and this file there. The Steam Insaniquarium.exe is a launcher that runs the game from
rem %ProgramData%\PopCap Games\Insaniquarium\popcapgame1.exe, so the loader has to be next to that file.
rem   install-steam.bat            install
rem   install-steam.bat uninstall  remove the loader again (then delete ddraw.dll and mods\ here)
set "LOADER=%ProgramData%\PopCap Games\Insaniquarium"
if /i "%~1"=="uninstall" (
  del "%LOADER%\ddraw.dll" 2>nul
  echo Insaniquarium - Remastered Mod loader removed from %LOADER%.
  goto :done
)
if not exist "%~dp0ddraw.dll" ( echo ddraw.dll is missing next to this file: copy ddraw.dll and mods here first & goto :done )
if not exist "%LOADER%" mkdir "%LOADER%"
copy /y "%~dp0ddraw.dll" "%LOADER%\ddraw.dll" >nul
echo Insaniquarium - Remastered Mod installed: loader in %LOADER%, mods in %~dp0mods
:done
rem double-clicked (no arguments): keep the window open so the result can be read
if "%~1"=="" pause
