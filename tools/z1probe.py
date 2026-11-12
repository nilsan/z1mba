#!/usr/bin/env python3
"""z1probe: find out how a Makera Z1 behaves with more than one client.

Standard library only, so it runs on any laptop next to the machine:

    python3 z1probe.py listen                 # UDP 3333 announcements, busy flag changes
    python3 z1probe.py poll 192.168.1.50      # hold one connection, '?' every second
    python3 z1probe.py poll IP --polite       # connect, one '?', disconnect, every 4 s
    python3 z1probe.py second IP              # one connection attempt, report what the Z1 did
    python3 z1probe.py watch IP               # listen + polite poll together (the main test)

Add --log FILE to any command to also write JSON lines, one per event.

The frame layout follows BitCue's notes on the Z1 protocol (journeyonlabs/bitcue,
z1screen/framing.py), written from scratch here:
    86 68 | length (2, BE) | type (1) | payload | CRC-16/XMODEM (2, BE) | 55 AA
length = 1 + len(payload) + 2, and the CRC covers the length, type and payload.
"""

from __future__ import annotations

import argparse
import binascii
import json
import socket
import sys
import threading
import time
from dataclasses import dataclass, field

HEADER = b"\x86\x68"
FOOTER = b"\x55\xaa"
MAX_LEN = 8200

CTRL_SINGLE = 0xA1
CTRL_MULTI = 0xA2
STATUS_RES = 0x81
NORMAL_INFO = 0x90
TYPE_NAMES = {
    0x71: "FIRM_VER",
    0x81: "STATUS_RES",
    0x82: "DIAG_RES",
    0x83: "LOAD_INFO",
    0x84: "LOAD_FINISH",
    0x85: "LOAD_ERROR",
    0x90: "NORMAL_INFO",
    0xA1: "CTRL_SINGLE",
    0xA2: "CTRL_MULTI",
    0xB0: "FILE_START",
    0xB1: "FILE_MD5",
    0xB2: "FILE_VIEW",
    0xB3: "FILE_DATA",
    0xB4: "FILE_END",
    0xB5: "FILE_CAN",
    0xB6: "FILE_RETRY",
}

DISCOVERY_PORT = 3333
MACHINE_PORT = 2222


def crc16(data: bytes) -> int:
    """CRC-16/XMODEM (poly 0x1021, init 0)."""
    return binascii.crc_hqx(data, 0)


def build_frame(ptype: int, payload: bytes) -> bytes:
    body = (1 + len(payload) + 2).to_bytes(2, "big") + bytes([ptype]) + payload
    return HEADER + body + crc16(body).to_bytes(2, "big") + FOOTER


@dataclass
class Frame:
    ptype: int
    payload: bytes
    crc_ok: bool

    @property
    def name(self) -> str:
        return TYPE_NAMES.get(self.ptype, f"0x{self.ptype:02X}")


@dataclass
class FrameParser:
    """Incremental parser. Feed bytes, get whole frames; junk is counted, not raised."""

    buf: bytearray = field(default_factory=bytearray)
    stray: int = 0

    def feed(self, data: bytes) -> list[Frame]:
        self.buf += data
        out: list[Frame] = []
        while True:
            start = self.buf.find(HEADER)
            if start < 0:
                # Keep a trailing 0x86, it may be the first half of a header.
                keep = 1 if self.buf.endswith(HEADER[:1]) else 0
                self.stray += len(self.buf) - keep
                del self.buf[: len(self.buf) - keep]
                return out
            if start:
                self.stray += start
                del self.buf[:start]
            if len(self.buf) < 4:
                return out
            length = int.from_bytes(self.buf[2:4], "big")
            if length < 3 or length > MAX_LEN:
                self.stray += 1
                del self.buf[:1]
                continue
            total = 2 + 2 + length + 2
            if len(self.buf) < total:
                return out
            if self.buf[total - 2 : total] != FOOTER:
                self.stray += 1
                del self.buf[:1]
                continue
            body = bytes(self.buf[2 : 2 + 2 + length - 2])
            crc = int.from_bytes(self.buf[total - 4 : total - 2], "big")
            out.append(Frame(body[2], body[3:], crc16(body) == crc))
            del self.buf[:total]


