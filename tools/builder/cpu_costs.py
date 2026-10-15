# SPDX-License-Identifier: GPL-3.0-only
"""The CPU guard's cost model (CPU_GUARD, firmware/src/system/cpuguard.c), generated from measured data, never hand-tuned.

Inputs:
  tests/cpu_baseline.txt   the host CPU baseline tests/regress.c writes (BUDGET_UPDATE=1): instructions per sample
                           of each engine x preset with 8 notes held (VOICE 4, ACID 1, JUNO 6: its voices), of the idle mix
                           and of the idle mix with the drum groove
  tools/builder/costs.json "cpu": the target's instructions per host instruction, per engine where measured
                           ("scale_pct", "engine_pct"), fitted in the emulator (docs/CPU-GUARD.md, "Calibration")
Output: firmware/src/system/cpuguard_costs.h (target instructions a sample):
  CG_COST_BASE    the idle mix (buses, master, the parts' fixed work, the sequencer)
  CG_COST_DRUMS   the drum groove on top of it (any drum voice sounding)
  CG_VCOST[uid]   one sounding voice of each engine (by engine UID, registry.h ENGINE_LIST): the median over the
                  engine's presets of (preset - idle) / voices; an engine with no baseline entry (not built when
                  the baseline was taken) gets the largest measured one (an estimate errs high there)
  CG_X0X[ch]      one sounding X0X channel (drum_x0x.c: 0..10 the 909's voices, 11..23 the 808's lanes; cpu/x0x/chNN,
                  the channel kept sounding) less what the channels of its machine share: (the sum of its channels -
                  all of them at once, cpu/x0x/909_all / 808_all) / (channels - 1)
  CG_X0X_SHARED   counted once while any X0X channel sounds: the smaller of the two machines' shared work (most of it
                  is common to both: the summed channels' one pass of the mix, drum_x0x.c drums_x0x)
  CG_COST_TCOMP   a part's COMP insert while it runs (fx.c tcomp_run): cpu/fx/comp_on (the insert at 127 on a part
                  of 8 voices) less cpu/fx/comp_off (the same part, COMP in a slot at 0); 0 when not measured
  CG_COST_DBDIST  the drum bus's DIST while it runs (fx.c dbus_dist_run): cpu/fx/dbus_dist (the drum groove, the
                  drum track's DST 127) less cpu/fx/dbus_off (the groove, every drum bus amount 0); 0 when not measured
  CG_COST_DBCOMP  the drum bus's COMP while it runs (fx.c dbus_comp_run): cpu/fx/dbus_comp (COMP in S1, 127) less
                  cpu/fx/dbus_off
  CG_COST_DVDIST  a drum voice's DIST while it runs (fx.c dins_post), counted per voice: cpu/fx/dvoice_dist (one snare a
                  beat, every sound's DST 127) less cpu/fx/dvoice_off (the same, 0)
  CG_COST_DVCOMP  a drum voice's COMP while it runs, per voice: cpu/fx/dvoice_comp (COMP in S1, every sound's 127) less
                  cpu/fx/dvoice_off
  CG_COST_PDIST   a part's DIST insert while it runs (the track insert slots, fx.c ins_part): cpu/fx/part_dist (DIGITAL,
                  8 voices, S1 DIST 127) less cpu/fx/part_off (the same, both slots at 0)
  CG_COST_PFILT   a part's FILTER insert while it runs (fx.c tflt_part): cpu/fx/part_filt (S2 FILTER low-pass) less
                  cpu/fx/part_off
  CG_COST_DBFILT  the drum bus's FILTER while it runs (fx.c dbus_ins): cpu/fx/dbus_filt (the groove, the drum track's
                  FILTER low-pass) less cpu/fx/dbus_off. cpu/fx/ins_worst (three full parts with DIST + COMP, the drum
                  bus DIST + FILTER, the groove) is the check of the sum (docs/CPU-GUARD.md)
  CG_COST_PINS[i], CG_COST_DBINS[i]  the further inserts FOLD FUZZ PHSR FLNG FRMT WIDE CHRS ECHO GATER (fx/insfx/insfx.c) while they
                  run, a part's: cpu/fx/part_<t> (S1 at 127) less cpu/fx/part_off; the drum bus's (on its L and R):
                  cpu/fx/dbus_<t> less cpu/fx/dbus_off; 0 when not measured

  CG_COST_PDUST, CG_COST_DBDUST, CG_COST_TLIM, CG_COST_DVINS[i]  (fx-rework, 2026-10-14) DUST on a part (cpu/fx/part_dust less
                  part_off) and on the drum bus (dbus_dust less dbus_off); a part's DYN CEIL (part_lim, CEIL -1 dB, less
                  comp_on); a drum voice's slot of FILT DUST FOLD FUZZ PHSR FRMT (cpu/fx/dvoice_<t> less dvoice_off)

  python3 tools/builder/cpu_costs.py            write the header
  python3 tools/builder/cpu_costs.py --check    exit 1 when the header is not what the inputs give (the tests)"""
