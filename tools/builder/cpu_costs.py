# SPDX-License-Identifier: GPL-3.0-only
"""The CPU guard's cost model (CPU_GUARD, firmware/src/system/cpuguard.c), generated from measured data, never hand-tuned.

Inputs:
  tests/cpu_baseline.txt   the host CPU baseline tests/regress.c writes (BUDGET_UPDATE=1): instructions per sample
                           of each engine x preset with 8 notes held (VOICE 4, ACID 1: its voices), of the idle mix
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
NOTES = {"VOICE": 4, "ACID": 1}          # voices the regress CPU job sounds (8 notes held; its engine's cap)


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
    return {"base": tgt(idle), "drums": tgt(drums), "vcost": vcost, "names": [n for _, n in names],
            "measured": sorted(per), "scale": scale, "engine_pct": eng_pct,
            "x0x": [tgt(c, "X0X") if c else 0 for c in x0x], "x0x_shared": tgt(shared, "X0X") if shared else 0,
            "tcomp": tgt(tcomp) if tcomp > 0 else 0}


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
