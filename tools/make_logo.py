#!/usr/bin/env python3
"""Make the z1mba logo SVGs from a hand trace of the Zimba's skyline.

    python3 tools/make_logo.py [assets/logo] [--view saulakopf|schruns] [--header components/z1/z1_logo.h]

Each view is a trace of one photo on Wikimedia Commons, in that photo's preview
pixels (y grows downwards), with the crop that makes the logo. The logo is drawn
from the trace, so it carries the photo's licence: see VIEWS and
assets/logo/README.md.
"""

from __future__ import annotations

import argparse
import itertools
from dataclasses import dataclass
from pathlib import Path

Point = tuple[float, float]


@dataclass(frozen=True)
class View:
    trace: list[Point]
    x0: float  # crop for the mark and the outline
    x1: float
    base: float
    icon_x0: float  # tighter crop for the square icon
    icon_x1: float
    icon_base: float
    source: str
    credit: str
    licence: str


# fmt: off
VIEWS = {
    # The north-east face from the Saulakopf (2517 m): the full pyramid.
    "saulakopf": View(
        trace=[
            (200, 1638), (267, 1584), (313, 1544), (360, 1491), (400, 1451), (440, 1424), (467, 1378),
            (493, 1338), (507, 1278), (520, 1231), (533, 1184), (544, 1131), (553, 1098), (573, 1064),
            (600, 1024), (616, 1011), (640, 971), (660, 944), (693, 924), (720, 915), (760, 891),
            (800, 871), (833, 851), (853, 838), (873, 824), (887, 804), (904, 791), (917, 787),
            (933, 795), (949, 808), (963, 818), (984, 844), (997, 864), (1011, 884), (1024, 911),
            (1040, 924), (1067, 944), (1087, 971), (1104, 987), (1120, 1011), (1133, 1031), (1149, 1051),
            (1160, 1058), (1173, 1038), (1184, 1031), (1197, 1044), (1207, 1064), (1220, 1084),
            (1240, 1098), (1260, 1124), (1280, 1151), (1300, 1178), (1320, 1211), (1340, 1244),
            (1360, 1278), (1380, 1318), (1400, 1344), (1427, 1371), (1467, 1411), (1507, 1451),
            (1547, 1491), (1600, 1531),
        ],
        x0=330, x1=1530, base=1760,
        icon_x0=420, icon_x1=1420, icon_base=1380,
        source="https://commons.wikimedia.org/wiki/File:Zimba_vom_Saulakopf_1.JPG",
        credit="Friedrich Böhringer",
        licence="CC BY-SA 2.5",
    ),
    # From Schruns in the Montafon: the leaning horn with its shoulder and saddle.
    "schruns": View(
        trace=[
            (0, 524), (24, 488), (52, 464), (76, 448), (92, 438), (100, 434), (108, 442), (120, 450),
            (148, 440), (172, 428), (188, 404), (200, 384), (216, 372), (240, 356), (256, 336), (268, 312),
            (280, 294), (300, 290), (324, 284), (340, 272), (344, 248), (352, 232), (360, 220), (366, 200),
            (374, 180), (384, 168), (400, 164), (412, 166), (424, 176), (438, 192), (452, 200), (476, 206),
            (492, 220), (504, 240), (516, 268), (526, 286), (536, 280), (552, 270), (568, 267), (584, 272),
            (596, 288), (604, 306), (632, 310), (652, 312), (672, 302), (696, 301), (716, 312), (724, 316),
            (744, 300), (756, 280), (772, 260), (776, 244), (800, 240), (812, 218), (828, 214), (840, 206),
            (852, 200), (872, 196), (880, 180), (900, 172), (928, 164), (960, 168),
        ],
        x0=150, x1=724, base=420,
        icon_x0=230, icon_x1=724, icon_base=380,
        source="https://commons.wikimedia.org/wiki/File:Zimba_vom_Lidl_Parkplatz_in_Schruns.jpg",
        credit="Helge Klaus Rieder",
        licence="CC0 1.0 (public domain)",
    ),
}
# fmt: on

INK = "#1f2a36"
TILE = "#e8eef4"


def crop(points: list[Point], x0: float, x1: float) -> list[Point]:
    def at(x: float) -> Point:
        for (ax, ay), (bx, by) in itertools.pairwise(points):
            if ax <= x <= bx:
                return (x, ay + (by - ay) * (x - ax) / (bx - ax))
        raise ValueError(f"{x} is outside the trace")

    inner = [p for p in points if x0 < p[0] < x1]
    return [at(x0), *inner, at(x1)]


