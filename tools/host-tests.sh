#!/usr/bin/env bash
# Builds and runs the host tests (hosttests/): the mod's pure logic (JSON, base64, URLs, room codes, version compare,
# the word filter) compiled for this machine with its own compiler, no Windows, Wine or game needed. CI runs this.
# AddressSanitizer is used when a probe program with it runs here (it can hang or fail on some kernels/containers);
# otherwise the tests run with UBSan only, and every run is limited to 60 seconds.
#   tools/host-tests.sh              # uses $CXX_HOST or c++
set -euo pipefail
cd "$(dirname "$0")/.."
cxx="${CXX_HOST:-c++}"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
fail=0
san="address,undefined"
printf 'int main(){char*p=new char[4];p[0]=1;int r=p[0];delete[] p;return r-1;}\n' > "$out/probe.cpp"
if ! "$cxx" -std=c++17 -fsanitize=address -o "$out/probe" "$out/probe.cpp" 2>/dev/null || ! timeout 20 "$out/probe" 2>/dev/null; then
  san="undefined"
  echo "note: AddressSanitizer does not run here; the host tests use UBSan only"
fi
for t in hosttests/*_test.cpp; do
  n="$(basename "$t" .cpp)"
  "$cxx" -std=c++17 -O1 -Wall -Wextra -Werror -fsanitize="$san" -fno-sanitize-recover=undefined \
    -Iinclude -Imods/coop -Icore -o "$out/$n" "$t"
  timeout 60 "$out/$n" || fail=1
done
exit $fail
