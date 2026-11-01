#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Two runs of the Optimist UI's host test (tests/ui_optimist_test.c) with UI_FRAME_HASHES=FILE compared frame by frame:
a drawing refactor (CANVAS_STRIP's passes) keeps every pixel of every frame. Prints the checks whose frames differ (the
frames since the check before it, up to it) and exits 1 when any do.

usage: frame_hashes_diff.py A B"""
import re
import sys


def segments(path: str) -> list[tuple[str, list[str]]]:
    """[(check, [hash of each frame before it, since the check before])]; the frames after the last check: '(the end)'"""
    out: list[tuple[str, list[str]]] = []
    frames: list[str] = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("# "):
                out.append((line[2:].rstrip("\n"), frames))
                frames = []
            elif line.strip():
                frames.append(line.split()[1])
    out.append(("(the end)", frames))
    return out


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    a, b = segments(sys.argv[1]), segments(sys.argv[2])
    na, nb = sum(len(f) for _, f in a), sum(len(f) for _, f in b)
    if [re.sub(r"[0-9]+", "N", c) for c, _ in a] != [re.sub(r"[0-9]+", "N", c) for c, _ in b] or na != nb:   # (a check's numbers may differ)
        print(f"frame hashes: the runs differ in shape ({len(a)} / {len(b)} checks, {na} / {nb} frames)")
        return 1
    bad = [(c, sum(x != y for x, y in zip(fa, fb)), len(fa)) for (c, fa), (_, fb) in zip(a, b) if fa != fb]
    for c, n, t in bad:
        print(f"  differ: {n} of {t} frames before: {c}")
    print(f"frame hashes: {na} frames, {len(a)} checks; {len(bad)} checks with frames that differ")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
