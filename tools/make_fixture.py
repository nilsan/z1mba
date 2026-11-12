#!/usr/bin/env python3
"""Turn a z1probe --log file into an anonymised replay fixture for the C++ tests.

    python3 tools/make_fixture.py runs/04-studio.jsonl tests/fixtures/studio-session.txt

Each line: <ms since the first event> <U|S|F> <text>
  U  a UDP announcement (raw datagram)
  S  a status reply (the payload of a STATUS frame)
  F  a failed poll
The probe logs every announcement only when it changes, which is also what the
tracker reacts to, so the replay keeps the real timing of state changes.

Raw logs name your machine (its announced name carries what looks like a serial
number) and your LAN addresses, so they stay in runs/, which git ignores. The
fixture replaces each machine name and IPv4 address with a placeholder.
"""

from __future__ import annotations

import json
import re
import sys

PLACEHOLDER_NAME = "Makera_Z1_000000"
PLACEHOLDER_IPS = [f"192.168.1.{50 + i}" for i in range(200)]
IPV4 = re.compile(r"\b(?:\d{1,3}\.){3}\d{1,3}\b")


class Anonymiser:
    def __init__(self) -> None:
        self.names: dict[str, str] = {}
        self.ips: dict[str, str] = {}

    def learn(self, announcement: str) -> None:
        fields = announcement.split(",")
        if fields and fields[0]:
            self.names.setdefault(
                fields[0], PLACEHOLDER_NAME if not self.names else f"Makera_Z1_{len(self.names):06d}"
            )

    def __call__(self, text: str) -> str:
        for real, fake in self.names.items():
            text = text.replace(real, fake)
        return IPV4.sub(lambda m: self.ips.setdefault(m.group(0), PLACEHOLDER_IPS[len(self.ips)]), text)


def convert(src: str, dst: str) -> int:
    with open(src, encoding="utf-8") as f:
        events = [json.loads(line) for line in f]
    anon = Anonymiser()
    for e in events:
        if e["kind"] == "udp" and e.get("raw"):
            anon.learn(e["raw"])
    t0 = events[0]["t"]
    out = []
    for e in events:
        ms = round((e["t"] - t0) * 1000)
        if e["kind"] == "udp" and e.get("raw"):
            out.append(f"{ms} U {anon(e['raw'])}")
        elif e["kind"] == "status":
            out.append(f"{ms} S {anon(e['msg'].strip())}")
        elif e["kind"] == "result" and not e["msg"].split(" ", 1)[-1].startswith("answered"):
            out.append(f"{ms} F {anon(e['msg'])}")
    with open(dst, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    return len(out)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    print(f"{convert(sys.argv[1], sys.argv[2])} events written to {sys.argv[2]}")