import json
import re
import statistics
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASELINE = ROOT / "tests" / "cpu_baseline.txt"
COSTS = ROOT / "tools" / "builder" / "costs.json"
REGISTRY_H = ROOT / "firmware" / "src" / "core" / "registry.h"
HEADER = ROOT / "firmware" / "src" / "system" / "cpuguard_costs.h"
NOTES = {"VOICE": 4, "ACID": 1, "JUNO": 6}   # (JUNO: six voices, eng_juno.c JUNO_POLY)
INS_NEW = ("fold", "fuzz", "phsr", "flng", "frmt", "wide", "chrs", "echo", "gate")   # the further inserts, fx_slots.c FXT_FOLD .. FXT_GATE
DV_NEW = ("filt", "dust", "fold", "fuzz", "phsr", "frmt")   # a drum sound's slot types past DIST and DYN (fx.c dins_post)


def engines():
    """[(uid, list name)] of the build's engine list (registry.h, the FELUCCA_ANALOG2 one: the shipped list)"""
    text = REGISTRY_H.read_text()
    block = text[text.index("#if FELUCCA_ANALOG2"):text.index("#else", text.index("#if FELUCCA_ANALOG2"))]
    return [(int(u), s) for u, s in re.findall(r'X\((\d+),\s*\w+,\s*\d+,\s*"([^"]+)"\)', block)]


def baseline(path=BASELINE):
    out = {}
    for ln in Path(path).read_text().splitlines():
        m = re.fullmatch(r"cpu/(\S+)/(\S+)\s+(\d+)", ln.strip())
        if m:
            out.setdefault(m.group(1), {})[m.group(2)] = int(m.group(3))
    return out


def scales(path=COSTS):
    try:
        cpu = json.loads(Path(path).read_text()).get("cpu", {})
    except (OSError, ValueError):
        cpu = {}
    return int(cpu.get("scale_pct", 100)), {k: int(v) for k, v in cpu.get("engine_pct", {}).items()}


