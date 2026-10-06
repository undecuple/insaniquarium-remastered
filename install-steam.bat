@echo off
rem Insaniquarium - Remastered Mod for the Steam release of Insaniquarium Deluxe on Windows.
rem Run this from the Steam game folder (Steam > Insaniquarium Deluxe > Manage > Browse local files) after copying
rem ddraw.dll, mods\ and this file there. The Steam Insaniquarium.exe is a launcher that runs the game from
rem %ProgramData%\PopCap Games\Insaniquarium\popcapgame1.exe, so the loader has to be next to that file.
rem   install-steam.bat            install
rem   install-steam.bat uninstall  remove the loader and the mod's files again (your settings stay)
set "LOADER=%ProgramData%\PopCap Games\Insaniquarium"
rem files in use: replacing them under a running game can crash it
tasklist /fi "imagename eq popcapgame1.exe" 2>nul | find /i "popcapgame1.exe" >nul && ( echo Insaniquarium is running: close it first, then run this again. & goto :done )
if /i "%~1"=="uninstall" (
  del "%LOADER%\ddraw.dll" 2>nul
  rem only the mod's own files, listed in mods\remod-files.txt: other mods stay, and so do your settings and the log
  if exist "%~dp0mods\remod-files.txt" for /f "usebackq tokens=* delims=" %%f in ("%~dp0mods\remod-files.txt") do call :remove "%%f"
  del "%~dp0mods\remod-files.txt" "%~dp0mods\remod-files.installed" 2>nul
  rd "%~dp0mods\coop" 2>nul
  echo Insaniquarium - Remastered Mod removed: the loader from %LOADER%, its files from %~dp0mods
  echo ^(kept mods\remastered-mod.ini and the log; delete the mods folder to remove them too^). Delete ddraw.dll here too.
  goto :done
)
if not exist "%~dp0ddraw.dll" ( echo ddraw.dll is missing next to this file: copy ddraw.dll and mods here first & goto :done )
if not exist "%LOADER%" mkdir "%LOADER%"
copy /y "%~dp0ddraw.dll" "%LOADER%\ddraw.dll" >nul
echo Insaniquarium - Remastered Mod installed: loader in %LOADER%, mods in %~dp0mods
:done
rem double-clicked (no arguments): keep the window open so the result can be read
if "%~1"=="" pause
goto :eof

:remove
set "F=%~1"
if "%F:~0,8%"=="version " goto :eof
if "%F%"=="" goto :eof
del "%~dp0mods\%F%" 2>nul
goto :eof
