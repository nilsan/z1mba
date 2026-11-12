#!/usr/bin/env python3
"""Print the screen presets for the native layout test (tests/run.sh).

    python3 tools/screen_presets.py [screens/]

One line per preset: name width height font_small font_medium font_big, read
from the substitutions at the top of each screens/*.yaml. Stdlib only, so the
YAML is read line by line: keep those five as plain `key: "value"` lines.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

KEYS = ("screen_width", "screen_height", "font_small", "font_medium", "font_big")
LINE = re.compile(r'^\s+(\w+):\s*"?(\d+)"?\s*(?:#.*)?$')


def read(path: Path) -> list[int]:
    found: dict[str, int] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if m := LINE.match(line):
            found.setdefault(m.group(1), int(m.group(2)))
    missing = [k for k in KEYS if k not in found]
    if missing:
        raise ValueError(f"{path}: no {', '.join(missing)} in substitutions")
    return [found[k] for k in KEYS]


def presets(folder: Path) -> list[tuple[str, list[int]]]:
    return [(p.stem, read(p)) for p in sorted(folder.glob("*.yaml"))]


if __name__ == "__main__":
    folder = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "screens"
    for name, values in presets(folder):
        print(name, *values)