def model(base=None, sc=None):
    """-> dict: base, drums, vcost (list by UID), names, source notes"""
    b = baseline() if base is None else base
    scale, eng_pct = scales() if sc is None else sc
    idle = b["mix"]["idle"]
    drums = b["mix"]["idle_drums"] - idle
    names = engines()
    per = {}
    for _, name in names:
        if name in b:
            per[name] = statistics.median((c - idle) / NOTES.get(name, 8) for c in b[name].values())
    top = max(per.values())

    def tgt(host, name=None):
        return max(1, round(host * eng_pct.get(name, scale) / 100))
    vcost = [tgt(per.get(name, top), name) for _, name in names]
    x0x, shared = x0x_costs(b.get("x0x", {}), idle)
    fx = b.get("fx", {})
    tcomp = fx["comp_on"] - fx["comp_off"] if "comp_on" in fx and "comp_off" in fx else 0
    dbase = fx.get("dbus_off")
    dbdist = fx["dbus_dist"] - dbase if dbase and "dbus_dist" in fx else 0
    dbcomp = fx["dbus_comp"] - dbase if dbase and "dbus_comp" in fx else 0
    vbase = fx.get("dvoice_off")
    dvdist = fx["dvoice_dist"] - vbase if vbase and "dvoice_dist" in fx else 0
    dvcomp = fx["dvoice_comp"] - vbase if vbase and "dvoice_comp" in fx else 0
    pbase = fx.get("part_off")
    pdist = fx["part_dist"] - pbase if pbase and "part_dist" in fx else 0
    pfilt = fx["part_filt"] - pbase if pbase and "part_filt" in fx else 0
    dbfilt = fx["dbus_filt"] - dbase if dbase and "dbus_filt" in fx else 0
    pins = [fx[f"part_{t}"] - pbase if pbase and f"part_{t}" in fx else 0 for t in INS_NEW]
    dbins = [fx[f"dbus_{t}"] - dbase if dbase and f"dbus_{t}" in fx else 0 for t in INS_NEW]
    pdust = fx["part_dust"] - pbase if pbase and "part_dust" in fx else 0
    dbdust = fx["dbus_dust"] - dbase if dbase and "dbus_dust" in fx else 0
    tlim = fx["part_lim"] - fx["comp_on"] if "part_lim" in fx and "comp_on" in fx else 0
    dvins = [fx[f"dvoice_{t}"] - vbase if vbase and f"dvoice_{t}" in fx else 0 for t in DV_NEW]
    return {"pdist": tgt(pdist) if pdist > 0 else 0, "pfilt": tgt(pfilt) if pfilt > 0 else 0,
            "dbfilt": tgt(dbfilt) if dbfilt > 0 else 0,"base": tgt(idle), "drums": tgt(drums), "vcost": vcost, "names": [n for _, n in names],
            "measured": sorted(per), "scale": scale, "engine_pct": eng_pct,
            "x0x": [tgt(c, "X0X") if c else 0 for c in x0x], "x0x_shared": tgt(shared, "X0X") if shared else 0,
            "tcomp": tgt(tcomp) if tcomp > 0 else 0, "dbdist": tgt(dbdist) if dbdist > 0 else 0,
            "dbcomp": tgt(dbcomp) if dbcomp > 0 else 0, "dvdist": tgt(dvdist) if dvdist > 0 else 0,
            "dvcomp": tgt(dvcomp) if dvcomp > 0 else 0,
            "pins": [tgt(c) if c > 0 else 0 for c in pins], "dbins": [tgt(c) if c > 0 else 0 for c in dbins],
            "pdust": tgt(pdust) if pdust > 0 else 0, "dbdust": tgt(dbdust) if dbdust > 0 else 0,
            "tlim": tgt(tlim) if tlim > 0 else 0, "dvins": [tgt(c) if c > 0 else 0 for c in dvins]}


X0X_NCH, X0X_CH808 = 24, 11            # drum_x0x.c: the 909's channels 0..10, the 808's 11..23


def x0x_costs(x, idle):
    """-> ([a channel's host cost net of its machine's shared work, 0 = not measured] * 24, the shared work counted
    once: the smaller machine's (0: not measured)"""
    ch = [x.get(f"ch{c:02d}", 0) - idle if f"ch{c:02d}" in x else 0 for c in range(X0X_NCH)]
    shared = []
    for m, lo, hi in (("909", 0, X0X_CH808), ("808", X0X_CH808, X0X_NCH)):
        mine = [c for c in ch[lo:hi] if c > 0]
        allc = x.get(f"{m}_all")
        s = (sum(mine) - (allc - idle)) / (len(mine) - 1) if allc and len(mine) > 1 else 0
        s = max(0, min(s, min(mine) - 1 if mine else 0))
        shared.append(s)
        for c in range(lo, hi):
            ch[c] = ch[c] - s if ch[c] > 0 else 0
    shared = [s for s in shared if s > 0]
    return ch, min(shared) if shared else 0


