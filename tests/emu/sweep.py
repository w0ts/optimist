#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""emu-pass: every synth preset of a build through the emulated chip (tests/emu's driver and session).

  python3 tests/emu/sweep.py BUILD_DIR OUT_DIR [--mhz 192] [--max 400] [--per-engine N] [--a2]

BUILD_DIR holds felucca.fwsc, felucca.elf and gen/felucca_config.h. Track T1 (the mixer's first synth row after
boot, ALGORITHM untouched) is walked with PRESETS one detent at a time; PRESETS stays in the engine's own sounds
(since 2026-10-21), so where the same one is read 4 times in a row (the engine's list's end) SHIFT2 + PRESETS steps
to the next engine's first sound; the walk ends when the first (engine, preset) comes back, an engine already walked
comes again, or at --max detents. A preset read again (a bank whose voices share one trk.preset, FM6's packs, the
list's end) is walked past, not played. After a fault the session's gestures are replayed from a fresh boot.
For each preset: one note (white key 5, 0.7 s), then a 4-note chord (white 1, 3, 5, 8; 1.4 s), each released and given 0.4 s
of tail, recorded as one WAV. Per preset: the engine (trk[0].engine -> ENGINES[slot]->name), the preset's name
(ENGINES[slot]->presets[preset].name), peak / RMS of the note and the chord, full-scale samples, the largest
sample-to-sample step, the tail RMS, felucca_dbg's late halves, boots and the slowest half (max_us, reset per preset)
and fm1_crash. A guest fault (the driver's error) is recorded; the session is opened again from a fresh flash and the
walk goes on at the next detent.
--per-engine N: play only the first N presets of each engine (the others are walked past). --a2: then the ANALOG 2
filter models: on the first ANALOG preset, P_A2FTYP poked to model * 5 + mode (LP12, LP24) for each model built.
Writes OUT_DIR/results.json and OUT_DIR/results.txt (and a PNG and a WAV per preset)."""
import argparse
import json
import math
import struct
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))                       # (tests/emu first on the path)
import fwfacts as F          # noqa: E402  (tests/emu on the path first)
import session as S          # noqa: E402

FS = 44100
NOTE = [F.white(5)]
CHORD = [F.white(1), F.white(3), F.white(5), F.white(8)]     # (the checklist's 4-note chord)
ENG_PRESETS_OFF, ENG_NPRESETS_OFF = 108, 112       # engine_t: name, page_title[2], edit[8] (12 B each), presets, npresets
PRESET_SIZE = 28                                   # preset_t (core.h: "28 B an entry")
DBG_MAX_US = "felucca_dbg+8"                       # (magic, halves, max_us, ...)


def cstr(e, addr):
    try:
        return e.rodata(addr, 24).split(b"\0")[0].decode("latin1")
    except S.EmuError:
        return f"@{addr:#x}"


class Names:
    """slot -> engine name, (slot, preset) -> preset name, from the ELF's const tables"""

    def __init__(self, e):
        a, size = e.sym("ENGINES")
        self.n = size // 4
        self.eng = [int.from_bytes(e.rodata(a + 4 * i, 4), "little") for i in range(self.n)]
        self.names = [cstr(e, int.from_bytes(e.rodata(p, 4), "little")) for p in self.eng]

    def engine(self, slot):
        return self.names[slot] if slot < self.n else f"slot{slot}"

    def preset(self, e, slot, k):
        if slot >= self.n:
            return "?"
        p = self.eng[slot]
        tab = int.from_bytes(e.rodata(p + ENG_PRESETS_OFF, 4), "little")
        npre = e.rodata(p + ENG_NPRESETS_OFF, 1)[0]
        if k >= npre or not tab:
            return f"#{k} (of {npre})"
        return cstr(e, int.from_bytes(e.rodata(tab + PRESET_SIZE * k, 4), "little"))


def stats(x):
    if not x:
        return dict(peak=0.0, rms=0.0, full=0, step=0.0, dc=0.0)
    return dict(peak=max(abs(v) for v in x), rms=S.rms(x), full=sum(1 for v in x if abs(v) >= 0.999),
                step=max(abs(x[i] - x[i - 1]) for i in range(1, len(x))), dc=sum(x) / len(x))


def play(e, wav):
    """the note, then the chord -> (note stats, chord stats, tail rms), from both channels"""
    e.align()
    e.wavstart()
    e.hold(*NOTE)
    e.run(0.7)
    e.release(*NOTE)
    e.run(0.4)
    e.hold(*CHORD)
    e.run(1.4)
    e.release(*CHORD)
    e.run(0.4)
    left, right = e.wavstop(wav)
    n1 = int(1.1 * FS)
    both = lambda a, b: [v for pair in zip(a, b) for v in pair]  # noqa: E731
    note = stats(both(left[:n1], right[:n1]))
    chord = stats(both(left[n1:], right[n1:]))
    tail = S.rms(both(left[-int(0.1 * FS):], right[-int(0.1 * FS):]))
    return note, chord, tail


class Walker:
    def __init__(self, a):
        self.a = a
        self.bd = Path(a.build).resolve()
        self.out = Path(a.out).resolve()
        self.out.mkdir(parents=True, exist_ok=True)
        self.facts = F.Facts(self.bd / "felucca.fwsc")
        self.pkg = S.stage(self.bd / "felucca.fwsc", self.out / "run")
        self.eng_off = 2 * self.facts.P["P_COUNT"]
        self.e = None

    def open(self, moves=()):
        """a fresh session, then the walk's gestures so far replayed: "p" PRESETS +1, "e" SHIFT2 + PRESETS +1 (the next
        engine's first sound: PRESETS alone stays in the engine's own sounds since 2026-10-21)"""
        if self.e:
            self.e.close()
        self.e = S.Emu(self.pkg, self.out / "run", fresh=True, mhz=self.a.mhz)
        self.e.run(3.0)
        self.trk, size = self.e.sym("trk")
        self.stride = size // 4
        if not hasattr(self, "names"):
            self.names = Names(self.e)
        for m in moves:
            self.move(m)
        self.e.run(0.4)

    def move(self, m):
        if m == "e":
            self.e.hold(F.BTN["OCTUP"])
            self.e.run(0.15)
            self.e.click("PRESETS", 1)
            self.e.release(F.BTN["OCTUP"])
            self.e.run(1.0)                             # (OCT+ let go: no octave change, a knob was used meanwhile)
        else:
            self.e.click("PRESETS", 1)
            self.e.run(0.4)

    def where(self):
        b = self.e.mem(f"{self.trk + self.eng_off:#x}", 4)
        return b[0], b[1], b[2]                      # engine, preset, eng_req

    def dbg(self):
        d = self.e.dbg()
        cr = self.e.peek("fm1_crash", 2)
        d["crash"] = cr[1] if cr[0] == 0x43525348 else 0
        return d

    def one(self, i, slot, pre):
        e = self.e
        name = self.names.engine(slot)
        r = dict(i=i, slot=slot, engine=name, preset=pre, name=self.names.preset(e, slot, pre))
        e.poke(DBG_MAX_US, 0)
        d0 = self.dbg()
        e.png(str(self.out / f"{i:03d}-{name}-{pre}.png"))
        r["note"], r["chord"], r["tail"] = play(e, str(self.out / f"{i:03d}-{name}-{pre}.wav"))
        d1 = self.dbg()
        r.update(late=d1["late"] - d0["late"], boots=d1["boots"], max_us=d1["max_us"], cpu_q8=d1["cpu_q8"],
                 crash=d1["crash"], fault=None)
        return r

    def walk(self):
        res, seen, count, first, last, same = [], set(), {}, None, None, 0
        moves, done = [], set()                         # (the gestures so far; the engines walked to their end)
        self.open()
        i = 0
        while i < self.a.max:
            try:
                slot, pre, req = self.where()
                key = (slot, pre)
                if first is None:
                    first = key
                elif key == first and i > 1:
                    break                               # (SHIFT2 + PRESETS past the last engine: the first again)
                if slot in done:
                    break                               # (SHIFT2 + PRESETS moved to no engine not walked)
                same = same + 1 if key == last else 0
                last = key
                if same >= 3:                           # (PRESETS stops at the engine's last sound: the next engine)
                    done.add(slot)
                    moves.append("e")
                    self.move("e")
                    same, last = 0, None
                    continue
                if key in seen:
                    r = dict(i=i, slot=slot, engine=self.names.engine(slot), preset=pre, skip="seen")
                elif count.get(slot, 0) >= self.a.per_engine:
                    r = dict(i=i, slot=slot, engine=self.names.engine(slot), preset=pre, skip="per-engine")
                else:
                    r = self.one(i, slot, pre)
                    r["moves"] = "".join(moves)
                    count[slot] = count.get(slot, 0) + 1
                seen.add(key)
                res.append(r)
                self.line(r)
                i += 1
                moves.append("p")
                self.move("p")
            except S.EmuError as x:
                r = dict(i=i, fault=str(x)[:600], err=self.e.err[-12:] if self.e else [])
                res.append(r)
                self.line(r)
                i += 1
                moves.append("p")
                try:
                    self.open(moves)
                except S.EmuError as y:                 # (the replay to preset i faulted too: stop this walk)
                    r = dict(i=i, fault="reopen: " + str(y)[:600], err=self.e.err[-12:] if self.e else [])
                    res.append(r)
                    self.line(r)
                    break
        return res

    def a2(self, res):
        """the ANALOG 2 filter models on the first ANALOG preset played"""
        out = []
        an = [r for r in res if r.get("engine") == "ANALOG" and "note" in r]
        if self.a.a2_at is not None:
            an = [dict(i=self.a.a2_at, moves="p" * self.a.a2_at)]
        if not an:
            return out
        self.open(an[0]["moves"])
        pid = self.facts.P["P_A2FTYP"]
        at = self.trk + 2 * pid
        e4 = self.trk + 2 * self.facts.P["P_E4"]
        print("A2 on", self.where(), "FTYP", self.e.word(f"{at:#x}") & 0xFFFF, "E4 (cutoff)", self.e.word(f"{e4:#x}") & 0xFFFF, flush=True)
        for mdl, mname in enumerate(("SVF", "LADR", "OTA", "DIOD", "MS20")):
            if mdl and not self.facts.on("A2_" + mname):
                continue
            for mode, mo in ((0, "LP12"), (1, "LP24"), (2, "BP"), (3, "HP"), (4, "NOTCH")):
                v = mdl * 5 + mode
                try:
                    w = self.e.word(f"{at & ~3:#x}")
                    sh = 16 * ((at & 3) // 2)
                    self.e.poke(f"{at & ~3:#x}", (w & ~(0xFFFF << sh)) | (v << sh))
                    got = self.e.word(f"{at & ~3:#x}") >> sh & 0xFFFF
                    r = self.one(len(res) + len(out), *self.where()[:2])
                    r.update(a2=f"{mname} {mo}", ftyp=got)
                except S.EmuError as x:
                    r = dict(a2=f"{mname} {mo}", fault=str(x)[:600], err=self.e.err[-12:])
                    self.open(an[0]["moves"])
                out.append(r)
                self.line(r)
        return out

    def line(self, r):
        print(fmt(r), flush=True)


def fmt(r):
    if r.get("fault"):
        return f"{r.get('i', '-'):>3} {r.get('a2', '')} FAULT {r['fault'][:200]}"
    if r.get("skip"):
        return f"{r['i']:>3} {r['engine']:8s} {r['preset']:3d} (skip: {r['skip']})"
    n, c = r["note"], r["chord"]
    flags = []
    if c["peak"] < 0.001 and n["peak"] < 0.001:
        flags.append("SILENT")
    if c["full"] or n["full"]:
        flags.append("FULLSCALE")
    if c["step"] > 0.5 or n["step"] > 0.5:
        flags.append("JUMP")
    if r["late"]:
        flags.append("LATE")
    if r["boots"] != 1 or r["crash"]:
        flags.append("RESET")
    tag = (r.get("a2") or "") + " "
    return (f"{r['i']:>3} {tag}{r['engine']:8s} {r['preset']:3d} {r['name'][:16]:16s} note {n['peak']:.3f}/{n['rms']:.4f} "
            f"chord {c['peak']:.3f}/{c['rms']:.4f} full {n['full'] + c['full']} step {max(n['step'], c['step']):.3f} "
            f"tail {r['tail']:.4f} late {r['late']} max_us {r['max_us']} cpu {r['cpu_q8'] * 100 // 256}% "
            f"{' '.join(flags) or 'ok'}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build")
    ap.add_argument("out")
    ap.add_argument("--mhz", type=int, default=192)
    ap.add_argument("--max", type=int, default=400)
    ap.add_argument("--per-engine", type=int, default=1000)
    ap.add_argument("--a2", action="store_true")
    ap.add_argument("--a2-at", type=int, help="the A2 test on the preset this many PRESETS detents from boot")
    ap.add_argument("--a2-only", action="store_true", help="no walk: the A2 test only")
    a = ap.parse_args()
    t0 = time.time()
    w = Walker(a)
    res = [] if a.a2_only else w.walk()
    if a.a2:
        res += w.a2(res)
    w.e.close()
    Path(a.out, "results.json").write_text(json.dumps(res, indent=1))
    Path(a.out, "results.txt").write_text("\n".join(fmt(r) for r in res) + "\n")
    print(f"done: {len(res)} rows in {time.time() - t0:.0f} s")


if __name__ == "__main__":
    main()
