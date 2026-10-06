#!/usr/bin/env bash
# Builds the release zip of Insaniquarium - Remastered Mod: build/release/Insaniquarium-Remastered-Mod-<version>.zip
# and its SHA-256 in build/release/SHA256SUMS.txt. Only our own files (plus third-party notices) go in, nothing from the game.
#   tools/package.sh                   # without Valve's steam_api.dll (see below)
#   tools/package.sh --with-steam-api  # with it in mods/coop/ (only where Valve's terms allow shipping it)
# steam_api.dll is Valve's Steamworks redistributable: its licence lets Steamworks partners ship it with their own Steam
# apps. Without it, co-op works over direct IP (and Steam once one is found); README.txt tells players how to add it for Steam.
set -euo pipefail
cd "$(dirname "$0")/.."
STEAMAPI=0
[ "${1:-}" != "--with-steam-api" ] || STEAMAPI=1
./build.sh >/dev/null
VER=$(sed -n 's/^#define REMOD_VERSION "\(.*\)"/\1/p' include/version.h)
NAME=Insaniquarium-Remastered-Mod-$VER
R=build/release; D=$R/$NAME
rm -rf "${D:?}" "$R/$NAME.zip"
mkdir -p "$D/mods"
cp build/dist/ddraw.dll build/dist/install-steam.bat tools/install-steam.sh "$D/"
cp build/dist/mods/*.dll build/dist/mods/remastered-mod.ini "$D/mods/"
[ $STEAMAPI = 0 ] || { mkdir -p "$D/mods/coop"; cp third_party/steamworks/steam_api.dll "$D/mods/coop/"; }
# README.txt: the README without what only works on a web page (the screenshot table, image badges)
python3 - "$D/README.txt" <<'PY'
import re, sys
text = open('README.md').read()
text = re.sub(r'## Screenshots\n.*?(?=\n## )', '', text, flags=re.S)
text = re.sub(r'\[!\[[^\]]*\]\([^)]*\)\]\(([^)]*)\)', r'\1', text)   # an image link (the Ko-fi badge): its address
text = '\n'.join(l for l in text.split('\n') if not l.lstrip().startswith('!['))
open(sys.argv[1], 'w', newline='\r\n').write(text)
PY
cp docs/INSTALL.md "$D/INSTALL.txt"
python3 - "$D/CHANGELOG.txt" <<'PY'
import re, sys
src = open('include/version.h').read()
out = ['Insaniquarium - Remastered Mod: changes in each version', '']
for ver, body in re.findall(r'\{\s*"([^"]+)",\s*\{(.*?)nullptr\s*\}\s*\}', src, re.S):
    out.append(ver)
    out += ['  ' + l.replace('\\"', '"') for l in re.findall(r'"((?:[^"\\]|\\.)*)"', body)]
    out.append('')
open(sys.argv[1], 'w', newline='\r\n').write('\n'.join(out))
PY
{
  echo "Insaniquarium - Remastered Mod $VER"
  echo "Unofficial fan project, not affiliated with PopCap Games or EA. Insaniquarium and its art belong to PopCap Games;"
  echo "nothing from the game is included here."
  echo
  echo "================================================================================"
  echo "Insaniquarium - Remastered Mod"
  echo "================================================================================"
  cat LICENSE.txt
  echo
  echo "================================================================================"
  echo "MinHook (compiled into ddraw.dll) - https://github.com/TsudaKageyu/minhook"
  echo "================================================================================"
  cat third_party/minhook/LICENSE.txt
  if [ $STEAMAPI = 1 ]; then
    echo
    echo "================================================================================"
    echo "mods/coop/steam_api.dll - Steamworks API, (c) Valve Corporation"
    echo "================================================================================"
    echo "Valve's Steamworks redistributable, distributed under the Steamworks SDK Access Agreement"
    echo "(https://partner.steamgames.com/documentation/sdk_access_agreement)."
  fi
} | sed 's/\r$//' > "$D/LICENSES.txt"
sed -i 's/$/\r/' "$D/LICENSES.txt" "$D/README.txt" "$D/INSTALL.txt"   # Notepad-friendly line ends
# a reproducible zip: entries sorted, every date = SOURCE_DATE_EPOCH (default: the last commit's time), fixed permissions
EPOCH=${SOURCE_DATE_EPOCH:-$(git log -1 --format=%ct 2>/dev/null || echo 1767225600)}
python3 - "$R" "$NAME" "$EPOCH" <<'PY'
import os, sys, time, zipfile
root, name, epoch = sys.argv[1], sys.argv[2], int(sys.argv[3])
date = time.gmtime(max(epoch, 315532800))[:6]
files = []
for d, dirs, fs in os.walk(os.path.join(root, name)):
    dirs.sort()
    files += [os.path.join(d, f) for f in sorted(fs)]
with zipfile.ZipFile(os.path.join(root, name + '.zip'), 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for f in sorted(files):
        info = zipfile.ZipInfo(os.path.relpath(f, root).replace(os.sep, '/'), date)
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = (0o755 if f.endswith('.sh') else 0o644) << 16
        z.writestr(info, open(f, 'rb').read())
PY
(cd "$R" && sha256sum "$NAME.zip" > SHA256SUMS.txt)
echo "$R/$NAME.zip"; cat "$R/SHA256SUMS.txt"
python3 -m zipfile -l "$R/$NAME.zip"
