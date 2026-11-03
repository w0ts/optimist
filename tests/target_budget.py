#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Target-side cost estimate of the hot DSP code, from the pi32v2 objdump listing tools/build.py writes
(build/felucca.dis). For each engine's render function and the audio ISR: the instructions
inside loops (every address range closed by a backward branch: the per-sample loops and what they
contain), and the hardware divides and calls in them. cost = the loop instructions, each weighted 4 per
level of nesting (an inner loop runs several times per sample) and 1 + 8 for a divide (many cycles). A static count, not a cycle count: it changes only when the compiled code changes,
so it is exact run to run, and it catches a render loop that grew (more work per sample, a new divide,
something not inlined any more). Compared with tests/target_budget.txt at +10 %. The host
CPU check (regress.c) is the primary one; this one sees the target compiler's code.
  tests/target_budget.py [DIS [BUDGET]]      BUDGET_UPDATE=1 rewrites BUDGET"""
import os
import re
import sys

FUNCS = ["analog_render", "digital_render", "phase_render", "lofi_render", "sample_render", "formant_render",
         "trio_render", "trio_pass", "drawbar_render", "drawbar_block",
         "grain_render", "grain_block", "super_render", "slicer_track", "drums_mix",
         "fm1_alnk0_irq", "a2_saw", "a2_saw2", "a2_pulse", "a2_tri", "a2_sin", "a2_lp", "a2_bp", "a2_hp", "a2_lp2",
         "a2_lp_i", "a2_bp_i", "a2_hp_i", "a2_lp2_i"]
# in one build of FELUCCA_ANALOG2 only (1: ANALOG 2's kernels, eng_analog2.c; 0: SUPER): the other's skip
VARIANT = {"super_render", "a2_saw", "a2_saw2", "a2_pulse", "a2_tri", "a2_sin", "a2_lp", "a2_bp", "a2_hp", "a2_lp2",
           "a2_lp_i", "a2_bp_i", "a2_hp_i", "a2_lp2_i"}
# the builder (tools/builder): a function of an item this build leaves out (build/gen/felucca_config.h) skips
OWNER = {"analog_render": "ENG_ANALOG", "digital_render": "ENG_DIGITAL", "phase_render": "ENG_PHASE",
         "lofi_render": "ENG_LOFI", "sample_render": "ENG_SAMPLE", "formant_render": "ENG_FORMANT",
         "trio_render": "ENG_TRIO", "trio_pass": "ENG_TRIO", "drawbar_render": "ENG_DRAWBAR",
         "drawbar_block": "ENG_DRAWBAR", "grain_render": "ENG_GRAIN", "grain_block": "ENG_GRAIN",
         "slicer_track": "FX_SLICER", **{f: "ENG_ANALOG" for f in VARIANT if f.startswith("a2_")}}


def switches(value, cfg_h="build/gen/felucca_config.h"):
    """the FELUCCA_ switches at value (0, 1) in this build's configuration header"""
    try:
        text = open(cfg_h).read()
    except OSError:
        return set()
    return {m.group(1) for m in re.finditer(rf"#define FELUCCA_(\w+) {value}\b", text)}


def left_out(cfg_h="build/gen/felucca_config.h"):
    """the FELUCCA_ switches at 0 in this build's configuration header"""
    return switches(0, cfg_h)


# functions that hold two exclusive paths with FELUCCA_SIMD (the packed one and the scalar fallback the boot probe
# picks, eng_analog2.c a2_saw2): one call runs one of them, so a SIMD build counts each outermost loop nest apart
# and takes the costlier, against the same budget as the scalar build
SIMD_PATHS = {"a2_saw2"}


TOL = 0.10                      # exact (no noise): small edits pass, a grown render loop does not
DIV_W = 8                       # a divide weighs 1 + 8 instructions
NEST = 4                        # an instruction in a loop inside a loop weighs 4, two deep 16, ...
MAXD = 4

dis = sys.argv[1] if len(sys.argv) > 1 else "build/felucca.dis"
budget = sys.argv[2] if len(sys.argv) > 2 else "tests/target_budget.txt"
LABEL = re.compile(r"^([A-Za-z_][A-Za-z_0-9.]*):$")
INSN = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)$")
TARGET = re.compile(r"goto -?\d+ <[^>]*: ([0-9a-f]+) >")


def functions(path):
    """{name: [(addr, text)]}: a function runs to the next label not starting with '.' (the compiler's
    jump-table labels are inside functions); jump-table data ('< n : 0x.. >') is left out"""
    out, cur = {}, None
    with open(path) as f:
        for line in f:
            m = LABEL.match(line.strip())
            if m:
                name = m.group(1)
                if not name.startswith("."):
                    cur = out.setdefault(name, []) if name in FUNCS else None
                continue
            m = INSN.match(line)
            if cur is not None and m and not m.group(3).lstrip().startswith("<"):
                cur.append((int(m.group(1), 16), m.group(3).strip()))
    return out


TRAMP = 4                       # a branch target this many instructions or fewer before an unconditional goto