def shape(trace: list[Point], x0: float, x1: float, base: float) -> tuple[list[Point], float, float]:
    """The crop, standing on a flat base, moved so the summit touches y = 0."""
    pts = crop(trace, x0, x1)
    top = min(y for _, y in pts)
    return [(x - x0, min(y, base) - top) for x, y in pts], x1 - x0, base - top


def fmt(pts: list[Point]) -> str:
    return " L".join(f"{round(x, 1):g} {round(y, 1):g}" for x, y in pts)


def svg_filled(pts: list[Point], w: float, h: float, ink: str = INK) -> str:
    d = f"M0 {h:g} L{fmt(pts)} L{w:g} {h:g} Z"
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w:g} {h:g}"><path d="{d}" fill="{ink}"/></svg>\n'


def svg_line(pts: list[Point], w: float, h: float, ink: str = INK, stroke: float = 12) -> str:
    """Just the skyline: the path for milling the logo with a V-bit."""
    m = stroke
    d = f"M{fmt([(x + m, y + m) for x, y in pts])}"
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w + 2 * m:g} {h + 2 * m:g}">'
        f'<path d="{d}" fill="none" stroke="{ink}" stroke-width="{stroke:g}" '
        f'stroke-linejoin="round" stroke-linecap="round"/></svg>\n'
    )


def svg_icon(pts: list[Point], w: float, h: float, ink: str = INK) -> str:
    """Square, for a favicon or the Home Assistant device: the mountain rises from the
    bottom edge of a rounded tile, its summit a little below the top."""
    size, top_pad = 512, 72
    s = min(size / w, (size - top_pad) / h)
    ox = (size - w * s) / 2
    tile = [(ox + x * s, top_pad + y * s) for x, y in pts]
    d = f"M{ox:g} {size} L{fmt(tile)} L{ox + w * s:g} {size} Z"
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {size} {size}">'
        f'<defs><clipPath id="t"><rect width="{size}" height="{size}" rx="96"/></clipPath></defs>'
        f'<g clip-path="url(#t)"><rect width="{size}" height="{size}" fill="{TILE}"/>'
        f'<path d="{d}" fill="{ink}"/></g></svg>\n'
    )


def cpp_header(pts: list[Point], w: float, h: float, v: View) -> str:
    """The skyline for the screen (components/z1/z1_screen.h), scaled to 1000 wide."""
    s = 1000 / w
    rows = ",\n".join(
        "    " + ", ".join(f"{{{x * s:.0f}, {y * s:.0f}}}" for x, y in chunk)
        for chunk in itertools.batched(pts, 8)
    )
    return (
        "#pragma once\n"
        "// Generated by tools/make_logo.py, do not edit. The Zimba's skyline: x right,\n"
        "// y down from the summit, on a LOGO_WIDTH x LOGO_HEIGHT grid.\n"
        f"// Traced from {v.source}\n// by {v.credit}, {v.licence}.\n"
        "#include <cstddef>\n\nnamespace z1lib {\n\n"
        f"constexpr int LOGO_WIDTH = 1000;\nconstexpr int LOGO_HEIGHT = {h * s:.0f};\n"
        f"constexpr size_t LOGO_POINTS = {len(pts)};\n"
        f"constexpr float LOGO[LOGO_POINTS][2] = {{\n{rows},\n}};\n\n}}  // namespace z1lib\n"
    )


def write(out: Path, name: str, header: Path | None = None) -> list[Path]:
    v = VIEWS[name]
    out.mkdir(parents=True, exist_ok=True)
    mark = shape(v.trace, v.x0, v.x1, v.base)
    icon = shape(v.trace, v.icon_x0, v.icon_x1, v.icon_base)
    files = {
        "z1mba-mark.svg": svg_filled(*mark),
        "z1mba-outline.svg": svg_line(*mark),
        "z1mba-icon.svg": svg_icon(*icon),
    }
    paths = []
    for fname, text in files.items():
        (out / fname).write_text(text)
        paths.append(out / fname)
    if header:
        header.write_text(cpp_header(*mark, v))
        paths.append(header)
    return paths


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", nargs="?", default="assets/logo")
    ap.add_argument("--view", choices=sorted(VIEWS), default="saulakopf")
    ap.add_argument("--header", default="components/z1/z1_logo.h", help="the skyline for the screen, as C++")
    a = ap.parse_args()
    v = VIEWS[a.view]
    for p in write(Path(a.out), a.view, Path(a.header) if a.header else None):
        print(f"wrote {p}")
    print(f"traced from {v.source} by {v.credit}, {v.licence}")


if __name__ == "__main__":
    main()
