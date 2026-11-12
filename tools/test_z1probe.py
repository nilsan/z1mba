"""Tests for z1probe. Run: python3 -m unittest discover -s tools"""

from __future__ import annotations

import socket
import threading
import unittest

import z1probe as zp


class Quiet(zp.Log):
    def __init__(self) -> None:
        super().__init__(None)
        self.events: list[tuple[str, str]] = []

    def __call__(self, kind: str, msg: str, **data: object) -> None:
        self.events.append((kind, msg))


class FrameTests(unittest.TestCase):
    def test_crc_check_value(self) -> None:
        self.assertEqual(zp.crc16(b"123456789"), 0x31C3)  # CRC-16/XMODEM check value

    def test_query_frame_layout(self) -> None:
        f = zp.build_frame(zp.CTRL_SINGLE, b"?")
        self.assertEqual(f[:5], bytes([0x86, 0x68, 0x00, 0x04, 0xA1]))
        self.assertEqual(f[5:6], b"?")
        self.assertEqual(f[-2:], b"\x55\xaa")
        self.assertEqual(len(f), 1 + 9)

    def test_round_trip_split_and_junk(self) -> None:
        a = zp.build_frame(zp.STATUS_RES, b"<Idle|T:1,0,-1>")
        b = zp.build_frame(zp.NORMAL_INFO, b"hello\n")
        stream = b"junk\x86" + a + b
        p = zp.FrameParser()
        frames = []
        for i in range(len(stream)):  # one byte at a time
            frames += p.feed(stream[i : i + 1])
        self.assertEqual(
            [(f.ptype, f.payload) for f in frames],
            [(zp.STATUS_RES, b"<Idle|T:1,0,-1>"), (zp.NORMAL_INFO, b"hello\n")],
        )
        self.assertTrue(all(f.crc_ok for f in frames))
        self.assertEqual(p.stray, 5)

    def test_bad_crc_is_flagged(self) -> None:
        f = bytearray(zp.build_frame(zp.STATUS_RES, b"<Run>"))
        f[-3] ^= 0xFF
        (frame,) = zp.FrameParser().feed(bytes(f))
        self.assertFalse(frame.crc_ok)

    def test_bad_footer_resyncs(self) -> None:
        bad = bytearray(zp.build_frame(zp.STATUS_RES, b"<Run>"))
        bad[-1] = 0
        good = zp.build_frame(zp.STATUS_RES, b"<Idle>")
        frames = zp.FrameParser().feed(bytes(bad) + good)
        self.assertEqual([f.payload for f in frames], [b"<Idle>"])


class ParseTests(unittest.TestCase):
    def test_status(self) -> None:
        st = zp.parse_status("<Tool|MPos:1.0,2.0,3.0|T:3,-1.474,4|P:15433,37,6120>")
        self.assertEqual(st["state"], "Tool")
        self.assertEqual(st["T"], ["3", "-1.474", "4"])
        self.assertEqual(st["P"], ["15433", "37", "6120"])

    def test_announcement(self) -> None:
        ann = zp.parse_announcement(b"Makera_Z1,192.168.1.50,2222,1,Run,x")
        self.assertEqual(
            ann,
            {
                "name": "Makera_Z1",
                "ip": "192.168.1.50",
                "port": "2222",
                "busy": "1",
                "state": "Run",
                "extra": "x",
            },
        )
        self.assertNotIn("state", zp.parse_announcement(b"Carvera,10.0.0.2,2222,0"))


class FakeZ1:
    """Localhost stand-in: the first client gets answers, later ones get `second_mode`."""

    def __init__(self, second_mode: str = "close", plain: bool = False) -> None:
        self.srv = socket.create_server(("127.0.0.1", 0))
        self.port = self.srv.getsockname()[1]
        self.second_mode = second_mode
        self.plain = plain
        self.holder: socket.socket | None = None
        threading.Thread(target=self._accept, daemon=True).start()

    def _accept(self) -> None:
        while True:
            try:
                c, _ = self.srv.accept()
            except OSError:
                return
            if self.holder is None:
                self.holder = c
                threading.Thread(target=self._serve, args=(c,), daemon=True).start()
            elif self.second_mode == "close":
                c.close()
            # "silent": leave it open and never answer

    def _serve(self, c: socket.socket) -> None:
        while True:
            try:
                data = c.recv(1024)
            except OSError:
                data = b""
            if not data:
                self.holder = None
                c.close()
                return
            if self.plain:
                c.sendall(b"<Idle|T:1,0,-1>\n")
            else:
                c.sendall(zp.build_frame(zp.STATUS_RES, b"<Idle|T:1,0,-1>"))

    def close(self) -> None:
        self.srv.close()


class NetworkTests(unittest.TestCase):
    def test_second_answered_when_free(self) -> None:
        z1 = FakeZ1()
        self.addCleanup(z1.close)
        self.assertEqual(zp.second("127.0.0.1", z1.port, False, Quiet()), "answered")

    def test_plain_text_reply(self) -> None:
        z1 = FakeZ1(plain=True)
        self.addCleanup(z1.close)
        self.assertEqual(zp.second("127.0.0.1", z1.port, True, Quiet()), "answered (plain text)")

    def test_second_client_closed(self) -> None:
        z1 = FakeZ1("close")
        self.addCleanup(z1.close)
        holder = socket.create_connection(("127.0.0.1", z1.port))
        self.addCleanup(holder.close)
        holder.sendall(b"?")
        holder.recv(100)
        self.assertTrue(zp.second("127.0.0.1", z1.port, False, Quiet()).startswith(("closed", "reset")))

    def test_second_client_silent(self) -> None:
        z1 = FakeZ1("silent")
        self.addCleanup(z1.close)
        holder = socket.create_connection(("127.0.0.1", z1.port))
        self.addCleanup(holder.close)
        holder.sendall(b"?")
        holder.recv(100)
        self.assertEqual(zp.second("127.0.0.1", z1.port, False, Quiet()), "accepted, silent")

    def test_refused(self) -> None:
        s = socket.create_server(("127.0.0.1", 0))
        port = s.getsockname()[1]
        s.close()
        self.assertEqual(zp.second("127.0.0.1", port, False, Quiet()), "refused")

    def test_polite_poll_releases_connection(self) -> None:
        z1 = FakeZ1("close")
        self.addCleanup(z1.close)
        log = Quiet()
        zp.poll("127.0.0.1", z1.port, True, 0.05, False, log, threading.Event(), 3)
        self.assertEqual(sum(1 for k, _ in log.events if k == "status"), 3)
        self.assertEqual(sum(1 for k, m in log.events if k == "tcp" and "connected" in m), 3)


if __name__ == "__main__":
    unittest.main()
