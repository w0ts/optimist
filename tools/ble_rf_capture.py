#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Capture the radio's start-up tables for a BLE build (FELUCCA_BLE=1) from the user's own stock V15 firmware running in
the FM-1 emulator. The result is committed as firmware/hal/ble_rf_tables_v15.h (a build with neither FM1_STOCK_FWSC nor
a local cache uses it). With the firmware given, the tool keeps a generated, git-ignored local cache: config/ble/
ble_rf_tables.h (with ble_rf_tables.json: the V15 SHA-256 and this tool's format version; the emulator's side outputs
in ble_rf_capture/). tools/build.py copies the tables into build/gen for each BLE build and calls ensure(): a cache that
matches is used as it is, the capture runs again only when it is missing or stale (another format or table hash) and
FM1_STOCK_FWSC names a firmware file, or when the cache was made from another file than the one FM1_STOCK_FWSC names.

  tools/ble_rf_capture.py [--stock FM-1.fwsc] [--diagnose PATH] [--out config/ble/ble_rf_tables.h] [--keep DIR]

Why (docs/BLE-HW-FACTS.md §17, branch feat/ble-facts d907ce3): the RF start-up of the AC791N (§16) writes some 34,000
register words, most of them constant tables (the BBP / MAC load, the Wi-Fi analog init, the RF-die LUT, the AGC
table). The repository carries none of them. This tool observes the stock firmware the user owns writing them in the
emulator, on the user's machine, and the build compiles them in. Nothing it writes is committed (config/ble/ and
build/ are git-ignored).

Inputs
  stock V15   --stock, else $FM1_STOCK_FWSC, else firmwares/FM-1.fwsc. Refused unless its SHA-256 is stock V15's
              (tools/fm1_rescue.py STOCK_SHA256). Never downloaded.
  emulator    --diagnose, else $FM1_BLE_DIAGNOSE, else $FM1_EMU/rust-emulator/target/release/examples/diagnose
              (FM1_EMU default ~/GitHub/fm1-emulator-ble), as tests/ble_emu_test.py finds it. It needs the trace
              variables of the emulator's README ("Tracing and experiment variables"): FM1_MMIO_TRACE,
              FM1_MMIO_RANGES, FM1_FLASH_DUMP, FM1_FLASH_RESTORE.