def header(m):
    rows = ", ".join(f"{c}" for c in m["vcost"])
    gone = [n for n in m["names"] if n not in m["measured"]]
    pct = ", ".join(f"{k} {v} %" for k, v in sorted(m["engine_pct"].items())) or "none"
    return "\n".join([
        "/* SPDX-License-Identifier: GPL-3.0-only */",
        "/* generated by tools/builder/cpu_costs.py from tests/cpu_baseline.txt (tests/regress.c) and the emulator's",
        f" * scale in tools/builder/costs.json (target instructions per host instruction: {m['scale']} %; per engine:",
        f" * {pct}): do not edit. The CPU guard's model (cpuguard.c), target instructions a sample. Engines with",
        f" * no baseline entry take the largest measured voice: {', '.join(gone) or 'none'} */",
        "#pragma once",
        f"#define CG_COST_BASE {m['base']}u      /* the idle mix */",
        f"#define CG_COST_DRUMS {m['drums']}u     /* the drum groove on top of it */",
        f"#define CG_NVCOST {len(m['vcost'])}u",
        "static const uint16_t CG_VCOST[CG_NVCOST] = {" + rows + "};   /* a sounding voice, by engine UID: "
        + " ".join(m["names"]) + " */",
        f"#define CG_X0X_SHARED {m['x0x_shared']}u    /* while any X0X channel sounds (their mix) */",
        "static const uint16_t CG_X0X[24] = {" + ", ".join(str(c) for c in m["x0x"]) + "};   /* a sounding X0X "
        "channel (drum_x0x.c: the 909's voices, the 808's lanes) */",
        f"#define CG_COST_TCOMP {m['tcomp']}u     /* a part's COMP insert while it runs (fx.c tcomp_run) */",
        f"#define CG_COST_DBDIST {m['dbdist']}u    /* the drum bus's DIST while it runs (fx.c dbus_dist_run) */",
        f"#define CG_COST_DBCOMP {m['dbcomp']}u    /* the drum bus's COMP while it runs (fx.c dbus_comp_run) */",
        f"#define CG_COST_DVDIST {m['dvdist']}u    /* a drum voice's DIST while it runs (fx.c dins_post) */",
        f"#define CG_COST_DVCOMP {m['dvcomp']}u    /* a drum voice's COMP while it runs (fx.c dins_post) */",
        f"#define CG_COST_PDIST {m['pdist']}u     /* a part's DIST insert while it runs (fx.c ins_part) */",
        f"#define CG_COST_PFILT {m['pfilt']}u     /* a part's FILTER insert while it runs (fx.c tflt_part) */",
        f"#define CG_COST_DBFILT {m['dbfilt']}u    /* the drum bus's FILTER while it runs (fx.c dbus_ins) */",
        f"static const uint16_t CG_COST_PINS[{len(INS_NEW)}] = {{" + ", ".join(str(c) for c in m["pins"]) + "};   /* a "
        "part's further insert while it runs, FOLD .. GATER (insfx.c ifx_part) */",
        f"static const uint16_t CG_COST_DBINS[{len(INS_NEW)}] = {{" + ", ".join(str(c) for c in m["dbins"]) + "};   /* the "
        "drum bus's "
        "(insfx.c ifx_bus: L and R) */",
        f"#define CG_COST_PDUST {m['pdust']}u     /* a part's DUST insert while it runs (fx.c dust_run) */",
        f"#define CG_COST_DBDUST {m['dbdust']}u    /* the drum bus's DUST while it runs (L and R) */",
        f"#define CG_COST_TLIM {m['tlim']}u      /* a track's DYN CEIL (its limiter) while it runs (master_comp.c tlim_block) */",
        f"static const uint16_t CG_COST_DVINS[{len(DV_NEW)}] = {{" + ", ".join(str(c) for c in m["dvins"]) + "};   /* a "
        "drum voice's slot while it runs: FILT DUST FOLD FUZZ PHSR FRMT (fx.c dins_post) */",
        ""])


def main(argv):
    text = header(model())
    if "--check" in argv:
        ok = HEADER.exists() and HEADER.read_text() == text
        print(f"cpu_costs: {HEADER.relative_to(ROOT)} {'matches' if ok else 'is NOT what'} tests/cpu_baseline.txt "
              f"{'and costs.json' if ok else 'and costs.json give: python3 tools/builder/cpu_costs.py'}")
        return 0 if ok else 1
    HEADER.write_text(text)
    print(f"cpu_costs: wrote {HEADER.relative_to(ROOT)}")
    print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
