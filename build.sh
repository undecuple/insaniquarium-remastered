#!/usr/bin/env bash
# Builds Insaniquarium - Remastered Mod for the original game: build/dist/ddraw.dll (loader + core) and build/dist/mods/*.dll.
# Cross-compiles with 32-bit mingw-w64 (nixpkgs pkgsCross.mingw32); install = copy build/dist/* into the game folder.
#   ./build.sh                 # everything
#   ./build.sh timecontrol     # the core and one mod
#   ./build.sh --tests         # also the test-only mods (tests/) and examples (examples/) -> build/tests (never released)
set -euo pipefail
SELF=$(realpath "$0")
cd "$(dirname "$0")"
if ! command -v i686-w64-mingw32-g++ >/dev/null; then
  exec nix-shell -p pkgsCross.mingw32.buildPackages.gcc python3 curl --run "$SELF $*"
fi
tools/fetch-deps.sh
B=build; O=$B/dist
mkdir -p $B/gen $B/obj $O/mods
CXX="i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Iinclude -Ithird_party/minhook/include"
CC="i686-w64-mingw32-gcc -O2 -Ithird_party/minhook/include -Ithird_party/minhook/src"
LDFLAGS="-shared -static -static-libgcc -static-libstdc++ -s -Wl,--no-insert-timestamp"   # no link time: the same source gives the same DLLs
CORELIBS="-lshell32 -lgdi32"

python3 tools/gen-proxy.py $B/gen >/dev/null
MH=""
for f in hook buffer trampoline hde/hde32; do
  o=$B/obj/mh_$(basename $f).o
  [ $o -nt third_party/minhook/src/$f.c ] || $CC -c third_party/minhook/src/$f.c -o $o
  MH="$MH $o"
done

# version info in every DLL (file properties; antivirus tools also look for it): version.h's REMOD_VERSION
VER=$(sed -n 's/^#define REMOD_VERSION "\(.*\)"/\1/p' include/version.h)
verres() {   # verres NAME DESCRIPTION -> $B/obj/NAME.res.o
  local v=${VER//./,}
  cat > $B/gen/$1.rc <<RC
1 VERSIONINFO
FILEVERSION $v,0
PRODUCTVERSION $v,0
FILEOS 0x40004
FILETYPE 2
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904b0"
    BEGIN
      VALUE "CompanyName", "Insaniquarium - Remastered Mod (unofficial fan project)"
      VALUE "FileDescription", "$2"
      VALUE "FileVersion", "$VER"
      VALUE "InternalName", "$1"
      VALUE "OriginalFilename", "$1.dll"
      VALUE "ProductName", "Insaniquarium - Remastered Mod"
      VALUE "ProductVersion", "$VER"
      VALUE "LegalCopyright", "MIT licence. Unofficial fan project, not affiliated with PopCap Games or EA"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x409, 1200
  END
END
RC
  i686-w64-mingw32-windres $B/gen/$1.rc -O coff -o $B/obj/$1.res.o
}

# the core: ddraw proxy stubs + loader + API
$CC -c $B/gen/proxy_names.c -o $B/obj/proxy_names.o
verres ddraw "Insaniquarium - Remastered Mod loader (DirectDraw proxy: loads the mods, passes DirectDraw through)"
$CXX $LDFLAGS -o $O/ddraw.dll core/core.cpp core/display.cpp $B/obj/proxy_names.o $B/gen/proxy_stubs.S $MH $B/gen/proxy.def $B/obj/ddraw.res.o $CORELIBS
echo "built $O/ddraw.dll"

TESTS=0
if [ "${1:-}" = "--tests" ]; then TESTS=1; shift; fi
[ $# -gt 0 ] || rm -f $O/mods/*.dll   # a full build: no leftovers from mods that were removed
for d in mods/*/; do
  m=$(basename $d)
  [ $# -gt 0 ] && [[ " $* " != *" $m "* ]] && continue
  verres $m "Insaniquarium - Remastered Mod: $m mod"
  $CXX $LDFLAGS -o $O/mods/$m.dll $d*.cpp $B/obj/$m.res.o -lgdi32 -lshell32 -lws2_32 -lwinhttp
  echo "built $O/mods/$m.dll"
done
if [ $TESTS = 1 ]; then
  mkdir -p $B/tests
  for d in tests/*/ examples/*/; do $CXX $LDFLAGS -o $B/tests/$(basename $d).dll $d*.cpp; echo "built $B/tests/$(basename $d).dll"; done
fi
cp remastered-mod.ini $O/mods/remastered-mod.ini
# Valve's steam_api.dll for co-op over Steam: in mods\coop\ (a DLL directly in mods\ would be loaded as a mod)
if [ -f third_party/steamworks/steam_api.dll ] && [ -f $O/mods/coop.dll ]; then mkdir -p $O/mods/coop && cp third_party/steamworks/steam_api.dll $O/mods/coop/; fi
cp install-steam.bat $O/   # Windows installer for the Steam release
