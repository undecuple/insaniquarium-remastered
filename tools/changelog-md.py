#!/usr/bin/env python3
"""Writes CHANGELOG.md from the changelog in include/version.h (the one place it is edited; the release zip's
CHANGELOG.txt and the release page text come from the same block).
  tools/changelog-md.py            writes CHANGELOG.md
  tools/changelog-md.py --check    fails if CHANGELOG.md differs from what version.h gives
"""
import re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def render():
    src = (ROOT / "include/version.h").read_text()
    out = ["# Changelog", "",
           "What changed in each version, newest first. Generated from `include/version.h` by `tools/changelog-md.py`:",
           "edit it there, not here. Co-op only joins players on the same version.", ""]
    for ver, body in re.findall(r'\{\s*"([^"]+)",\s*\{(.*?)nullptr\s*\}\s*\}', src, re.S):
        lines = [l.replace('\\"', '"') for l in re.findall(r'"((?:[^"\\]|\\.)*)"', body)]
        out += [f"## {ver}", ""]
        for l in lines:
            out.append("  " + l.strip() if l.startswith("  ") else "- " + l)
        out.append("")
    return "\n".join(out).rstrip("\n") + "\n"


def main():
    text, path = render(), ROOT / "CHANGELOG.md"
    if "--check" in sys.argv:
        ok = path.exists() and path.read_text() == text
        print(("ok     " if ok else "DIFFERS ") + str(path))
        sys.exit(0 if ok else 1)
    path.write_text(text)
    print(f"{path} written")


if __name__ == "__main__":
    main()
