#!/usr/bin/env bash
# Downloads the third-party sources the mod build needs into third_party/ (git-ignored):
#   MinHook (function hooking for x86, BSD-2-Clause).
set -euo pipefail
cd "$(dirname "$0")/.."
# renovate: datasource=github-tags depName=TsudaKageyu/minhook
MINHOOK_VERSION=v1.3.4
D=third_party/minhook
if [ "$(cat $D/.version 2>/dev/null)" != "$MINHOOK_VERSION" ]; then
  rm -rf "${D:?}" && mkdir -p $D
  curl -sSL "https://github.com/TsudaKageyu/minhook/archive/refs/tags/$MINHOOK_VERSION.tar.gz" | tar -xz -C $D --strip-components=1
  echo "$MINHOOK_VERSION" > $D/.version
  echo "minhook $MINHOOK_VERSION"
fi

# Valve's steam_api.dll (32-bit) for co-op over Steam, from the Steamworks.NET standalone release (which carries
# Valve's redistributable libraries). Optional: without it co-op works by address only.
# renovate: datasource=github-releases depName=rlabrecque/Steamworks.NET
STEAMWORKS_NET_VERSION=2025.164.1
S=third_party/steamworks
if [ "$(cat $S/.version 2>/dev/null)" != "$STEAMWORKS_NET_VERSION" ]; then
  rm -rf "${S:?}" && mkdir -p $S
  if curl -sSL -o $S/sw.zip "https://github.com/rlabrecque/Steamworks.NET/releases/download/$STEAMWORKS_NET_VERSION/Steamworks.NET-Standalone_$STEAMWORKS_NET_VERSION.zip"; then
    python3 - $S/sw.zip $S <<'PY'
import sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
for n in z.namelist():
    if n.endswith('Windows-x86/steam_api.dll') or n.endswith('LICENSE.txt'):
        open(sys.argv[2] + '/' + n.split('/')[-1], 'wb').write(z.read(n))
PY
    rm -f $S/sw.zip
    echo "$STEAMWORKS_NET_VERSION" > $S/.version
    echo "steam_api.dll (Steamworks.NET $STEAMWORKS_NET_VERSION)"
  else
    echo "couldn't download steam_api.dll: co-op over Steam won't be in the build" >&2
  fi
fi
