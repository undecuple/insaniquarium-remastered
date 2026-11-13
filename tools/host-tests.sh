#!/usr/bin/env bash
# Builds and runs the host tests (hosttests/): the mod's pure logic (JSON, base64, URLs, room codes, version compare,
# the word filter) compiled for this machine with its own compiler, no Windows, Wine or game needed. CI runs this.
#   tools/host-tests.sh              # uses $CXX_HOST or c++
set -euo pipefail
cd "$(dirname "$0")/.."
cxx="${CXX_HOST:-c++}"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
fail=0
for t in hosttests/*_test.cpp; do
  n="$(basename "$t" .cpp)"
  "$cxx" -std=c++17 -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -Iinclude -Imods/coop -Icore -o "$out/$n" "$t"
  "$out/$n" || fail=1
done
exit $fail
