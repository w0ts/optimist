#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""tools/fm1_mirror.py's decoder and PNG against the device's encoder: tests/mirror_test.c's fixture (a panel drawn whole,
its frames and its pixels) decoded to the same pixels, written as a PNG that reads back to them; STATE in words.
  python3 tests/fm1_mirror_test.py MIRROR_TEST_BINARY"""
import importlib.util
import os
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

_SPEC = importlib.util.spec_from_file_location("fm1_mirror", Path(__file__).resolve().parent.parent / "tools" / "fm1_mirror.py")
fm = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(fm)

fails = 0


def check(ok: bool, what: str) -> None:
    global fails
    print(f"fm1_mirror: {what:72s} {'ok' if ok else 'FAIL'}")
    fails += not ok


def png_pixels(path: Path) -> list[tuple[int, int, int]]:
    data, i, idat = path.read_bytes(), 8, b""
    while i < len(data):
        n = int.from_bytes(data[i:i + 4], "big")
        if data[i + 4:i + 8] == b"IDAT":
            idat += data[i + 8:i + 8 + n]
        i += 12 + n
    raw, px = zlib.decompress(idat), []
    for y in range(fm.H):
        row = raw[y * (1 + 3 * fm.W) + 1:(y + 1) * (1 + 3 * fm.W)]
        px += [tuple(row[3 * x:3 * x + 3]) for x in range(fm.W)]
    return px


def main() -> None:
    with tempfile.TemporaryDirectory() as d:
        base = Path(d) / "fix"
        env = dict(os.environ, MIRROR_FIXTURE=str(base))
        subprocess.run([sys.argv[1]], env=env, check=True, stdout=subprocess.DEVNULL)
        frames = fm.frames_of(Path(f"{base}.sysex").read_bytes())
        want = Path(f"{base}.rgb565").read_bytes()
        want = [want[2 * k] | want[2 * k + 1] << 8 for k in range(fm.W * fm.H)]
        dec = fm.Decoder()
        bad = sum(not dec.frame(f) for f in frames)
        check(bad == 0 and dec.fb == want, f"{len(frames)} frames decoded, the fixture's pixels (RGB565)")
        png = Path(d) / "shot.png"
        fm.write_png(png, dec.fb)
        px = png_pixels(png)
        c = dec.fb[120 * fm.W + 120]
        r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
        check(px[120 * fm.W + 120] == ((r * 255 + 15) // 31, (g * 255 + 31) // 63, (b * 255 + 15) // 31),
              "the PNG reads back to the decoded pixels")
    s = [0] * 29
    s[0] = 1 << 3                              # held: LFO (bit 3)
    s[6], s[8] = 1 << 3, 1 << 0                # lit: LFO, bit 14 (PLAY's green lamp: byte 2 bit 0)
    s[19:26] = [64, 64, 64, 66, 64, 64, 64]    # KNOB 1 +2
    s[26], s[27], s[28] = 700 & 127, 700 >> 7, 3
    t = fm.state_text(s)
    check("held:  LFO" in t and "PLAY(green)" in t and "KNOB 1 +2" in t and "MASTER pot 700 (the client sets it)" in t
          and "a test session" in t, "STATE in words")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
