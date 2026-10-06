#!/usr/bin/env bash
# Runs the original game with Insaniquarium - Remastered Mod under Wine on a hidden X display (Xvfb: no window appears, nothing takes
# focus) and drives it with steps, for testing without Windows:
#   tools/run-wine.sh STEP...
#   NOMODS=1 runs the game without Insaniquarium - Remastered Mod (for comparison).
#   steps: wait:SECONDS  sh:COMMAND  info (list windows)  focus  click:X:Y  move:X:Y  key:KEYSYM (xdotool names: F5, Return, a)  type:TEXT  shot:FILE.png
# Uses a copy of the game in $TESTDIR (default: /tmp/remod-test; made from $GAME, your game folder, which is never
# touched) with
# build/dist installed (DIST=FOLDER: another one, e.g. an unpacked release), and a Wine prefix in $TESTDIR/prefix. Prints mods/remastered-mod.log at the end.
# Wine: the wine-wow64 package from nixpkgs unless WINE is set (see below).
set -euo pipefail
SELF=$(realpath "$0")
cd "$(dirname "$0")/.."
T=${TESTDIR:-/tmp/remod-test}
DIST=${DIST:-build/dist}
GAME=${GAME:-}
[ -n "$GAME" ] || [ -f "$T/game/Insaniquarium.exe" ] || { echo "set GAME to your Insaniquarium folder (it is copied to \$TESTDIR, never changed)" >&2; exit 2; }
if [ -z "${REMOD_IN_SHELL:-}" ]; then
  export REMOD_IN_SHELL=1
  exec nix-shell -p wineWow64Packages.stable xvfb-run xdotool imagemagick python3 --run "$SELF $(printf '%q ' "$@")"
