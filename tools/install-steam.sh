#!/usr/bin/env bash
# Installs Insaniquarium - Remastered Mod (build/dist, or the release folder this script sits in) into the Steam release of Insaniquarium Deluxe (app 3320) on Linux / Steam Deck.
#   tools/install-steam.sh                 # find the game in your Steam libraries, install
#   tools/install-steam.sh --uninstall     # remove Insaniquarium - Remastered Mod again
#   tools/install-steam.sh --game DIR --prefix PFX   # explicit folders (also used by run-wine.sh's STEAMBUILD test)
#
# Why two places: the Steam Insaniquarium.exe is a launcher that writes the game out as
# <prefix>/drive_c/ProgramData/PopCap Games/Insaniquarium/popcapgame1.exe and starts it there, so the loader
# (ddraw.dll) must sit next to that file; mods/ and remastered-mod.ini go in the game folder (the core finds it from the
# launcher's -changedir= argument). Then set the game's Steam launch options to: WINEDLLOVERRIDES="ddraw=n,b" %command%
set -euo pipefail
cd "$(dirname "$0")"
if [ -f ddraw.dll ]; then SRC=.   # unpacked release: the files are next to this script
else cd ..; SRC=build/dist; fi
APP=3320 GAME="" PFX="" UNINSTALL=0
while [ $# -gt 0 ]; do
  case "$1" in
    --game) GAME=$2; shift ;;
    --prefix) PFX=$2; shift ;;
    --uninstall) UNINSTALL=1 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
  shift
done
if [ -z "$GAME" ]; then
  # Steam's folder: STEAM_DIR, or the usual places (native package, ~/.steam link, Flatpak)
  for STEAM in ${STEAM_DIR:-} "$HOME/.local/share/Steam" "$HOME/.steam/steam" "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam" \
               "$HOME/.var/app/com.valvesoftware.Steam/data/Steam"; do
    [ -f "$STEAM/steamapps/libraryfolders.vdf" ] || continue
    while IFS= read -r lib; do
      if [ -f "$lib/steamapps/appmanifest_$APP.acf" ]; then
        GAME="$lib/steamapps/common/Insaniquarium Deluxe"; PFX=${PFX:-"$lib/steamapps/compatdata/$APP/pfx"}; break 2
      fi
    done < <(echo "$STEAM"; grep -o '"path"[[:space:]]*"[^"]*"' "$STEAM/steamapps/libraryfolders.vdf" | cut -d'"' -f4)
  done
  [ -n "$GAME" ] || { echo "Insaniquarium Deluxe (Steam app $APP) not found in your Steam libraries (set STEAM_DIR to your Steam folder)" >&2; exit 1; }
fi
LOADER_DIR="$PFX/drive_c/ProgramData/PopCap Games/Insaniquarium"
echo "game folder: $GAME"
echo "loader folder: $LOADER_DIR"
# files in use: replacing them under a running game can crash it
if pgrep -f 'popcapgame1\.ex[e]' >/dev/null 2>&1; then echo "Insaniquarium is running: close it first, then run this again." >&2; exit 1; fi
# the mod's own files (paths relative to mods/): what this installed last time, else this release's list
remod_files() {
  local list
  for list in "$GAME/mods/remod-files.installed" "$GAME/mods/remod-files.txt" "$SRC/mods/remod-files.txt"; do
    [ -f "$list" ] && { tr -d '\r' < "$list" | grep -v '^version ' | tr '\\' /; return; }
  done
}

if [ $UNINSTALL = 1 ]; then
  rm -f "$LOADER_DIR/ddraw.dll" "$GAME/ddraw.dll"
  python3 - "$PFX/user.reg" <<'PY' 2>/dev/null || true
import re, sys
p = sys.argv[1]; s = open(p).read()
s = re.sub(r'\n\[Software\\\\Wine\\\\AppDefaults\\\\popcapgame1\.exe\\\\DllOverrides\][^\n]*\n"ddraw"="native,builtin"\n', '\n', s)
open(p, 'w').write(s)
PY
  # only the mod's own files: other mods in mods/ stay
  remod_files | while IFS= read -r f; do case "$f" in ''|*..*|/*) ;; *) rm -f "$GAME/mods/$f" ;; esac; done
  rmdir "$GAME/mods/coop" 2>/dev/null || true
  rm -f "$GAME/mods/remod-files.txt" "$GAME/mods/remod-files.installed"
  echo "removed Insaniquarium - Remastered Mod (kept $GAME/mods/remastered-mod.ini and the log; delete $GAME/mods to remove them too)"
  exit 0
fi
[ -f $SRC/ddraw.dll ] || { echo "build first: ./build.sh" >&2; exit 1; }
if [ ! -d "$PFX/drive_c" ]; then
  echo "No Proton prefix at $PFX yet: start the game once from Steam (with a Proton version forced in its" >&2
  echo "Properties > Compatibility), quit, then run this again." >&2
  exit 1
fi
# Wine must use our ddraw.dll for the game: a per-program DLL override in the prefix (no launch options needed)
REGKEY='[Software\\Wine\\AppDefaults\\popcapgame1.exe\\DllOverrides]'
if ! grep -qF "$REGKEY" "$PFX/user.reg" 2>/dev/null; then
  printf '\n%s %s\n"ddraw"="native,builtin"\n' "$REGKEY" "$(date +%s)" >> "$PFX/user.reg"
  echo "added a ddraw override for popcapgame1.exe to the prefix"
fi
mkdir -p "$LOADER_DIR" "$GAME/mods"
cp $SRC/ddraw.dll "$LOADER_DIR/"
cp $SRC/mods/*.dll $SRC/mods/remastered-mod.default.ini $SRC/mods/remod-files.txt "$GAME/mods/"
[ ! -d $SRC/mods/coop ] || cp -r $SRC/mods/coop "$GAME/mods/"   # steam_api.dll for co-op over Steam
# the player's remastered-mod.ini is never replaced: the game makes it from the defaults at its first start and adds
# the settings of newer versions to it; files an older version had and this one doesn't are removed then too
echo "installed. Start the game from Steam as usual (if the mods don't show, set its Launch Options to"
echo '  WINEDLLOVERRIDES="ddraw=n,b" %command%  )'
