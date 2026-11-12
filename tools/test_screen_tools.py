"""Tests for screen_presets and the logo header. Run: python3 -m unittest discover -s tools"""

from __future__ import annotations

import pathlib
import tempfile
import unittest

import make_logo
import screen_presets

REPO = pathlib.Path(__file__).resolve().parent.parent


class PresetTests(unittest.TestCase):
    def test_reads_substitutions(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            pathlib.Path(d, "tiny.yaml").write_text(
                'substitutions:\n  screen_width: "250"  # after rotation\n  screen_height: "122"\n'
                '  font_small: "12"\n  font_medium: 18\n  font_big: "60"\n\ndisplay:\n  - rotation: 90\n'
            )
            self.assertEqual(screen_presets.presets(pathlib.Path(d)), [("tiny", [250, 122, 12, 18, 60])])

    def test_missing_key_is_an_error(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            pathlib.Path(d, "bad.yaml").write_text('substitutions:\n  screen_width: "250"\n')
            with self.assertRaisesRegex(ValueError, "screen_height"):
                screen_presets.presets(pathlib.Path(d))

    def test_every_shipped_preset_reads(self) -> None:
        found = screen_presets.presets(REPO / "screens")
        self.assertIn("weact-2.9in-3c", [name for name, _ in found])
        for name, (w, h, small, medium, big) in found:
            with self.subTest(name):
                self.assertTrue(w > 0 and h > 0)
                self.assertLess(small, medium)
                self.assertLess(medium, big)


class LogoHeaderTests(unittest.TestCase):
    def test_header_scales_to_1000_wide(self) -> None:
        v = make_logo.VIEWS["saulakopf"]
        pts, w, h = make_logo.shape(v.trace, v.x0, v.x1, v.base)
        text = make_logo.cpp_header(pts, w, h, v)
        self.assertIn("constexpr int LOGO_WIDTH = 1000;", text)
        self.assertIn(f"constexpr size_t LOGO_POINTS = {len(pts)};", text)
        self.assertIn(v.credit, text)
        self.assertEqual(text.count("{") - text.count("}"), 0)

    def test_checked_in_header_is_current(self) -> None:
        v = make_logo.VIEWS["saulakopf"]
        text = make_logo.cpp_header(*make_logo.shape(v.trace, v.x0, v.x1, v.base), v)
        self.assertEqual((REPO / "components/z1/z1_logo.h").read_text(), text, "run tools/make_logo.py")


if __name__ == "__main__":
    unittest.main()
