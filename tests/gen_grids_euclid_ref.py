#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2012 Emilie Gillet (Mutable Instruments): Flatten and EuclidianPattern as in
# pichenettes/eurorack grids/resources/lookup_tables.py, its table loop too; Python 3 here (range for xrange, and
# Python 2's round, halves away from zero, for round).
"""Grids' Euclidean table (lut_res_euclidean) as the reference of tests/gen_test.c (seq/gen.c gr_eu_mask): one line a
length (1..32), the 32 fills' masks in hex, step j -> bit j.
  tests/gen_grids_euclid_ref.py [OUT]   (default tests/grids_euclid_ref.txt)"""
import math
import sys
from pathlib import Path


def flatten(items):
    if hasattr(items, "pop"):
        for item in items:
            yield from flatten(item)
    else:
        yield items


def euclidian_pattern(k, n):
    pattern = [[1]] * k + [[0]] * (n - k)
    while k:
        cut = min(k, len(pattern) - k)
        k, pattern = cut, [pattern[i] + pattern[k + i] for i in range(cut)] + pattern[cut:k] + pattern[k + cut:]
    return pattern


def py2_round(x: float) -> int:
    return int(math.floor(x + 0.5)) if x >= 0 else -int(math.floor(-x + 0.5))


def main(out: Path) -> None:
    lines = ["# Grids' Euclidean table (tests/gen_grids_euclid_ref.py from grids/resources/lookup_tables.py): a line a "
             "length 1..32, the 32 fills' masks (step j -> bit j)"]
    for num_steps in range(1, 33):
        row = []
        for fill in range(32):
            num_notes = py2_round(float(fill) / 31 * num_steps)
            mask = 0
            for i, bit in enumerate(flatten(euclidian_pattern(num_notes, num_steps))):
                if bit:
                    mask |= 1 << i
            row.append(f"{mask:08x}")
        lines.append(" ".join(row))
    out.write_text("\n".join(lines) + "\n")
    print(f"grids euclid: {out}")


if __name__ == "__main__":
    main(Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent / "grids_euclid_ref.txt")
