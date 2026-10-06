#!/usr/bin/env bash
# release-notes.sh VERSION: writes build/release/NOTES.md, the release page's text: that version's changelog (from
# include/version.h), where to find the installation guide, and the zip's SHA-256.
set -euo pipefail
VER=$1
[ -f build/release/SHA256SUMS.txt ] || { echo "run tools/package.sh first" >&2; exit 1; }
python3 - "$VER" > build/release/NOTES.md <<'PY'
import re, sys
src = open('include/version.h').read()
lines = []
for ver, body in re.findall(r'\{\s*"([^"]+)",\s*\{(.*?)nullptr\s*\}\s*\}', src, re.S):
    if ver == sys.argv[1]:
        lines = [l.replace('\\"', '"') for l in re.findall(r'"((?:[^"\\]|\\.)*)"', body)]
sums = open('build/release/SHA256SUMS.txt').read().strip()
text = "\n".join("- " + l if not l.startswith("  ") else "  " + l.strip() for l in lines)
text += ("\n\nNeeds *Insaniquarium! Deluxe* from Steam. Installing: INSTALL.txt in the zip, or "
         "[docs/INSTALL.md](docs/INSTALL.md).\n\nSHA-256:\n```\n" + sums + "\n```\n")
print(text)
PY
echo "wrote build/release/NOTES.md"