def parse_status(text: str) -> dict[str, object]:
    """'<Run|MPos:1,2,3|T:3,-1.4,4>' -> {'state': 'Run', 'MPos': ['1','2','3'], 'T': [...]}."""
    text = text.strip()
    text = text.removeprefix("<")
    text = text.removesuffix(">")
    parts = text.split("|")
    out: dict[str, object] = {"state": parts[0]}
    for part in parts[1:]:
        key, sep, value = part.partition(":")
        if sep:
            out[key] = [v.strip() for v in value.split(",")]
    return out


def parse_announcement(data: bytes) -> dict[str, str]:
    fields = data.decode("utf-8", "replace").strip().split(",")
    # The Z1 adds a 5th field, the machine state (community observation; the Carvera sends 4).
    names = ["name", "ip", "port", "busy", "state"]
    out = {k: v for k, v in zip(names, fields)}
    if len(fields) > 5:
        out["extra"] = ",".join(fields[5:])
    return out


# --- output ---------------------------------------------------------------


class Log:
    def __init__(self, path: str | None):
        self.lock = threading.Lock()
        self.file = open(path, "a", encoding="utf-8") if path else None  # noqa: SIM115 (lives as long as the process)

    def __call__(self, kind: str, msg: str, **data: object) -> None:
        now = time.time()
        stamp = time.strftime("%H:%M:%S", time.localtime(now)) + f".{int(now * 1000) % 1000:03d}"
        with self.lock:
            print(f"{stamp} {kind:<8} {msg}", flush=True)
            if self.file:
                rec = {"t": round(now, 3), "kind": kind, "msg": msg, **data}
                self.file.write(json.dumps(rec) + "\n")
                self.file.flush()


# --- commands -------------------------------------------------------------


