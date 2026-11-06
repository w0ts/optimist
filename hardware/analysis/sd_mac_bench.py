#!/usr/bin/env python3
"""The FM-1's streaming pattern on the Mac: what the card's own waits are, away from the FM-1's clock and bus.

  python3 hardware/analysis/sd_mac_bench.py "/Volumes/CARD/Optimist/Samples/Kits/Acoustic"

Reads only: files are opened read-only, the Mac's cache is off for them (F_NOCACHE). The rows are the ones of the console's
sd ceil: runs of k blocks taken in turn from 6 files, then the same from one file. "aligned" is against the file's start, since
the Mac does not give the card's block number here. The reader adds its own USB time and the card runs 4 bits wide, so compare
rows, and the first read's wait against the FM-1's, not KB/s against the FM-1's.
"""
import argparse
import fcntl
import os
import statistics
import sys
import time

BLOCK = 512
F_NOCACHE = 48                         # sys/fcntl.h
FILES = 6
MIN_BLOCKS = 128                       # as sd ceil: smaller files are skipped
RUNS = (1, 2, 4, 8, 16, 32, 64)
ALIGNED = (8, 16, 32)
SECONDS = 1.5


def open_files(folder):
    names = sorted(n for n in os.listdir(folder) if not n.startswith("."))
    out = []
    for n in names:
        path = os.path.join(folder, n)
        if os.path.isfile(path) and os.path.getsize(path) // BLOCK >= MIN_BLOCKS:
            fd = os.open(path, os.O_RDONLY)
            fcntl.fcntl(fd, F_NOCACHE, 1)
            out.append((fd, os.path.getsize(path) // BLOCK))
    return out[:FILES]


def run_row(files, k, aligned):
    offs = [0] * len(files)
    waits, blocks, turn = [], 0, 0
    t_end = time.perf_counter() + SECONDS
    t_start = time.perf_counter()
    while time.perf_counter() < t_end:
        i = turn % len(files)
        turn += 1
        fd, size = files[i]
        s = offs[i]
        if aligned:
            s = (s + k - 1) // k * k
        if s >= size or (aligned and s + k > size):
            offs[i] = 0
            continue
        n = min(k, size - s)
        t = time.perf_counter()
        if len(os.pread(fd, n * BLOCK, s * BLOCK)) != n * BLOCK:
            raise SystemExit("short read")
        waits.append((time.perf_counter() - t) * 1e6)
        blocks += n
        offs[i] = s + n
    secs = time.perf_counter() - t_start
    return blocks * BLOCK / 1000 / secs, waits


def show(places, k, aligned, kbs, waits):
    q = statistics.quantiles(waits, n=20) if len(waits) > 1 else [waits[0]] * 19
    kind = "run aligned" if aligned else "run"
    print(f"places {places} {kind} {k}: KB/s {kbs:.0f} runs {len(waits)} | "
          f"run us median {statistics.median(waits):.0f} p95 {q[18]:.0f} max {max(waits):.0f}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folder", help="a folder of samples on the mounted card")
    a = ap.parse_args()
    if not os.path.isdir(a.folder):
        sys.exit(f"sd_mac_bench: no folder {a.folder}")
    files = open_files(a.folder)
    if not files:
        sys.exit("sd_mac_bench: no file of 64 KB or more in that folder")
    for k in RUNS:
        show(len(files), k, False, *run_row(files, k, False))
    for k in ALIGNED:
        show(len(files), k, True, *run_row(files, k, True))
    for k in (8, 64):
        show(1, k, False, *run_row(files[:1], k, False))


if __name__ == "__main__":
    main()
