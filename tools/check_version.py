#!/usr/bin/env python3
"""Fail when the version pins across the repository disagree.

The version lives in library.json, library.properties, ESPBASE_VERSION, the
top CHANGELOG entry, and the lib_deps tag pinned in README.md, the Getting
Started page and the Blank template. CI runs this; the release steps in
docs/wiki/Development.md say to run it before tagging.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PIN = r"esp-base\.git#(\d+\.\d+\.\d+)"


def one(pattern: str, path: str) -> str:
    text = (ROOT / path).read_text(encoding="utf-8")
    m = re.search(pattern, text, flags=re.M)
    if not m:
        sys.exit(f"error: no version found in {path}")
    return m.group(1)


def main() -> int:
    found = {
        "library.json": json.loads((ROOT / "library.json").read_text(encoding="utf-8"))["version"],
        "library.properties": one(r"^version=(\d+\.\d+\.\d+)$", "library.properties"),
        "src/EspBaseConfig.h": one(r'#define ESPBASE_VERSION "(\d+\.\d+\.\d+)"', "src/EspBaseConfig.h"),
        "CHANGELOG.md": one(r"^## \[(\d+\.\d+\.\d+)\]", "CHANGELOG.md"),
        "README.md": one(PIN, "README.md"),
        "docs/wiki/Getting-Started.md": one(PIN, "docs/wiki/Getting-Started.md"),
        "examples/Blank/platformio.ini": one(PIN, "examples/Blank/platformio.ini"),
    }
    versions = set(found.values())
    for path, v in found.items():
        print(f"{v:10} {path}")
    if len(versions) != 1:
        print("error: version pins disagree", file=sys.stderr)
        return 1
    print(f"ok: everything says {versions.pop()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