def listen(log: Log, stop: threading.Event, seconds: float | None = None) -> None:
    """Print each announcement once, then only changes (busy flips, new senders)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    if hasattr(socket, "SO_REUSEPORT"):
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    s.bind(("", DISCOVERY_PORT))
    s.settimeout(0.5)
    last: dict[str, str] = {}
    end = time.monotonic() + seconds if seconds else None
    log("udp", f"listening on UDP {DISCOVERY_PORT}")
    while not stop.is_set() and (end is None or time.monotonic() < end):
        try:
            data, addr = s.recvfrom(1024)
        except TimeoutError:
            continue
        raw = data.decode("utf-8", "replace").strip()
        if last.get(addr[0]) != raw:
            ann = parse_announcement(data)
            log(
                "udp",
                f"{addr[0]}: {raw!r} busy={ann.get('busy')} state={ann.get('state')}",
                src=addr[0],
                raw=raw,
                **ann,
            )
            last[addr[0]] = raw
    s.close()


def _connect(host: str, port: int, timeout: float) -> tuple[socket.socket | None, str, float]:
    t = time.monotonic()
    try:
        sock = socket.create_connection((host, port), timeout=timeout)
        return sock, "connected", time.monotonic() - t
    except ConnectionRefusedError:
        return None, "refused", time.monotonic() - t
    except TimeoutError:
        return None, "connect timeout", time.monotonic() - t
    except OSError as e:
        return None, f"error: {e}", time.monotonic() - t


def _query(sock: socket.socket, plain: bool, wait: float, log: Log) -> str:
    """Send one '?', read until a status arrives or `wait` runs out. Returns the outcome."""
    sock.sendall(b"?" if plain else build_frame(CTRL_SINGLE, b"?"))
    parser = FrameParser()
    text = b""
    end = time.monotonic() + wait
    got_any = False
    while time.monotonic() < end:
        sock.settimeout(max(0.05, end - time.monotonic()))
        try:
            data = sock.recv(4096)
        except TimeoutError:
            break
        except ConnectionResetError:
            return "reset by Z1"
        if not data:
            return "closed by Z1" + (" after replying" if got_any else " without replying")
        got_any = True
        for f in parser.feed(data):
            payload = f.payload.decode("utf-8", "replace")
            if f.ptype == STATUS_RES:
                st = parse_status(payload)
                log("status", payload, state=st.get("state"), crc_ok=f.crc_ok)
                return "answered"
            log("frame", f"{f.name}: {payload[:120]!r}", ptype=f.ptype, crc_ok=f.crc_ok)
        if parser.stray:
            text += data
            for line in text.split(b"\n")[:-1]:
                s = line.decode("utf-8", "replace").strip()
                if s.startswith("<"):
                    log("status", s + " (plain text)", state=parse_status(s).get("state"))
                    return "answered (plain text)"
                if s:
                    log("text", repr(s))
            text = text.split(b"\n")[-1]
    return "accepted, data but no status" if got_any else "accepted, silent"


def second(host: str, port: int, plain: bool, log: Log) -> str:
    sock, how, dt = _connect(host, port, 4.0)
    log("tcp", f"{how} in {dt * 1000:.0f} ms", outcome=how, ms=round(dt * 1000))
    if not sock:
        return how
    try:
        outcome = _query(sock, plain, 5.0, log)
    finally:
        sock.close()
    log("result", outcome, outcome=outcome)
    return outcome


def poll(
    host: str,
    port: int,
    polite: bool,
    interval: float,
    plain: bool,
    log: Log,
    stop: threading.Event,
    count: int | None,
) -> None:
    n = 0
    sock: socket.socket | None = None
    fails = 0
    while not stop.is_set() and (count is None or n < count):
        n += 1
        t = time.monotonic()
        if sock is None:
            sock, how, dt = _connect(host, port, 4.0)
            log("tcp", f"#{n} {how} in {dt * 1000:.0f} ms", n=n, outcome=how, ms=round(dt * 1000))
        if sock is not None:
            outcome = _query(sock, plain, 2.0, log)
            if outcome != "answered" and not outcome.startswith("answered"):
                log("result", f"#{n} {outcome}", n=n, outcome=outcome)
            if polite or outcome.startswith(("closed", "reset")):
                sock.close()
                sock = None
            fails = 0 if outcome.startswith("answered") else fails + 1
        else:
            fails += 1
        # Same back-off shape as BitCue: 3, 5, 7, 9, then 10 s while it fails.
        delay = interval if fails == 0 else min(10.0, 1 + 2 * fails)
        stop.wait(max(0.0, delay - (time.monotonic() - t)))
    if sock:
        sock.close()


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--log", help="also append JSON lines to this file")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("listen", help="show UDP 3333 announcements")
    p.add_argument("--seconds", type=float)

    for name, helptext in (("poll", "poll status over TCP"), ("watch", "listen + polite poll")):
        p = sub.add_parser(name, help=helptext)
        p.add_argument("host")
        p.add_argument("--port", type=int, default=MACHINE_PORT)
        p.add_argument("--plain", action="store_true", help="send a bare '?' instead of a frame")
        p.add_argument("--interval", type=float, help="seconds between polls (default 1, polite 4)")
        p.add_argument("--count", type=int, help="stop after N polls")
        if name == "poll":
            p.add_argument("--polite", action="store_true", help="disconnect after each reply")

    p = sub.add_parser("second", help="one connection attempt, report what happened")
    p.add_argument("host")
    p.add_argument("--port", type=int, default=MACHINE_PORT)
    p.add_argument("--plain", action="store_true")

    a = ap.parse_args(argv)
    log = Log(a.log)
    stop = threading.Event()
    try:
        if a.cmd == "listen":
            listen(log, stop, a.seconds)
        elif a.cmd == "second":
            second(a.host, a.port, a.plain, log)
        else:
            polite = a.cmd == "watch" or a.polite
            interval = a.interval or (4.0 if polite else 1.0)
            if a.cmd == "watch":
                threading.Thread(target=listen, args=(log, stop), daemon=True).start()
            log("start", f"{a.cmd} {a.host}:{a.port} {'polite' if polite else 'hold'} every {interval:g} s")
            poll(a.host, a.port, polite, interval, a.plain, log, stop, a.count)
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
    return 0


if __name__ == "__main__":
    sys.exit(main())