fi
mkdir -p "$T"
[ -f "$T/game/Insaniquarium.exe" ] || cp -r "$GAME" "$T/game"
rm -f "$T/game/ddraw.dll"
if [ -z "${NOMODS:-}" ]; then   # NOMODS=1: the unmodded game, for comparison
  cp "$DIST"/ddraw.dll "$T/game/"
  mkdir -p "$T/game/mods" && rm -f "$T/game/mods/"*.dll && cp "$DIST"/mods/*.dll "$T/game/mods/"
  [ ! -d "$DIST"/mods/coop ] || cp -r "$DIST"/mods/coop "$T/game/mods/"
  [ -z "${TESTMODS:-}" ] || cp build/tests/*.dll "$T/game/mods/"   # TESTMODS=1: also the test-only mods and examples (build.sh --tests)
  # STEAMBUILD=1 (GAME = a copy of the Steam release, PROTON set): the launcher runs the game from ProgramData, so the
  # loader goes there too, as tools/install-steam.sh does
  if [ -n "${STEAMBUILD:-}" ]; then
    L="$T/compat/pfx/drive_c/ProgramData/PopCap Games/Insaniquarium"; mkdir -p "$L"; cp "$DIST"/ddraw.dll "$L/"
  fi
fi
[ -n "${NOMODS:-}" ] || [ -f "$T/game/mods/remastered-mod.ini" ] || cp "$DIST/mods/remastered-mod.ini" "$T/game/mods/"
# INI='display.window=native display.scale=integer': settings for this run (section.key=value, set in a fresh copy of
# the default ini; a key the default doesn't have is added to the end of its section, or in a new section)
if [ -n "${INI:-}" ]; then
  python3 - "$T/game/mods/remastered-mod.ini" $INI <<'PY'
import re, sys
lines = open('remastered-mod.ini').read().splitlines()
for kv in sys.argv[2:]:
    sk, v = kv.split('=', 1); sec, key = sk.split('.', 1)
    start = next((i for i, l in enumerate(lines) if l.strip() == f'[{sec}]'), None)
    if start is None: lines += ['', f'[{sec}]', f'{key}={v}']; continue
    end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith('[')), len(lines))
    hit = next((i for i in range(start + 1, end) if re.match(rf'\s*{re.escape(key)}\s*=', lines[i])), None)
    if hit is None: lines.insert(end, f'{key}={v}')
    else: lines[hit] = f'{key}={v}'
open(sys.argv[1], 'w').write('\n'.join(lines) + '\n')
PY
fi
# WINE=/path/to/bin/wine: another Wine build (give it its own TESTDIR: prefixes of different versions don't mix).
# PROTON=/path/to/a/Proton/folder: run through Proton's own launcher (`proton run`, as Steam does), inside Steam's
# runtime (steam-run), with its prefix in $TESTDIR/compat. Its folder must be visible to steam-run (not /tmp).
if [ -n "${WINE:-}" ]; then export PATH="$(dirname "$WINE"):$PATH"; fi
export WINEDEBUG=-all WINEDLLOVERRIDES="ddraw=n,b"
# silent by default: Wine's audio drivers switched off, so a test game has no sound device and never plays through
# the speakers (nor shows up in the desktop's mixer); SOUND=1 keeps the sound
[ "${SOUND:-0}" = 1 ] || WINEDLLOVERRIDES="$WINEDLLOVERRIDES;winepulse.drv,winealsa.drv,wineoss.drv,winecoreaudio.drv=d"
if [ -n "${NOOVERRIDE:-}" ]; then   # NOOVERRIDE=1: rely on the prefix's own ddraw override (install-steam.sh)
  WINEDLLOVERRIDES=${WINEDLLOVERRIDES#ddraw=n,b}; WINEDLLOVERRIDES=${WINEDLLOVERRIDES#;}
  [ -n "$WINEDLLOVERRIDES" ] || unset WINEDLLOVERRIDES
fi
if [ -n "${PROTON:-}" ]; then
  export STEAM_COMPAT_DATA_PATH="$T/compat" STEAM_COMPAT_CLIENT_INSTALL_PATH="${STEAM_DIR:-$HOME/.local/share/Steam}"
  export WINEPREFIX="$T/compat/pfx"
  mkdir -p "$STEAM_COMPAT_DATA_PATH"
  export LAUNCH="steam-run \"$PROTON/proton\" run \"$T/game/Insaniquarium.exe\""   # full path: proton resolves a bare name elsewhere
  export KILL="steam-run \"$PROTON/files/bin/wineserver\" -k"
else
  export WINEPREFIX="$T/prefix" LAUNCH="wine Insaniquarium.exe" KILL="wineserver -k"
fi
if [ -z "${PROTON:-}" ] && [ ! -f "$WINEPREFIX/.ready" ]; then
  echo "creating the Wine prefix (once)..."
  xvfb-run -a sh -c 'wineboot -i >/dev/null 2>&1; wineserver -w'
  touch "$WINEPREFIX/.ready"
fi
# windowed mode (registry ScreenMode=0), so the game window is a normal window that can be focused and clicked;
# WINDOWED=0 leaves the game's own setting (Proton otherwise starts it fullscreen, where headless input doesn't work)
if [ "${WINDOWED:-1}" = 1 ]; then
  REG="reg add HKCU\\Software\\PopCap\\Insaniquarium /v ScreenMode /t REG_DWORD /d 0 /f"
  if [ -n "${PROTON:-}" ]; then steam-run "$PROTON/proton" run $REG >/dev/null 2>&1 || true; steam-run "$PROTON/files/bin/wineserver" -w
  else wine $REG >/dev/null 2>&1 || true; wineserver -w; fi   # let that wineserver exit: the game's must start on the X display
fi
cat > "$T/steps.sh" <<'EOF'
set -u
cd "$GAMEDIR"
# the game's window: the largest visible "Insaniquarium" window of a real size (Proton also has a 65535x65535 helper)
gamewin() {
  for w in $(xdotool search --onlyvisible --name "Insaniquarium"); do
    eval "$(xdotool getwindowgeometry --shell "$w")"
    [ "$WIDTH" -le 4096 ] && echo "$((WIDTH * HEIGHT)) $w"
  done | sort -n | tail -1 | cut -d' ' -f2
}
[ -z "${WM:-}" ] || { $WM >/dev/null 2>&1 & sleep 1; }   # WM=/path/to/openbox: a window manager, as on a desktop
eval "$LAUNCH" > "$T/wine.log" 2>&1 &
wid=""
for step in "$@"; do
  case "$step" in
    wait:*) sleep "${step#wait:}" ;;
    click:*|move:*|key:*|type:*)
      if [ -z "$wid" ]; then   # the game only runs while its window is active: give it the focus (no window manager here)
        wid=$(gamewin)
        xdotool windowfocus "$wid" 2>/dev/null || true
      fi
      case "$step" in
        click:*) IFS=: read -r _ x y <<< "$step"   # game coordinates (640x480), scaled to fit the window (centred, as
                 eval "$(xdotool getwindowgeometry --shell "$wid")"   # [display] window=native draws it)
                 if [ $((WIDTH * 480)) -lt $((HEIGHT * 640)) ]; then sw=$WIDTH; sh=$((WIDTH * 480 / 640)); else sh=$HEIGHT; sw=$((HEIGHT * 640 / 480)); fi
                 x=$(((WIDTH - sw) / 2 + x * sw / 640)); y=$(((HEIGHT - sh) / 2 + y * sh / 480))
                 # move first: SexyApp buttons need the hover before the press
                 xdotool mousemove --window "$wid" $((x - 3)) "$y"; sleep 0.2; xdotool mousemove --window "$wid" "$x" "$y"; sleep 0.3
                 xdotool mousedown 1; sleep 0.1; xdotool mouseup 1 ;;
        move:*) IFS=: read -r _ x y <<< "$step"   # hover only (game coordinates, as click)
                eval "$(xdotool getwindowgeometry --shell "$wid")"
                if [ $((WIDTH * 480)) -lt $((HEIGHT * 640)) ]; then sw=$WIDTH; sh=$((WIDTH * 480 / 640)); else sh=$HEIGHT; sw=$((HEIGHT * 640 / 480)); fi
                xdotool mousemove --window "$wid" $(((WIDTH - sw) / 2 + x * sw / 640)) $(((HEIGHT - sh) / 2 + y * sh / 480)) ;;
        key:*) xdotool key --window "$wid" "${step#key:}" ;;
        type:*) xdotool type --window "$wid" "${step#type:}" ;;
      esac ;;
    focus) wid=$(gamewin); xdotool windowfocus "$wid" 2>/dev/null || true ;;
    info) for w in $(xdotool search --name "Insaniquarium"); do echo "window $w: $(xdotool getwindowname $w) $(xdotool getwindowgeometry $w | tr '\n' ' ')"; done ;;
    sh:*) bash -c "${step#sh:}" ;;
    shot:*) import -window root "${step#shot:}"; echo "shot ${step#shot:}" ;;
  esac
done
eval "$KILL"
# and anything of this test copy that outlived it (Proton's launcher can leave the game running)
sleep 1; pkill -f "$(basename "$T").game.Insaniquarium\.ex[e]" 2>/dev/null || true
EOF
SCREEN=${SCREEN:-800x600x24}   # the game runs windowed (see WINDOWED), so it isn't scaled
GAMEDIR="$T/game" T="$T" xvfb-run -a -s "-screen 0 $SCREEN" bash "$T/steps.sh" "$@" || true
echo "---- mods/remastered-mod.log"
cat "$T/game/mods/remastered-mod.log" 2>/dev/null || echo "(no log: ddraw.dll was not loaded)"
