#!/usr/bin/env bash
# Builds and runs the host tests (hosttests/): the mod's pure logic (JSON, base64, URLs, room codes, version compare,
# the word filter) compiled for this machine with its own compiler, no Windows, Wine or game needed. CI runs this.
# UBSan is always on. AddressSanitizer is opt-in (HOST_ASAN=1): GCC 12's runtime can loop forever in
# "AddressSanitizer:DEADLYSIGNAL" on some CI kernels, so CI does not use it. Every run is limited to 60 seconds.
#   tools/host-tests.sh              # uses $CXX_HOST or c++; HOST_ASAN=1 adds AddressSanitizer
set -euo pipefail
cd "$(dirname "$0")/.."
cxx="${CXX_HOST:-c++}"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
fail=0
san="undefined"
[ "${HOST_ASAN:-0}" = 1 ] && san="address,undefined"
for t in hosttests/*_test.cpp; do
  n="$(basename "$t" .cpp)"
  "$cxx" -std=c++17 -O1 -Wall -Wextra -Werror -fsanitize="$san" -fno-sanitize-recover=undefined \
    -Iinclude -Imods/coop -Icore -o "$out/$n" "$t"
  timeout 60 "$out/$n" || fail=1
done
exit $fail
