#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The AA token (opti_miraa_1) in Python, on 10 pages captured from the FM-1 (ios/OptimistMirror/Tests/Fixtures/mirror_pages.sysex,
fm1-base's anti-aliased text): tools/fm1_mirror.py's decoder takes the pages apart into their writes; tools/mirror_enc.py encodes
them without the token (the FM-1's very bytes) and with it (the firmware's very bytes: mirror_pages_aa.sysex, written by
tests/mirror_pages_test.c), and the decoder gives the same pixels back, page by page; the bytes per page; INFO's name read.
  python3 tests/mirror_aa_test.py"""
import importlib.util
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FIX = ROOT / "ios" / "OptimistMirror" / "Tests" / "Fixtures"
PAGES = ["global", "home_mix", "master_fx", "project", "scope", "song", "sound_edit", "sound_fx", "steps", "system"]


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


fm = load("fm1_mirror", ROOT / "tools" / "fm1_mirror.py")
me = load("mirror_enc", ROOT / "tools" / "mirror_enc.py")
fails = 0


def check(ok: bool, what: str) -> None:
    global fails
    print(f"mirror_aa: {what:96s} {'ok' if ok else 'FAIL'}")
    fails += not ok


def is_whole(f: list[int]) -> bool:
    return f[5] == 0 and f[6:14] == [0, 0, 0, 0, 112, 1, 112, 1]


def pages_of(frames: list[list[int]]) -> tuple[list[list[int]], list[tuple], int]:
    """each page's screen (the frames decoded), every write (fill or blit with its pixels, its page), the frames not decoded"""
    dec, shots, writes, bad = fm.Decoder(), [], [], 0
    cur = None

    def take() -> None:
        if cur is not None:
            x, y, w, h, page = cur
            writes.append(("blit", x, y, w, h, [dec.fb[(y + j) * fm.W + x + i] for j in range(h) for i in range(w)], page))

    for f in frames:
        if f[5] <= 1:
            take()
            cur = None
            if is_whole(f) and writes:
                shots.append(list(dec.fb))
            page = len(shots)
            v = [f[k] | f[k + 1] << 7 for k in (6, 8, 10, 12)]
            if f[5] == 0:
                writes.append(("fill", *v, f[14] | f[15] << 7 | f[16] << 14, page))
            else:
                cur = (*v, page)
        bad += not dec.frame(f)
    take()
    shots.append(list(dec.fb))
    return shots, writes, bad


def encode(writes: list[tuple], aa: bool) -> tuple[bytes, list[int]]:
    enc, out, per = me.Encoder(aa), bytearray(), [0] * len(PAGES)
    for kind, x, y, w, h, arg, page in writes:
        b = enc.fill(x, y, w, h, arg) if kind == "fill" else enc.blit(x, y, w, h, arg)
        out += b
        per[page] += len(b)
    return bytes(out), per


def main() -> None:
    raw = (FIX / "mirror_pages.sysex").read_bytes()
    want_aa = (FIX / "mirror_pages_aa.sysex").read_bytes()
    shots, writes, bad = pages_of(fm.frames_of(raw))
    check(len(shots) == len(PAGES) and not bad, f"the fixture: {len(shots)} pages, {len(writes)} writes, {bad} frames not decoded")
    old, per_old = encode(writes, False)
    check(old == raw, f"without the token: the FM-1's bytes ({len(old)} of {len(raw)} B)")
    new, per_new = encode(writes, True)
    check(new == want_aa, f"with the token: the firmware's bytes (mirror_pages_test.c; {len(new)} of {len(want_aa)} B)")
    got, _, bad = pages_of(fm.frames_of(want_aa))
    diff = [sum(a != b for a, b in zip(g, s)) for g, s in zip(got, shots)]
    check(len(got) == len(shots) and not bad and not any(diff), f"the AA stream decoded: the same pixels on every page ({diff})")
    for p, name in enumerate(PAGES):
        print(f"  {name:10s} {per_old[p]:6d} B -> {per_new[p]:6d} B  {100 * per_new[p] / per_old[p]:5.1f} %")
    print(f"  {'mean':10s} {sum(per_old) / 1e3 / len(PAGES):.2f} KB -> {sum(per_new) / 1e3 / len(PAGES):.2f} KB")
    dec = fm.Decoder()
    frame = [0xF0, 0x7D, 0x46, 0x4C, 106, 1, 0, 0, 0, 0, 3, 0, 1, 0, 14 << 3 | 7, 0, 112, 3, 0 << 3 | 7, 3, 0b0101101, 0b1000000,
             0xF7]                          # (3 px: a new colour 0xF800 over slot 0 black at a = 15, 0, 6: '0' '10' '110110')
    ok = dec.frame(frame)
    check(ok and dec.fb[:3] == [0xF800, 0, me.blend(0xF800, 0, 6)], f"a token by hand: {[hex(c) for c in dec.fb[:3]]}")
    bad_k = frame[:18] + [7 << 3 | 7] + frame[19:]
    check(not fm.Decoder().frame(bad_k), "a ground slot of 7 refused")
    check(fm.takes_aa(list(b"\x67\x0eopti_miraa_1 ")) and not fm.takes_aa(list(b"\x67\x0eopti_miraa_12 "))
          and not fm.takes_aa(list(b"xopti_miraa_1 ")) and not fm.takes_aa(None),
          "INFO: opti_miraa_1 read from tag 67, not from a longer name")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
