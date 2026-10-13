# SPDX-License-Identifier: GPL-3.0-only
"""The emulator checks' memory reads at any alignment (tests/emu/session.py mem / word, panel.py, fxkit.py): a fake of
the driver (tests/emu/drv/main.rs) answers a word read off a word boundary with 0xDEADBEEF as the real one does (the
bus faults, read() falls back), so a check that reads a uint8_t array by words fails once the linker puts it at an odd
address. The fx check's dins_amt did after the RAM -> pool moves (2026-10-13): no memory was wrong, the read was.

  python3 tests/emu_mem_align_test.py"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "emu"))
import session as S  # noqa: E402

fails = 0


def check(what, ok, got=""):
    global fails
    print(f"{what:<100} {'ok' if ok else 'FAIL ' + str(got)}")
    fails += not ok


class FakeDriver(S.Emu):
    """an Emu whose cmd() is the driver's peek / peekb over a little RAM image (no process)"""

    def __init__(self, syms, ram, base=0x01C08000):
        self.syms, self.ram, self.base = syms, ram, base

    def _addr(self, s):
        if s.startswith("0x"):
            return int(s, 16)
        name, _, off = s.partition("+")
        return self.syms[name] + int(off or "0", 0)

    def _read(self, a, size):
        if a % size:                                   # (bus.rs check: an unaligned access faults)
            return 0xDEADBEEF
        o = a - self.base
        return int.from_bytes(bytes(self.ram[o:o + size]), "little")

    def cmd(self, c):
        op, at, n = c.split()
        size = {"peek": 4, "peekh": 2, "peekb": 1}[op]
        a = self._addr(at)
        return [" ".join(str(self._read(a + i * size, size)) for i in range(int(n, 0)))]


ram = bytearray(256)
for i in range(32):
    ram[0x26 + i] = i + 1                              # a uint8_t[2][16] at an address 2 past a word boundary
ram[0x47:0x4B] = bytes([3, 1, 0xFF, 2])                # a uint8_t[4] at an odd address
e = FakeDriver({"dins_amt": 0x01C08026, "pat_cur": 0x01C08047, "aligned": 0x01C08040}, ram)

check("the fake driver answers an unaligned word read as the driver does (0xDEADBEEF)",
      e.peek("dins_amt")[0] == 0xDEADBEEF, e.peek("dins_amt"))
check("mem: 32 bytes of a byte array 2 past a word boundary read as they are",
      list(e.mem("dins_amt", 32)) == list(range(1, 33)), list(e.mem("dins_amt", 32)))
check("mem: at an odd address", list(e.mem("pat_cur", 4)) == [3, 1, 0xFF, 2], list(e.mem("pat_cur", 4)))
check("word: a uint8_t[4] at an odd address as one little-endian word", e.word("pat_cur") == 0x02FF0103,
      hex(e.word("pat_cur")))
check("word: an aligned word as peek reads it", e.word("aligned") == e.peek("aligned")[0])

import fxkit  # noqa: E402
import panel  # noqa: E402

fx = fxkit.Fx.__new__(fxkit.Fx)
fx.e = e
d, c = fx.ins()
check("fxkit Fx.ins: the drum sounds' DIST and COMP inserts, not 0xDEADBEEF bytes",
      d == list(range(1, 17)) and c == list(range(17, 33)), (d, c))
p = panel.Panel.__new__(panel.Panel)
p.e = e
check("panel cur(): the tracks' pattern sources at an odd address", p.cur() == [3, 1, 0xFF, 2], p.cur())
print("emu mem alignment test " + ("passed" if not fails else "FAILED"))
sys.exit(1 if fails else 0)
