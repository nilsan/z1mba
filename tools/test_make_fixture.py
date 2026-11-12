"""Tests for make_fixture. Run: python3 -m unittest discover -s tools"""

from __future__ import annotations

import json
import pathlib
import tempfile
import unittest

import make_fixture as mf


class AnonymiseTests(unittest.TestCase):
    def test_names_and_addresses_replaced(self) -> None:
        events = [
            {"t": 100.0, "kind": "udp", "raw": "Makera_Z1_123456,10.1.2.3,2222,0,Idle"},
            {"t": 100.5, "kind": "status", "msg": "<Idle|T:4,1.423,-1>\n"},
            {"t": 101.0, "kind": "result", "msg": "#2 closed by Z1 at 10.1.2.3"},
            {"t": 101.2, "kind": "result", "msg": "#3 answered"},
            {"t": 102.0, "kind": "udp", "raw": "Makera_Z1_123456,10.1.2.3,2222,0,Tool"},
        ]
        with tempfile.TemporaryDirectory() as d:
            src, dst = pathlib.Path(d, "in.jsonl"), pathlib.Path(d, "out.txt")
            src.write_text("".join(json.dumps(e) + "\n" for e in events))
            self.assertEqual(mf.convert(str(src), str(dst)), 4)
            out = dst.read_text()
        self.assertNotIn("123456", out)
        self.assertNotIn("10.1.2.3", out)
        self.assertEqual(
            out.splitlines(),
            [
                "0 U Makera_Z1_000000,192.168.1.50,2222,0,Idle",
                "500 S <Idle|T:4,1.423,-1>",
                "1000 F #2 closed by Z1 at 192.168.1.50",
                "2000 U Makera_Z1_000000,192.168.1.50,2222,0,Tool",
            ],
        )

    def test_second_machine_gets_its_own_placeholder(self) -> None:
        a = mf.Anonymiser()
        a.learn("Makera_Z1_111111,10.0.0.1,2222,0,Idle")
        a.learn("Makera_Z1_222222,10.0.0.2,2222,0,Idle")
        self.assertEqual(a("Makera_Z1_111111 10.0.0.1"), "Makera_Z1_000000 192.168.1.50")
        self.assertEqual(a("Makera_Z1_222222 10.0.0.2"), "Makera_Z1_000001 192.168.1.51")


if __name__ == "__main__":
    unittest.main()
