#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""tools/ble_vm.py and tools/ble_rf_capture.py without the emulator: the VM format (docs/BLE-HW-FACTS.md §14) on the
V15 extract of docs/ble-traces/v15-vm-trim-map.txt (an emulator result, copied in tests/ble_vm_test.c too), a perturbed
image, and the capture's cut on synthetic traces: BBP window triples, LUT words with their kick, the trim fields of
§14.5 / §16.4 (and a change no field explains stopping it), the VCO scan replaced, the read-back loop left out, a
repeated BBP block replayed, and the program expanding back to the writes it came from. Also: a firmware that is not
stock V15 is refused. The real capture is checked by the tool itself (its self-checks and pinned hash) and end to end
by tests/ble_emu_test.py."""
import hashlib
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import ble_vm  # noqa: E402
import ble_rf_capture as C  # noqa: E402

fails = 0


def check(what, ok, detail=""):
    global fails
    print(f"{what:96s} {'ok' if ok else 'FAIL'}")
    if not ok:
        fails += 1
        if detail:
            print("    " + str(detail)[:2000])


V15_A = bytes.fromhex(                                        # 0x093000 .. 0x0930AF
    "55 aa aa 55 91 6a 20 00 0b 0b 2c 6b 70 00 01 07 04 07 0b 01 07 ea bb 40 04 00 00 00 00 00 00 00"
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 40 40 00 00 00 00 00 00 00 00 00 00 00 00 00"
    "00 ff ff ff ff ff ff ff ff 00 02 00 02 00 02 00 02 00 02 00 02 00 02 00 02 35 1d 00 00 00 6c 40"
    "01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 97 71 e0 00 06 1a 00 00 ff ff ff"
    "ff ff ff ff ff ff ff cc 6d 20 02 a1 55 5b 5b 78 78 a6 a6 60 60 60 fe fe fb fb 29 29 37 02 02 ac"
    "ac e4 e4 30 30 30 27 27 56 56 c8 c8 ca ff ff ff")


def vm_tests():
    check("CRC-16/XMODEM check value 0x31C3", ble_vm.crc16_xmodem(b"123456789") == 0x31C3)
    img = V15_A + b"\xff" * (2 * ble_vm.AREA_SIZE - len(V15_A))
    vm = ble_vm.read(img, ble_vm.VM_BASE)
    ids = [r[1] for r in vm["records"]]
    check("V15 extract: area A, records 106 107 187 108 113 109, the log ends at +0xAD",
          vm["area"] == ble_vm.VM_BASE and ids == [106, 107, 187, 108, 113, 109] and vm["end"] == 0xAD, (vm["area"], ids))
    check("V15 extract: 106 = 0B 0B, 187's inner CRC holds", vm["latest"][106] == b"\x0b\x0b" and
          ble_vm.rec187_ok(vm["latest"][187]))
    flash = bytearray(b"\xff" * 0x100000)
    flash[ble_vm.VM_BASE:ble_vm.VM_BASE + len(img)] = img
    new187 = ble_vm.rec187(bytes(range(64)))
    out = ble_vm.rewrite(bytes(flash), ble_vm.VM_BASE, {106: b"\x01\x02", 187: new187})
    v2 = ble_vm.read(out)
    check("a rewrite in place keeps the log valid, the new data read back, 187 consistent",
          v2["latest"][106] == b"\x01\x02" and v2["latest"][187] == new187 and len(v2["records"]) == 6)
    area = ble_vm.build_area([(106, b"\x03\x04"), (106, b"\x05\x06")])
    v3 = ble_vm.read(area + b"\xff" * ble_vm.AREA_SIZE, ble_vm.VM_BASE)
    check("build_area: two 106, the later wins", v3["latest"][106] == b"\x05\x06")
    bad = bytearray(area)
    bad[4] ^= 1
    check("a bad check byte ends the log at once", ble_vm.read(bytes(bad) + b"\xff" * ble_vm.AREA_SIZE,
                                                               ble_vm.VM_BASE)["records"] == [])
    lines = "".join(f"{ble_vm.VM_BASE + i:06X}: " + " ".join(f"{b:02X}" for b in img[i:i + 16]) + "\r\n"
                    for i in range(0, len(img), 16))
    dimg, got = ble_vm.parse_dump("blevmdump\r\n" + lines + "end\r\n> ")
    check("parse_dump: a 'blevmdump' log gives the 16 KiB back, every byte", dimg == img and got == len(img))
    rep = ble_vm.report(dimg, ble_vm.VM_BASE)
    check("report: the live area, 106 = 0b 0b, the set complete (187's CRC ok)",
          "live area 093000" in rep and "RF 106: len 2  0b 0b" in rep and "187 inner CRC: ok" in rep and
          "the RF set is complete" in rep, rep)
    part, got = ble_vm.parse_dump(lines.splitlines()[0] + "\nflr 0x93010 16\n" + lines.splitlines()[1])
    check("parse_dump: two flr lines give 32 bytes, the rest FF", got == 32 and part[:32] == img[:32] and
          part[32:] == b"\xff" * (len(img) - 32))
    with tempfile.TemporaryDirectory() as d:
        f = Path(d) / "log.txt"
        f.write_text(lines)
        check("tools/ble_vm.py LOG: exit 0", ble_vm.main([str(f)]) == 0)
    check("only B marked: B is live", ble_vm.read(b"\xff" * ble_vm.AREA_SIZE + area, ble_vm.VM_BASE)["area"] ==
          ble_vm.VM_BASE + ble_vm.AREA_SIZE)


# ---- synthetic traces: (step, tick, address, value) writes, as C.writes() returns them
def bbp(reg, d, rd=0, flags=0x80000):
    c = flags | rd << 16 | reg << 8 | d
    return [(C.BBP_PORT, c), (C.BBP_PORT, c | C.START)]


def win_w(w, e, v):
    a, d, c, _ = C.WIN_REGS[w]
    return bbp(a, e) + bbp(d, v) + bbp(c, 1)


def win_r(w, e):
    a, _, c, r = C.WIN_REGS[w]
    return bbp(a, e) + bbp(c, 2) + bbp(r, 0, 1)


def lut(cmd, e, data):
    c = 0x10000 | e << 8 | cmd
    return [(C.SPI_DATA, data), (C.SPI_CTL, c)] + [(C.SPI_CTL, c | 0x10)] * 5 + [(C.SPI_CTL, c)] * 5


def scan_step(band):
    return [(0x11938, band << 19 | 0x50000), (0x11938, band << 19 | 0x40000), (0x11934, 0), (0x11938, band << 19 | 0x50000),
            (0x11934, 1 << 24), (0x11938, band << 19 | 0x50000), (0x11968, 1 << 28)] + [(0x11978, 1)] * 8 + [(0x11978, 0)]


def boot(t):
    """a tiny boot 2 with the trims t ([106, 107, 108, 187] data): every group kind once"""
    ws = [(0x10010, 0x400)]                                   # a clock word: left out
    ws += [(0x11900, 0x1234 | 0x4000), (0x30F00, 7)]
    block = [x for e in range(0, 300) for x in win_w(0, e & 0x7F, e & 0xFF)] + bbp(0x15, 3)
    ws += block
    ws += [(0x11930, (t[0][0] & 15) << 13 | (t[0][1] & 15) << 19 | 1)]   # VM 106 (§14.5)
    ws += [(0x11940, 0x9B41490), (0x1193C, 0x932)] + [x for b in (31, 47, 55, 59, 61) for x in scan_step(b)]
    ws += [(0x1193C, 0xF32), (0x11938, 0x81B7D033)]           # stock's final band: constants after the scan
    ws += win_r(2, 0x61) + win_w(2, 0x61, t[3][0] & 3)         # VM 187 byte 0 by read-modify-write (§16.4)
    ws += win_w(2, 0x04, (t[3][1] & 3) << 6)                  # VM 187 byte 1
    ws += [(0x11908, 0x9C010010 | (t[3][32] & 3) << 7 | (t[3][33] & 1) << 15)]
    ws += block                                               # the reload: the same block
    ws += [x for i in range(80) for x in win_r(2, 0x5C) + win_w(2, 0x5C, 0x10)]   # the read-back loop
    ws += [x for e in range(4) for x in lut(0xE, e, 0x1000 + e)] + lut(0xD, 0xE0, 0x2000 + t[1][0])
    ws += win_w(2, 0x0B, t[2][0] or 0x20)                     # VM 108 byte 0: non-zero replaces the default
    ws += [(0x14000, 0x80), (0x2FD98, 0)] + [(0x2FD9C, i) for i in range(128)]
    return [(i, 100 * i, a, v) for i, (a, v) in enumerate(ws)]


def capture_tests():
    t = [bytes((11, 11)), bytes((1, 7, 4, 7, 11, 1, 7)), bytes(20), ble_vm.rec187(bytes(64))]
    p = [bytes(b ^ 3 for b in d) for d in t]
    p[3] = ble_vm.rec187(p[3][:64])
    part, agc = C.cut(boot(t))
    part_p, _ = C.cut(boot(p))
    check("cut: the clock word left out, the end at 0x14000, the 128 AGC words", part[0][2] == 0x11900 and
          agc == list(range(128)))
    toks, flags = C.tokens(part)
    ops = C.fold(toks)
    kinds = {o[0] for _, o in ops}
    check("tokens / fold: window writes and reads, a direct BBP write, LUT words, register writes, flags 0x80000",
          kinds == {"m", "ww", "wr", "bw", "lut"} and flags == 0x80000, kinds)
    prog, addrs, notes, fl, counts, want = C.build_program(part, part_p, agc, t, p)
    check("the program: one scan op, one replay, the read-back loop left out (80 reads, 80 writes)",
          prog.count(bytes((C.OP_SCAN,))) >= 1 and counts["replay"] == 1 and notes["skipped"] == 3 * 160,
          (counts, notes))
    check("trim marks: 106 x2, 187 bytes 0 (read-modify-write), 1, 32, 33, 108 byte 0", counts["trim"] == 7, counts)
    check("one trim-dependent LUT word (107 -> entry 0xE0) counted, written as captured", counts["lut_trim"] == 1)
    check("section markers: the §16.1 groups by register pattern, in order (2 3 4 5 7 8 10 11 13)",
          notes["sections"] == [2, 3, 4, 5, 7, 8, 10, 11, 13] and counts["sect"] == 9, notes["sections"])
    exp = C.masked(C.expand(prog, addrs, t, fl))
    check("expand(program, trims) = the trace's writes (scan and loop as markers)", exp == C.masked(want))
    t2 = [bytes((5, 9)), t[1], bytes([0x33] + [0] * 19), ble_vm.rec187(bytes([2, 1] + [0] * 30 + [3, 1] + [0] * 30))]
    got = dict((a, v) for a, v in C.expand(prog, addrs, t2, fl) if isinstance(a, int) and a != C.BBP_PORT)
    check("other trims land in their fields: 0x11930 [16:13] / [22:19], 0x11908 [8:7] and 15",
          got[0x11930] == 5 << 13 | 9 << 19 | 1 and got[0x11908] == 0x9C010010 | 3 << 7 | 1 << 15, hex(got[0x11930]))
    bad = list(part_p)
    k = next(i for i, w in enumerate(bad) if w[2] == 0x30F00)
    bad[k] = bad[k][:3] + (8,)
    try:
        C.build_program(part, bad, agc, t, p)
        check("a trim-dependent write no field explains stops the tool", False)
    except C.CaptureError as e:
        check("a trim-dependent write no field explains stops the tool", "no field" in str(e), e)


def input_tests():
    with tempfile.TemporaryDirectory() as d:
        f = Path(d) / "FM-1.fwsc"
        f.write_bytes(b"not the stock firmware")
        try:
            C.find_stock(str(f))
            check("a package that is not stock V15 is refused (SHA-256)", False)
        except C.CaptureError as e:
            check("a package that is not stock V15 is refused (SHA-256)", "not stock V15" in str(e))
    check("the pinned fingerprint is a SHA-256", C.PINNED and len(C.PINNED) == 64 and
          int(C.PINNED, 16) >= 0 and hashlib.sha256(b"").hexdigest() != C.PINNED)


vm_tests()
capture_tests()
input_tests()
print("BLE RF CAPTURE: all passed" if not fails else f"BLE RF CAPTURE: {fails} FAILED")
sys.exit(1 if fails else 0)