def landing(insns, at, target):
    """where a branch to target goes on to: through a trampoline (a few plain instructions, then an unconditional
    goto: the compiler's stub for a forward jump placed before the branch) to that goto's target, else target"""
    for _ in range(4):
        i = at.get(target)
        if i is None:
            return target
        for _, t in insns[i:i + TRAMP]:
            if t.startswith("goto "):
                m = TARGET.search(t)
                target = int(m.group(1), 16) if m else target
                break
            if "goto" in t or "if" in t or "{" in t or "}" in t or t.startswith(("call", "rti", "rts")):
                return target
        else:
            return target
    return target


def nests(heads):
    """the outermost loop nests: the spans merged where they overlap, as (first, last) address pairs"""
    out = []
    for h, e in sorted(heads.items()):
        if out and h <= out[-1][1]:
            out[-1] = (out[-1][0], max(out[-1][1], e))
        else:
            out.append((h, e))
    return out


def cost(insns, paths=False):
    """loops: one span per loop head (the farthest backward branch to it, a branch to a trampoline that jumps
    forward again not being one); an instruction inside d spans weighs NEST ** (d - 1) (an inner loop runs
    several times per pass of the outer one). paths: the outermost nests are exclusive paths (SIMD_PATHS), the
    costliest one counts"""
    lo = insns[0][0]
    at = {a: i for i, (a, _) in enumerate(insns)}
    heads = {}
    for a, t in insns:
        m = TARGET.search(t)
        h = landing(insns, at, int(m.group(1), 16)) if m else a
        if lo <= h < a:
            heads[h] = max(heads.get(h, a), a)
    per = {nest: {"loop": 0, "div": 0, "call": 0, "cost": 0} for nest in nests(heads)}
    for a, t in insns:
        d = min(sum(1 for h, e in heads.items() if h <= a <= e), MAXD)
        if not d:
            continue
        k = NEST ** (d - 1)
        div = bool(re.search(r"= r\d+ / r\d+", t))
        if div:
            k *= 1 + DIV_W
        c = per[next(nest for nest in per if nest[0] <= a <= nest[1])]
        c["loop"] += 1
        c["div"] += div
        c["call"] += t.startswith("call")
        c["cost"] += k
    if paths and per:
        best = max(per.values(), key=lambda c: c["cost"])
        return {"insns": len(insns), **best, "paths": len(per)}
    tot = {key: sum(c[key] for c in per.values()) for key in ("loop", "div", "call", "cost")}
    return {"insns": len(insns), **tot}


def main():
    if not os.path.exists(dis):
        print(f"target: skip ({dis} missing: run ./build.sh)")
        return 0
    fns = functions(dis)
    simd = "SIMD" in switches(1)
    res = {n: cost(fns[n], simd and n in SIMD_PATHS) for n in FUNCS if fns.get(n)}
    missing = [n for n in FUNCS if not fns.get(n)]
    base = {}
    if os.path.exists(budget):
        for line in open(budget):
            p = line.split()
            if len(p) >= 2 and not line.startswith("#"):
                base[p[0]] = int(p[1])
    if os.environ.get("BUDGET_UPDATE"):
        with open(budget, "w") as f:
            f.write("# FELUCCA target cost budget (tests/target_budget.py): instructions in the loops of each\n"
                    f"# function in build/felucca.dis, x{NEST} per nesting level, divides x{1 + DIV_W}. The check allows "
                    f"+{TOL * 100:.0f} %.\n# Rewritten by BUDGET_UPDATE=1.\n")
            for n in FUNCS:                       # (the other build's entries are kept)
                if n in res or n in base:
                    f.write(f"{n} {res[n]['cost'] if n in res else base[n]}\n")
        print(f"target: budget {budget} rewritten ({len(res)} functions)")
    fail = 0
    off = left_out()
    for n in missing:
        if OWNER.get(n) in off:
            print(f"target: skip {n} (not in this build: {OWNER[n]} off)")
            continue
        if n in VARIANT:
            print(f"target: skip {n} (not in this build: FELUCCA_ANALOG2)")
            continue
        print(f"target: FAIL {n} not found in {dis} (renamed? inlined? update FUNCS)")
        fail += 1
    for n, r in res.items():
        b = base.get(n)
        state = "no budget (BUDGET_UPDATE=1 adds it)" if b is None else "ok"
        if b is not None and not os.environ.get("BUDGET_UPDATE"):
            if r["cost"] > b * (1 + TOL):
                state = f"OVER BUDGET (+{(r['cost'] / b - 1) * 100:.0f} %, limit +{TOL * 100:.0f} %)"
                fail += 1
            elif r["cost"] < b * (1 - TOL):
                state = f"note: {(r['cost'] / b - 1) * 100:.0f} % (BUDGET_UPDATE=1 to keep it)"
        print(f"target: {n:15s} {r['insns']:5d} instructions, {r['loop']:4d} in loops, {r['div']} divides, "
              f"{r['call']:2d} calls there: cost {r['cost']:5d} (budget {b if b is not None else '-'}) {state}")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