Method (§17.1, the cut by register patterns only, never by the stock firmware's code addresses)
  1. boot 1 on a fresh flash (V15 calibrates and writes its VM, §14), the flash saved;
  2. boot 2 over it (the stored trims present: the path our rf_init follows, §16.1), every write to the RF ranges
     traced, from the first one up to the BT block (the first write to 0x14000, §5.1 step 3);
  3. boot 2 again with every byte of VM 106 / 107 / 108 / 187 xor 03 (187's inner CRC and the check bytes fixed, §19.2):
     the writes that change are the trim-carrying ones; each is mapped to its field of §14.5 / §16.4 (a change no
     field explains stops the tool);
  4. the boot-2 writes become a small program for the firmware (hal/fm1_ble_rf.h): register writes, BBP window
     transactions (§16.3), RF-die LUT words (§5.2), delays, one VCO-scan step in place of the stock scan (§16.1
     group 7: the firmware runs its own), the window-D read-modify-write loop left out (§16.2: TODO(hardware)), the
     trim fields marked, and a repeated BBP block (the reload, §16.1 group 10) as a replay of the first;
  5. self-checks: the program, expanded with boot 2's own VM, gives exactly boot 2's writes outside the scan and the
     left-out loop, and with the perturbed VM the perturbed run's; then the SHA-256 of the result must be the pinned
     one below (a hash is not the table): a mismatch means the emulator or its trace changed, and the tool stops.

Outputs: the header, and next to it ble_rf_capture/ (the emulator's own VM from boot 1 and the program's expected
writes, for tests/ble_emu_test.py's rf_init check). The VM values there are the emulator's calibration results, not an
FM-1's (§16.4)."""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import ble_vm                          # (tools/: the script's own folder, on sys.path when run or imported from it)
from fm1_rescue import STOCK_SHA256

ROOT = Path(__file__).resolve().parents[1]

FORMAT = 2                              # the header's format (hal/fm1_ble_rf.h checks it)
# SHA-256 of the extracted data (program, addresses, AGC, fields) as this tool made it from stock V15 in fm1-emulator
# feat/ble-engine 6531e20 on 2026-10-08 (two runs identical). Not the table: only its fingerprint.
PINNED = "30ba97eac84ae1b5574c6328a2ab67df7a0c97720b7276b3dcb13cb0e49dcba3"
RANGES = "10000-100ff,11900-1197f,14000-14067,2fc00-2fdff,30000-31fff"   # §17.1 step 2
BOOT1_STEPS = 1_000_000_000              # V15 stores 187 about 1 s into a first boot (§5.3); 2.8 s emulated
BOOT2_STEPS = 60_000_000                 # the BT block starts near 1.3e7 instructions into boot 2
TICKS_PER_US = 24                        # the emulator's guest tick (24 MHz)
DELAY_MIN_US = 20                        # a gap between two writes at least this long becomes a delay
CACHE = ROOT / "config" / "ble"          # the local, git-ignored cache: survives a clean of build/
OUT = CACHE / "ble_rf_tables.h"
COMMITTED = ROOT / "firmware" / "hal" / "ble_rf_tables_v15.h"   # tracked: the same tables, for a build without the V15
META = "ble_rf_tables.json"              # next to the header: {"stock_sha256", "format", "tables_sha256"}
SIDE = "ble_rf_capture"                  # next to the header: vm_emu.bin, expected.txt (tests/ble_emu_test.py)

# ---- the BBP port and its windows (§5.2, §16.3)
BBP_PORT = 0x3101C
START, READ = 1 << 17, 1 << 16
WIN_ADDR = {0xC9: 0, 0xCD: 1, 0xD1: 2, 0xD5: 3}
WIN_DATA = {0xCA: 0, 0xCE: 1, 0xD2: 2, 0xD6: 3}
WIN_COMMIT = {0xC8: 0, 0xCC: 1, 0xD0: 2, 0xD4: 3}
WIN_RB = {0xCB: 0, 0xCF: 1, 0xD3: 2, 0xD7: 3}
WIN_REGS = {w: (a, d, c, r) for (a, w), (d, _), (c, _), (r, _) in
            zip(sorted(WIN_ADDR.items(), key=lambda x: x[1]), sorted(WIN_DATA.items(), key=lambda x: x[1]),
                sorted(WIN_COMMIT.items(), key=lambda x: x[1]), sorted(WIN_RB.items(), key=lambda x: x[1]))}
# ---- the RF-die SPI port (§5.2)
SPI_CTL, SPI_DATA = 0x14028, 0x1402C
LUT_CMDS = (0xD, 0xE)
# ---- the VCO scan's registers (§16.1 group 7)
SCAN_REGS = (0x11934, 0x11938, 0x1193C, 0x11968, 0x11978)
SCAN_LEAD = SCAN_REGS[:3] + (0x11940, 0x11900)
KEEP = {0x11900: 1 << 14}               # bit 14 of 0x11900 kept as found (§16.2)

# ---- the trim fields (§14.5, §16.4). Records: 0 = VM 106, 1 = 107, 2 = 108, 3 = 187. Widths [I] where the sheet
# measured only an xor 03 probe: a field that took both bits is 2 wide, one bit 1 wide (BLE-STACK.md §12)
REC = {106: 0, 107: 1, 108: 2, 187: 3}
K_FIELD, K_NONZERO = 0, 1
MMIO_FIELDS = [   # (address, shift, width, record id, byte)
    (0x11930, 13, 4, 106, 0), (0x11930, 19, 4, 106, 1),
    (0x11924, 24, 3, 107, 0), (0x11928, 0, 3, 107, 1), (0x11928, 9, 3, 107, 2), (0x11928, 15, 3, 107, 3),
    (0x11928, 22, 4, 107, 4), (0x1192C, 0, 3, 107, 5),
    (0x1191C, 8, 2, 187, 24), (0x11920, 0, 2, 187, 25), (0x11920, 10, 2, 187, 42), (0x11910, 14, 2, 187, 42),
    (0x11908, 7, 2, 187, 32), (0x11908, 15, 1, 187, 33), (0x11908, 17, 2, 187, 36), (0x11908, 25, 1, 187, 37),
    *[(0x1195C, 8 * i, 8, 187, 40 + i) for i in range(4)], *[(0x11960, 8 * i, 8, 187, 44 + i) for i in range(4)],
]
WIN_FIELDS = [    # (window, entry, shift, width, record id, byte, kind); window 2 = D, 3 = D'
    *[(2, 0x61, 2 * i, 2, 187, 4 * i, K_FIELD) for i in range(4)],
    (2, 0x62, 0, 2, 187, 16, K_FIELD), (2, 0x62, 2, 2, 187, 20, K_FIELD),
    *[(2, 0x04 + i, 6, 2, 187, 1 + 4 * i, K_FIELD) for i in range(6)],
    (3, 0x09, 0, 8, 187, 26, K_FIELD), (3, 0x0B, 0, 8, 187, 27, K_FIELD), (3, 0x0A, 0, 8, 187, 28, K_FIELD),
    (3, 0x0C, 0, 8, 187, 29, K_FIELD),
    *[(2, e, 0, 8, 108, b, K_NONZERO) for b, e in ((0, 0x0B), (1, 0x0D), (2, 0x0F), (3, 0x11), (5, 0x1B), (7, 0x1D),
                                                   (9, 0x1F), (11, 0x21), (12, 0x13), (14, 0x15), (16, 0x17),
                                                   (19, 0x19))],
]
FIELDS = [("m", a, s, w, r, b, K_FIELD) for a, s, w, r, b in MMIO_FIELDS] + \
         [("w", (win, e), s, w, r, b, k) for win, e, s, w, r, b, k in WIN_FIELDS]

# ---- the program's opcodes (hal/fm1_ble_rf.h runs them)
OP_W = 0x00          # 0x00-0x7F: register write, address index = op, then the value (4 bytes, little-endian)
OP_WIN_W = 0x80      # | window: entry, value
OP_WIN_R = 0x84      # | window: entry (the read-back lands in "last")
OP_BBP_W = 0x88      # reg, value
OP_BBP_R = 0x89      # reg
OP_LUT = 0x8A        # cmd, first entry, count, then count words (4 bytes each)
OP_TRIM = 0x8B       # field, flags (bit 0: the base is the last read-back): applies to the next write
OP_DELAY = 0x8C      # microseconds (2 bytes)
OP_SCAN = 0x8D       # the VCO scan; then the first 0x1193C value of the stock scan (4 bytes)
OP_REPLAY = 0x8E     # offset, length (2 bytes each): run that part of the program again
OP_SKIP = 0x8F       # transactions left out (2 bytes): the window-D read-back loop, TODO(hardware)
OP_SECT = 0x90       # a section marker: the §16.1 group that starts here (1 byte); writes nothing
OP_END = 0xFF


class CaptureError(Exception):
    pass


class NoStock(CaptureError):
    """no stock firmware was given or found (not: one was given and is wrong)"""


# ------------------------------------------------------------------------------------------------ inputs ---

def find_stock(arg):
    cands = [arg] if arg else [os.environ.get("FM1_STOCK_FWSC"), ROOT / "firmwares" / "FM-1.fwsc"]
    for c in cands:
        if c and Path(c).expanduser().is_file():
            p = Path(c).expanduser()
            h = hashlib.sha256(p.read_bytes()).hexdigest()
            if h != STOCK_SHA256:
                raise CaptureError(f"{p} is not stock V15 (SHA-256 {h[:16]}..., expected {STOCK_SHA256[:16]}...)")
            return p
    raise NoStock("no stock V15 FM-1.fwsc: give --stock PATH, set FM1_STOCK_FWSC, or put it at firmwares/FM-1.fwsc "
                  "(your own copy of the stock firmware; this tool never downloads it)")


def find_diagnose(arg):
    if arg:
        cands = [Path(arg)]
    elif os.environ.get("FM1_BLE_DIAGNOSE"):
        cands = [Path(os.environ["FM1_BLE_DIAGNOSE"])]
    else:
        emu = Path(os.environ.get("FM1_EMU", Path.home() / "GitHub" / "fm1-emulator-ble")).expanduser()
        cands = [emu / "rust-emulator" / "target" / "release" / "examples" / "diagnose"]
    for c in cands:
        if c.is_file() and os.access(c, os.X_OK):
            return c
    raise CaptureError(f"no emulator diagnose binary at {cands[0]}: build fm1-emulator (branch feat/ble-engine) with "
                       "'cargo build --release --example diagnose' and give --diagnose PATH, FM1_BLE_DIAGNOSE or FM1_EMU")


def run(diagnose, stock, steps, env, log):
    e = dict(os.environ, **env)
    with open(log, "w") as f:
        r = subprocess.run([str(diagnose), str(stock), str(steps)], env=e, stdout=f, stderr=subprocess.STDOUT)
    text = Path(log).read_text(errors="replace")
    if r.returncode not in (0, 1) and "instruction limit" not in text:
        raise CaptureError(f"diagnose failed ({r.returncode}); its log: {log}")
    return text


def writes(path):
    """the writes of a diagnose MMIO trace: [(step, tick, address, value)]"""
    out = []
    for line in open(path):
        if line.startswith("#"):
            continue
        f = line.split()
        if len(f) < 7:
            continue
        if f[3] == "W":
            if len(f) > 7:
                raise CaptureError(f"a faulting write in the trace: {line.strip()}")
            out.append((int(f[0]), int(f[1]), int(f[4], 16), int(f[6], 16)))
    return out


# ------------------------------------------------------------------------------------- the trace to ops ---

def cut(ws):
    """boot 2's writes -> (the RF start-up part, the AGC table): from the first write outside the clock words to the
    first write to 0x14000 (the BT block, §5.1 step 3); the AGC = the 128 words to 0x2FD9C after 0x2FD98 = 0"""
    first = next((i for i, w in enumerate(ws) if not 0x10000 <= w[2] < 0x10100), None)
    end = next((i for i, w in enumerate(ws) if w[2] == 0x14000), None)
    if first is None or end is None:
        raise CaptureError("boot 2 never reached the BT block (no write to 0x14000): the run was too short?")
    part = [w for w in ws[first:end] if not 0x10000 <= w[2] < 0x10100]   # (the clock words are the firmware's own)
    after = ws[end:]
    i = next((k for k, w in enumerate(after) if w[2] == 0x2FD98 and w[3] == 0), None)
    agc = [w[3] for w in after[i + 1:] if w[2] == 0x2FD9C][:128] if i is not None else []
    if len(agc) != 128:
        raise CaptureError(f"the AGC table: {len(agc)} words after 0x2FD98 = 0, 128 expected (§2.2)")
    return part, agc


def tokens(part):
    """writes -> ops: ('m', addr, value) | ('bbp', reg, data, rd) | ('lut', cmd, entry, data), each with its tick"""
    out, i, n = [], 0, len(part)
    flags = set()
    while i < n:
        _, t, a, v = part[i]
        if a == BBP_PORT:
            if i + 1 >= n or part[i + 1][2] != BBP_PORT or part[i + 1][3] != v | START or v & START:
                raise CaptureError(f"BBP port: a command without its start write at {part[i]}")
            flags.add(v & ~(START | READ | 0xFFFF))
            out.append((t, ("bbp", (v >> 8) & 0xFF, v & 0xFF, (v >> 16) & 1)))
            i += 2
            continue
        if a == SPI_DATA and i + 11 < n and part[i + 1][2] == SPI_CTL:
            c = part[i + 1][3]
            kick = [w[3] for w in part[i + 2:i + 12]]
            if (c & ~0xFF0F) == 0x10000 and c & 0xF in LUT_CMDS and all(w[2] == SPI_CTL for w in part[i + 2:i + 12]) \
                    and kick == [c | 0x10] * 5 + [c] * 5:
                out.append((t, ("lut", c & 0xF, (c >> 8) & 0xFF, v)))
                i += 12
                continue
        out.append((t, ("m", a, v)))
        i += 1
    if len(flags) != 1:
        raise CaptureError(f"BBP port: the command's upper bits vary ({sorted(map(hex, flags))})")
    return out, flags.pop()


def fold(toks):
    """BBP transactions -> window ops (§16.3): address, data, commit 1 = a write; address, commit 2, a read of the
    read-back register = a read. Anything else on a window register stops the tool."""
    out, i, n = [], 0, len(toks)
    def bbp(k):
        return toks[k][1] if k < n and toks[k][1][0] == "bbp" else None
    while i < n:
        t, op = toks[i]
        if op[0] != "bbp":
            out.append((t, op))
            i += 1
            continue
        _, reg, d, rd = op
        if reg in WIN_ADDR:
            w = WIN_ADDR[reg]
            a, c, r = WIN_REGS[w][0], WIN_REGS[w][2], WIN_REGS[w][3]
            b1, b2 = bbp(i + 1), bbp(i + 2)
            if b1 and b2 and b1[1] == WIN_REGS[w][1] and b2[1] == c and b2[2] == 1 and not b1[3] and not b2[3]:
                out.append((t, ("ww", w, d, b1[2])))
                i += 3
                continue
            if b1 and b2 and b1[1] == c and b1[2] == 2 and b2[1] == r and b2[3] == 1:
                out.append((t, ("wr", w, d)))
                i += 3
                continue
            raise CaptureError(f"BBP window {w}: an unknown transaction order at {op}")
        if reg in WIN_DATA or reg in WIN_COMMIT or reg in WIN_RB:
            raise CaptureError(f"BBP window register 0x{reg:02X} outside an address/data/commit triple")
        out.append((t, ("br", reg) if rd else ("bw", reg, d)))
        i += 1
    return out


# ------------------------------------------------------------------------------------- regions and trims ---

def scan_region(ops):
    """the stock VCO scan (§16.1 group 7): from the step set-up before the first 0x11968 start to the last 0x11978
    strobe. -> (first, last + 1, the first 0x1193C value written in it)"""
    starts = [k for k, (_, o) in enumerate(ops) if o[0] == "m" and o[1] == 0x11968]
    if not starts:
        raise CaptureError("no VCO scan (no write to 0x11968) in boot 2")
    s = starts[0]
    while s > 0 and ops[s - 1][1][0] == "m" and ops[s - 1][1][1] in SCAN_LEAD:
        s -= 1
    e = max(k for k, (_, o) in enumerate(ops) if o[0] == "m" and o[1] == 0x11978) + 1
    if any(o[0] != "m" for _, o in ops[s:e]):
        raise CaptureError("the VCO scan region holds BBP or LUT traffic")
    fine = next((o[2] for _, o in ops[s:e] if o[1] == 0x1193C), None)
    if fine is None:
        raise CaptureError("the VCO scan never wrote 0x1193C")
    return s, e, fine


def rmw_region(ops, after):
    """the window-D read-back loop (§16.2): the longest run of window-D ops after the scan with >= 64 reads, from its
    first read to its end -> (first, end) or None"""
    best, k, n = None, after, len(ops)
    while k < n:
        if ops[k][1][0] in ("ww", "wr") and ops[k][1][1] == 2:
            j = k
            while j < n and ops[j][1][0] in ("ww", "wr") and ops[j][1][1] == 2:
                j += 1
            reads = sum(1 for _, o in ops[k:j] if o[0] == "wr")
            if reads >= 64 and (best is None or j - k > best[1] - best[0]):
                f = next(m for m in range(k, j) if ops[m][1][0] == "wr")
                best = (f, j)
            k = j
        else:
            k += 1
    return best


def same_shape(a, b):
    if len(a) != len(b):
        raise CaptureError(f"the perturbed boot 2 differs in shape ({len(a)} vs {len(b)} ops): the trims changed the "
                           "control flow, or the VM was not accepted")
    for (_, x), (_, y) in zip(a, b):
        if x[0] != y[0] or x[1] != y[1] or (x[0] in ("ww", "wr", "lut") and x[2] != y[2]):
            raise CaptureError(f"the perturbed boot 2 differs in shape at {x} / {y}")


def field_ids_for(op, diff, prev):
    """the trim fields one changed write carries (§14.5, §16.4) -> [(field index, rmw)]; diff = old xor new"""
    if op[0] == "m":
        cand = [(i, f) for i, f in enumerate(FIELDS) if f[0] == "m" and f[1] == op[1]]
    else:
        cand = [(i, f) for i, f in enumerate(FIELDS) if f[0] == "w" and f[1] == (op[1], op[2])]
    hit, covered = [], 0
    for i, f in cand:
        m = ((1 << f[3]) - 1) << f[2]
        if diff & m:
            hit.append(i)
            covered |= m
    if not hit or diff & ~covered:
        raise CaptureError(f"a trim-dependent write no field of §14.5 / §16.4 explains: {op} (bits {diff:#x})")
    rmw = op[0] == "ww" and prev is not None and prev[0] == "wr" and prev[1:3] == op[1:3]
    return [(i, rmw) for i in hit]


# --------------------------------------------------------------------------------------- the program ---

def sections(ops, scan, rmw, marks):
    """where the groups of §16.1 start, by register pattern only -> {op index: group}. 2 the first op; 3 the first
    Wi-Fi clock / radio config write (0x14040-0x1405C, 0x30F00 / 04); 4 the first BBP op (the first load); 5 the first
    0x11930 write (the crystal trim, VM 106); 6 the first other 0x11900-0x11964 write after it, before the scan;
    7 the scan; 8 the first op after it; 10 the first BBP op after the scan (the second phase); 11 the first LUT
    word; 13 the first write carrying a VM 108 field. A group the trace does not show gets no marker."""
    s0, s1, _ = scan
    bbp = ("ww", "wr", "bw", "br")
    def first(pred, lo=0, hi=None):
        return next((k for k in range(lo, len(ops) if hi is None else hi) if pred(ops[k][1])), None)
    at = {2: 0 if ops else None,
          3: first(lambda o: o[0] == "m" and (0x14040 <= o[1] <= 0x1405C or o[1] in (0x30F00, 0x30F04)), 0, s0),
          4: first(lambda o: o[0] in bbp, 0, s0),
          5: first(lambda o: o[0] == "m" and o[1] == 0x11930, 0, s0)}
    if at[5] is not None:
        at[6] = first(lambda o: o[0] == "m" and 0x11900 <= o[1] <= 0x11964 and o[1] != 0x11930, at[5] + 1, s0)
    at[7] = s0
    at[8] = s1 if s1 < len(ops) else None
    at[10] = first(lambda o: o[0] in bbp, s1)
    at[11] = first(lambda o: o[0] == "lut")
    at[13] = next((k for k in sorted(marks) if any(FIELDS[f][6] == K_NONZERO for f, _ in marks[k])), None)
    out = {}
    for g, k in sorted(at.items()):
        if k is not None and k not in out:
            out[k] = g
    return out



def assemble(ops, scan, rmw, marks, agc, bbp_flags):
    """ops -> (program bytes, address table, notes). marks {op index: [(field, rmw)]}; LUT words a trim changes are
    counted in notes (their mapping is not known: TODO(hardware))"""
    s0, s1, fine = scan
    addrs = sorted({o[1] for k, (_, o) in enumerate(ops) if o[0] == "m" and not (s0 <= k < s1 and o[1] in SCAN_REGS)})
    if len(addrs) > 0x80:
        raise CaptureError(f"{len(addrs)} register addresses: the program's write op holds 128")
    aix = {a: i for i, a in enumerate(addrs)}
    prog, notes = bytearray(), {"skipped": 0, "dropped_scan_writes": 0}
    blocks = []            # (start op, end op, program offset, program length) of BBP runs, for the replay
    sect = sections(ops, scan, rmw, marks)
    notes["sections"] = [sect[k] for k in sorted(sect)]
    prev_tick = None
    k = 0
    # the scan region: what is not a scan register stays, before the scan op
    while k < len(ops):
        t, op = ops[k]
        if k in sect:
            prog += bytes((OP_SECT, sect[k]))
        if prev_tick is not None and k not in range(s0 + 1, s1):
            gap = (t - prev_tick) // TICKS_PER_US
            if gap >= DELAY_MIN_US:
                prog += bytes((OP_DELAY,)) + min(gap, 0xFFFF).to_bytes(2, "little")
        prev_tick = t
        if k == s0:
            for _, o in ops[s0:s1]:
                if o[1] in SCAN_REGS:
                    notes["dropped_scan_writes"] += 1
                else:
                    prog += bytes((OP_W | aix[o[1]],)) + o[2].to_bytes(4, "little")
            prog += bytes((OP_SCAN,)) + fine.to_bytes(4, "little")
            prev_tick = ops[s1 - 1][0]
            k = s1
            continue
        if rmw and k == rmw[0]:
            n = sum(3 for _ in ops[rmw[0]:rmw[1]])
            prog += bytes((OP_SKIP,)) + n.to_bytes(2, "little")
            notes["skipped"] = n
            prev_tick = ops[rmw[1] - 1][0]
            k = rmw[1]
            continue
        if op[0] in ("ww", "wr", "bw", "br"):          # a BBP run: replayed when an earlier one is the same
            j = k
            while j < len(ops) and ops[j][1][0] in ("ww", "wr", "bw", "br") and j not in marks and \
                    not (rmw and j == rmw[0]) and (j == k or j not in sect):
                j += 1
            run_ops = [o for _, o in ops[k:j]]
            same = next((b for b in blocks if b[4] == run_ops), None) if j - k >= 256 else None
            if same:
                prog += bytes((OP_REPLAY,)) + same[2].to_bytes(2, "little") + same[3].to_bytes(2, "little")
                prev_tick = ops[j - 1][0]
                k = j
                continue
            start = len(prog)
            for m in range(k, j):
                prog += encode(ops[m][1], aix)
            if j - k >= 256:
                blocks.append((k, j, start, len(prog) - start, run_ops))
            if j > k:
                prev_tick = ops[j - 1][0]
                k = j
                continue
        for f, r in marks.get(k, ()):
            prog += bytes((OP_TRIM, f, 1 if r else 0))
        if op[0] == "lut":                               # a run of consecutive entries of one command
            j = k
            while j + 1 < len(ops) and ops[j + 1][1][0] == "lut" and ops[j + 1][1][1] == op[1] and \
                    ops[j + 1][1][2] == ops[j][1][2] + 1 and j + 1 - k < 255 and j + 1 not in marks and \
                    j + 1 not in sect:
                j += 1
            prog += bytes((OP_LUT, op[1], op[2], j - k + 1))
            for m in range(k, j + 1):
                prog += ops[m][1][3].to_bytes(4, "little")
            prev_tick = ops[j][0]
            k = j + 1
            continue
        prog += encode(op, aix)
        k += 1
    prog += bytes((OP_END,))
    if len(prog) > 0xFFFF:
        raise CaptureError("the program is longer than a replay offset can reach")
    return bytes(prog), addrs, notes


def encode(op, aix):
    if op[0] == "m":
        return bytes((OP_W | aix[op[1]],)) + op[2].to_bytes(4, "little")
    if op[0] == "ww":
        return bytes((OP_WIN_W | op[1], op[2], op[3]))
    if op[0] == "wr":
        return bytes((OP_WIN_R | op[1], op[2]))
    if op[0] == "bw":
        return bytes((OP_BBP_W, op[1], op[2]))
    if op[0] == "br":
        return bytes((OP_BBP_R, op[1]))
    raise CaptureError(f"cannot encode {op}")


def trims_of(latest):
    """the four records' data from a VM read -> [bytes] in record order (106, 107, 108, 187)"""
    out = []
    for rid in ble_vm.RF_IDS:
        d = latest.get(rid)
        if d is None or len(d) != ble_vm.RF_LEN[rid]:
            raise CaptureError(f"VM record {rid} missing or of the wrong length in boot 1's flash")
        out.append(d)
    if not ble_vm.rec187_ok(out[3]):
        raise CaptureError("VM record 187's inner CRC fails in boot 1's flash")
    return out


def field_value(f, v, trims, base):
    _, _, s, w, rid, b, kind = FIELDS[f]
    x = trims[REC[rid]][b]
    if kind == K_NONZERO:
        return x if x else v
    m = ((1 << w) - 1) << s
    return (base & ~m) | ((x << s) & m)


def expand(prog, addrs, trims, bbp_flags):
    """what the firmware writes for a program (the model of hal/fm1_ble_rf.h, emulator read-backs = 0) -> [(address,
    value)], with ('scan', fine) / ('skip', n) markers where the firmware runs its own scan / leaves the loop out"""
    out = []
    def bbp(reg, d, rd):
        c = bbp_flags | rd << 16 | reg << 8 | d
        out.append((BBP_PORT, c))
        out.append((BBP_PORT, c | START))
    def run(pc, end):
        trim, last = [], 0
        while pc < end:
            op = prog[pc]
            if op < 0x80:
                v = int.from_bytes(prog[pc + 1:pc + 5], "little")
                for f, _ in trim:
                    v = field_value(f, v, trims, v)
                out.append((addrs[op], v))
                trim, pc = [], pc + 5
            elif op & 0xFC == OP_WIN_W:
                w, e, v = op & 3, prog[pc + 1], prog[pc + 2]
                if any(r for _, r in trim):
                    v = last                     # a read-modify-write: the fields go into what was read
                for f, _ in trim:
                    v = field_value(f, v, trims, v)
                a, d, c, _ = WIN_REGS[w]
                bbp(a, e, 0), bbp(d, v, 0), bbp(c, 1, 0)
                trim, pc = [], pc + 3
            elif op & 0xFC == OP_WIN_R:
                a, _, c, r = WIN_REGS[op & 3]
                bbp(a, prog[pc + 1], 0), bbp(c, 2, 0), bbp(r, 0, 1)
                last, pc = 0, pc + 2
            elif op == OP_BBP_W:
                bbp(prog[pc + 1], prog[pc + 2], 0)
                pc += 3
            elif op == OP_BBP_R:
                bbp(prog[pc + 1], 0, 1)
                pc += 2
            elif op == OP_LUT:
                cmd, e0, n = prog[pc + 1:pc + 4]
                for i in range(n):
                    c = 0x10000 | (e0 + i) << 8 | cmd
                    out.append((SPI_DATA, int.from_bytes(prog[pc + 4 + 4 * i:pc + 8 + 4 * i], "little")))
                    out.append((SPI_CTL, c))
                    out.extend([(SPI_CTL, c | 0x10)] * 5 + [(SPI_CTL, c)] * 5)
                pc += 4 + 4 * n
            elif op == OP_TRIM:
                trim.append((prog[pc + 1], prog[pc + 2] & 1))
                pc += 3
            elif op == OP_DELAY:
                pc += 3
            elif op == OP_SCAN:
                out.append(("scan", int.from_bytes(prog[pc + 1:pc + 5], "little")))
                pc += 5
            elif op == OP_REPLAY:
                o, n = int.from_bytes(prog[pc + 1:pc + 3], "little"), int.from_bytes(prog[pc + 3:pc + 5], "little")
                run(o, o + n)
                pc += 5
            elif op == OP_SKIP:
                out.append(("skip", int.from_bytes(prog[pc + 1:pc + 3], "little")))
                pc += 3
            elif op == OP_SECT:
                pc += 2
            elif op == OP_END:
                return
            else:
                raise CaptureError(f"bad opcode {op:#x} at {pc}")
    run(0, len(prog))
    return out


def reference(part, ops, scan, rmw, lut_from=None):
    """boot 2's own writes with the scan and the left-out loop replaced by the same markers expand() gives; with
    lut_from (the plain run's ops), a LUT word that differs takes the plain run's data: the firmware writes a
    trim-dependent LUT word as captured (its mapping is not known, TODO(hardware))"""
    s0, s1, fine = scan
    out, i = [], 0
    # an op's writes are contiguous in the trace: m 1, bw / br 2, ww / wr 6 (three transactions), lut 12
    widths = [1 if o[0] == "m" else 2 if o[0] in ("bw", "br") else 6 if o[0] in ("ww", "wr") else 12 for _, o in ops]
    k = 0
    while k < len(ops):
        w = widths[k]
        if k == s0:
            for m in range(s0, s1):
                if ops[m][1][1] not in SCAN_REGS:
                    out.append(tuple(part[i][2:]))
                i += widths[m]
            out.append(("scan", fine))
            k = s1
            continue
        if rmw and k == rmw[0]:
            n = 0
            for m in range(rmw[0], rmw[1]):
                i += widths[m]
                n += 3
            out.append(("skip", n))
            k = rmw[1]
            continue
        if lut_from and ops[k][1][0] == "lut" and lut_from[k][1] != ops[k][1]:
            out.append((SPI_DATA, lut_from[k][1][3]))
            i += 1
            w -= 1
        for _ in range(w):
            out.append(tuple(part[i][2:]))
            i += 1
        k += 1
    return out


def masked(seq):
    return [(a, v & ~KEEP.get(a, 0)) if isinstance(a, int) else (a, v) for a, v in seq]


# ------------------------------------------------------------------------------------------- the header ---

def header(prog, addrs, agc, notes, bbp_flags, digest, counts):
    def arr(name, ctype, vals, per, fmt):
        rows = [", ".join(fmt % v for v in vals[i:i + per]) for i in range(0, len(vals), per)]
        return f"static const {ctype} {name}[{len(vals)}] = {{\n    " + ",\n    ".join(rows) + "\n};\n"
    fields = []
    for kind, where, s, w, rid, b, k in FIELDS:
        fields += [REC[rid], b, s, w | (k << 7)]
    return (
        "/* BLE radio start-up values captured from the stock FM-1 V15 firmware (see docs/BLE-STACK.md). */\n"
        "#pragma once\n#include <stdint.h>\n"
        f"#define BLE_RF_TABLES_FORMAT {FORMAT}\n"
        f"#define BLE_RF_SHA256 \"{digest}\"\n"
        f"#define BLE_RF_BBP_FLAGS 0x{bbp_flags:X}u      /* the BBP command word's fixed upper bits (§5.2) */\n"
        f"#define BLE_RF_SKIPPED {notes['skipped']}u      /* window-D read-back loop transactions left out (§16.2) */\n"
        f"#define BLE_RF_LUT_TRIMMED {counts['lut_trim']}u   /* LUT words a stored trim changes: written as captured, "
        "TODO(hardware) */\n"
        f"#define BLE_RF_NFIELDS {len(FIELDS)}u\n"
        f"/* program {len(prog)} bytes: {counts['m']} register writes, {counts['ww']} window writes, {counts['wr']} window "
        f"reads, {counts['b']} direct BBP, {counts['lut']} LUT words, {counts['trim']} trim marks, {counts['delay']} delays, "
        f"{counts['replay']} replays, sections {' '.join(str(g) for g in notes['sections'])} (§16.1 groups) */\n"
        + arr("ble_rf_prog", "uint8_t", list(prog), 24, "0x%02X")
        + arr("ble_rf_addr", "uint32_t", addrs, 8, "0x%05Xu")
        + arr("ble_rf_agc", "uint32_t", agc, 8, "0x%08Xu")
        + "/* trim fields: record (0 = VM 106, 1 = 107, 2 = 108, 3 = 187), byte, shift, width | kind << 7 (1: a non-zero\n"
          " * byte replaces the value) (§14.5, §16.4) */\n"
        + arr("ble_rf_fields", "uint8_t", fields, 16, "%d"))


def count(prog):
    c = dict(m=0, ww=0, wr=0, b=0, lut=0, trim=0, delay=0, replay=0, sect=0)
    pc = 0
    while pc < len(prog):
        op = prog[pc]
        if op < 0x80:
            c["m"] += 1; pc += 5
        elif op & 0xFC == OP_WIN_W:
            c["ww"] += 1; pc += 3
        elif op & 0xFC == OP_WIN_R:
            c["wr"] += 1; pc += 2
        elif op == OP_BBP_W:
            c["b"] += 1; pc += 3
        elif op == OP_BBP_R:
            c["b"] += 1; pc += 2
        elif op == OP_LUT:
            c["lut"] += prog[pc + 3]; pc += 4 + 4 * prog[pc + 3]
        elif op == OP_TRIM:
            c["trim"] += 1; pc += 3
        elif op == OP_DELAY:
            c["delay"] += 1; pc += 3
        elif op in (OP_SCAN, OP_REPLAY):
            c["replay"] += op == OP_REPLAY; pc += 5
        elif op == OP_SKIP:
            pc += 3
        elif op == OP_SECT:
            c["sect"] += 1; pc += 2
        else:
            pc += 1
    return c


# ------------------------------------------------------------------------------------------------ main ---

def build_program(part, part_p, agc, trims, trims_p):
    """the two boot-2 RF parts (plain, perturbed) -> (program, addresses, notes, flags, counts) after the self-checks"""
    t0, flags = tokens(part)
    t1, flags1 = tokens(part_p)
    ops, ops_p = fold(t0), fold(t1)
    same_shape(ops, ops_p)
    scan = scan_region(ops)
    rmw = rmw_region(ops, scan[1])
    marks, lut_trim = {}, 0
    for k, ((_, a), (_, b)) in enumerate(zip(ops, ops_p)):
        if a == b:
            continue
        if scan[0] <= k < scan[1] and a[1] in SCAN_REGS:
            continue                                     # (the firmware runs its own scan)
        if rmw and rmw[0] <= k < rmw[1]:
            raise CaptureError(f"a stored trim changes the left-out read-back loop at op {k}: {a}")
        if a[0] == "lut":
            lut_trim += 1
            continue
        if a[0] == "m":
            diff = a[2] ^ b[2]
        elif a[0] == "ww":
            diff = a[3] ^ b[3]
        else:
            raise CaptureError(f"a stored trim changes a BBP op the fields do not cover: {a} / {b}")
        marks[k] = field_ids_for(a, diff, ops[k - 1][1] if k else None)
    prog, addrs, notes = assemble(ops, scan, rmw, marks, agc, flags)
    if flags1 != flags:
        raise CaptureError("BBP port: the perturbed run's command flags differ")
    want = masked(reference(part, ops, scan, rmw))
    if masked(expand(prog, addrs, trims, flags)) != want:
        raise CaptureError("self-check: the program does not reproduce boot 2's writes")
    want_p = masked(reference(part_p, ops_p, scan, rmw, ops))
    if masked(expand(prog, addrs, trims_p, flags)) != want_p:
        raise CaptureError("self-check: the program with the perturbed VM does not reproduce the perturbed run")
    c = count(prog)
    c["lut_trim"] = lut_trim
    return prog, addrs, notes, flags, c, want


def capture(stock, diagnose, out, work):
    work.mkdir(parents=True, exist_ok=True)
    b1 = work / "boot1.bin"
    print(f"ble_rf_capture: boot 1 of {stock.name} in the emulator (a first boot: V15 calibrates and stores its VM)")
    log = run(diagnose, stock, BOOT1_STEPS, {"FM1_FLASH_DUMP": str(b1)}, work / "boot1.log")
    if not b1.exists():
        raise CaptureError(f"diagnose saved no flash dump (FM1_FLASH_DUMP unsupported?): {log[-400:]}")
    image = b1.read_bytes()
    vm = ble_vm.read(image)
    if vm["area"] is None:
        raise CaptureError("boot 1 left no VM at 0x093000 / 0x095000")
    trims = trims_of(vm["latest"])
    pert = [bytes(b ^ 3 for b in d) for d in trims]
    pert[3] = ble_vm.rec187(pert[3][:64])
    b1p = work / "boot1-perturbed.bin"
    b1p.write_bytes(ble_vm.rewrite(image, vm["area"], dict(zip(ble_vm.RF_IDS, pert))))
    parts = []
    for name, img in (("boot2", b1), ("boot2-perturbed", b1p)):
        print(f"ble_rf_capture: {name} (stored trims present), tracing the RF ranges")
        tr = work / f"{name}.trace"
        run(diagnose, stock, BOOT2_STEPS, {"FM1_FLASH_RESTORE": str(img), "FM1_MMIO_TRACE": str(tr),
                                           "FM1_MMIO_RANGES": RANGES, "FM1_MMIO_TRACE_LIMIT": "4000000"},
            work / f"{name}.log")
        if not tr.exists():
            raise CaptureError(f"diagnose wrote no trace (FM1_MMIO_TRACE unsupported?): {work / (name + '.log')}")
        parts.append(cut(writes(tr)))
    (part, agc), (part_p, agc_p) = parts
    if agc != agc_p:
        raise CaptureError("the AGC table depends on the stored trims (§2.2 says it is constant)")
    prog, addrs, notes, flags, counts, want = build_program(part, part_p, agc, trims, pert)
    blob = prog + b"".join(a.to_bytes(4, "little") for a in addrs) + b"".join(v.to_bytes(4, "little") for v in agc)
    blob += bytes(x for kind, where, s, w, rid, b, k in FIELDS for x in (REC[rid], b, s, w | k << 7))
    digest = hashlib.sha256(blob).hexdigest()
    if PINNED and digest != PINNED:
        raise CaptureError(f"the captured tables' SHA-256 {digest} is not the pinned {PINNED}: the emulator or its "
                           "trace format changed (fm1-emulator feat/ble-engine 6531e20 made the pinned one); nothing "
                           "was written")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(header(prog, addrs, agc, notes, flags, digest, counts))
    side = out.parent / SIDE
    side.mkdir(exist_ok=True)
    side.joinpath("vm_emu.bin").write_bytes(image[ble_vm.VM_BASE:ble_vm.VM_BASE + 2 * ble_vm.AREA_SIZE])
    side.joinpath("expected.txt").write_text(
        "# the writes rf_init makes with vm_emu.bin's trims in the emulator (read-backs 0): address value, or a marker\n"
        + "".join(f"{a} {v}\n" if isinstance(a, str) else f"{a:05x} {v:08x}\n" for a, v in want))
    print(f"ble_rf_capture: {out} ({len(prog)} B program, {len(addrs)} addresses, 128 AGC words; {counts['ww']} window "
          f"writes, {counts['lut']} LUT words, {counts['trim']} trim marks, {notes['skipped']} read-back loop "
          f"transactions left out, {counts['lut_trim']} trim-dependent LUT words as captured); SHA-256 {digest}")
    out.parent.joinpath(META).write_text(json.dumps(
        {"stock_sha256": hashlib.sha256(stock.read_bytes()).hexdigest(), "format": FORMAT, "tables_sha256": digest},
        indent=1) + "\n")
    return digest


# ----------------------------------------------------------------------------------------- the local cache ---

def stale_reason(cache, stock=None):
    """why the cache in this folder cannot be used (None: it matches). stock: the firmware file this build was given
    (None: none given, the cache stands on its own)"""
    hdr = cache / OUT.name
    for p in (hdr, cache / META, cache / SIDE / "vm_emu.bin", cache / SIDE / "expected.txt"):
        if not p.is_file():
            return f"{p.relative_to(ROOT) if p.is_relative_to(ROOT) else p} is missing"
    try:
        meta = json.loads((cache / META).read_text())
    except ValueError:
        return f"{META} is unreadable"
    if meta.get("format") != FORMAT:
        return f"it is from capture format {meta.get('format')}, this tool makes {FORMAT}"
    if meta.get("tables_sha256") != PINNED:
        return "its tables are not the pinned ones (the capture tool or the emulator changed)"
    text = hdr.read_text()
    if f"#define BLE_RF_TABLES_FORMAT {FORMAT}\n" not in text or f'#define BLE_RF_SHA256 "{PINNED}"' not in text:
        return "the header is not the one the metadata describes"
    if stock is not None and meta.get("stock_sha256") != hashlib.sha256(stock.read_bytes()).hexdigest():
        return "it was captured from another firmware file than the one given"
    return None


def committed_reason(path=COMMITTED):
    """why the committed tables cannot be used (None: they are this tool's format and the pinned ones)"""
    if not path.is_file():
        return f"{path} is missing"
    text = path.read_text()
    if f"#define BLE_RF_TABLES_FORMAT {FORMAT}\n" not in text or f'#define BLE_RF_SHA256 "{PINNED}"' not in text:
        return f"{path} is not capture format {FORMAT} with the pinned tables"
    return None


def ensure(cache=CACHE, stock_arg=None, diagnose_arg=None, say=print, committed=COMMITTED):
    """the tables' header: the cache when it matches, else captured first when the stock firmware is given, else the
    committed copy. Raises CaptureError if the capture is needed and the emulator is not there, or if there is no
    usable committed copy either"""
    try:
        stock = find_stock(stock_arg)
    except NoStock:
        stock = None                       # (a wrong file still raises: it is never ignored)
    why = stale_reason(cache, stock)
    if why is None:
        say(f"ble      {cache.relative_to(ROOT) if cache.is_relative_to(ROOT) else cache}/{OUT.name} "
            f"(cached, captured {PINNED[:12]})")
        return cache / OUT.name
    if stock is None:
        bad = committed_reason(committed)
        if bad:
            raise CaptureError(f"no usable BLE radio tables: {bad}; or capture them from your stock V15: "
                               "FM1_STOCK_FWSC=/path/to/FM-1.fwsc (docs/BLE-STACK.md section 12)")
        say(f"ble      {committed.relative_to(ROOT) if committed.is_relative_to(ROOT) else committed} "
            f"(committed, captured {PINNED[:12]} from stock V15)")
        return committed
    diagnose = find_diagnose(diagnose_arg)
    say(f"ble      capturing the radio's start-up tables into {cache}/ ({why}; stock V15 in the emulator, about 30 s)")
    cache.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ble_rf_capture") as d:
        capture(stock, diagnose, cache / OUT.name, Path(d))
    return cache / OUT.name


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--stock", help="stock V15 FM-1.fwsc (default $FM1_STOCK_FWSC, then firmwares/FM-1.fwsc)")
    ap.add_argument("--diagnose", help="the emulator's diagnose binary (default $FM1_BLE_DIAGNOSE, then $FM1_EMU/...)")
    ap.add_argument("--out", type=Path, default=OUT, help=f"the header (default {OUT.relative_to(ROOT)})")
    ap.add_argument("--keep", type=Path, help="keep the runs (flash dumps, traces, logs) in this folder")
    a = ap.parse_args(argv)
    try:
        stock, diagnose = find_stock(a.stock), find_diagnose(a.diagnose)
        if a.keep:
            capture(stock, diagnose, a.out, a.keep)
        else:
            with tempfile.TemporaryDirectory(prefix="ble_rf_capture") as d:
                capture(stock, diagnose, a.out, Path(d))
    except CaptureError as e:
        print(f"ble_rf_capture: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
