#!/usr/bin/env bash
# Installs the build tools on Debian/Ubuntu (the CI container, node:24-bookworm): 32-bit mingw-w64 with POSIX threads
# (the co-op mod uses std::thread), its windres, Python, curl and git. Run as root.
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends g++-mingw-w64-i686-posix gcc-mingw-w64-i686-posix binutils-mingw-w64-i686 \
  python3 curl ca-certificates git >/dev/null
update-alternatives --set i686-w64-mingw32-gcc /usr/bin/i686-w64-mingw32-gcc-posix >/dev/null
update-alternatives --set i686-w64-mingw32-g++ /usr/bin/i686-w64-mingw32-g++-posix >/dev/null
i686-w64-mingw32-g++ --version | head -1
