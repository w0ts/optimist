#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The engines and the factory presets are listed by name, fixed at build time (tools/gen_bank_order.py), and nothing
stored moves: the generated order is a permutation of what bank_src.h and registry.h hold (same engine UIDs, same
kind / UID / name of every entry), the engines come out sorted by the names the UI shows, the entries by engine then
by name in both ENGINE_LIST variants, and a guard (#if) travels with its entry.
  python3 tests/engine_order_test.py"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import gen_bank_order as G  # noqa: E402

fails = 0


def check(what, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + what)
    fails += not ok


def parse(out):
    """{variant: (engine uids by name, [(guard, ENGINE, kind, uid, name)])} from the generated header"""
    res, variant, guards = {}, None, []
    for line in out.splitlines():
        if line.startswith("#if FELUCCA_ANALOG2") and not guards:
            variant, res[variant] = "A2", ([], [])
        elif line == "#else" and len(guards) == 0:
            variant, res[variant] = "OLD", ([], [])
        elif line.startswith("#define ENG_ALPHA_UIDS"):
            res[variant][0].extend(int(x) for x in line.split(" ", 2)[2].split(", "))
        elif line.startswith("#if "):
            guards.append(line[4:])
        elif line == "#endif":
            if guards:
                guards.pop()
        elif line.startswith("B("):
            e, k, u, n = G.B_RE.match(line).groups()
            res[variant][1].append((" && ".join(g for g in guards if g != "defined(BANK_ENTRIES)"), e, k, u.strip(), n))
        elif line.startswith("#ifdef BANK_ENTRIES"):
            guards.append("defined(BANK_ENTRIES)")
    return res


reg = (ROOT / "firmware/src/core/registry.h").read_text()
src = G.bank_entries((ROOT / "firmware/src/core/bank_src.h").read_text())
engines = G.engine_lists(reg)
out = parse(G.render(reg, (ROOT / "firmware/src/core/bank_src.h").read_text()))

for var, key in (("A2", "A2"), ("OLD", "OLD")):
    uids, entries = out[var]
    names = {u: s for u, _n, s in engines[key]}
    check(f"{var}: the engine UIDs are a permutation of ENGINE_LIST's (no UID added, dropped or renumbered)",
          sorted(uids) == sorted(names))
    shown = [names[u] for u in uids]
    check(f"{var}: the engines are sorted by the names the UI shows: {' '.join(shown[:8])} ...",
          shown == sorted(shown, key=G.sort_key))
    check(f"{var}: every entry kept (kind, UID, name, guard); none added or lost",
          sorted(entries) == sorted(src))
    rank = {n: i for i, (_u, n, _s) in enumerate(sorted(engines[key], key=lambda e: G.sort_key(e[2])))}
    eng = (lambda e: "ANALOG" if var == "A2" and e == "SUPER" else e)
    listed = [x for x in entries if eng(x[1]) in rank]
    keys = [(rank[eng(x[1])], G.sort_key(x[4])) for x in listed]
    check(f"{var}: the entries are by engine, then by preset name", keys == sorted(keys))

check("sort key: case-insensitive", G.sort_key("abc") == G.sort_key("ABC"))
check("sort key: leading punctuation skipped", G.sort_key("-ZED") == G.sort_key("zed"))
check("sort key: digit runs by value (808 before 8580)", G.sort_key("808 BOOM") < G.sort_key("8580 ACID"))
check("sort key: digits before letters", G.sort_key("8BIT ARP") < G.sort_key("ACID"))
uids, entries = out["A2"]
check("a guarded entry keeps its guard (LP24 BASS: FELUCCA_ANALOG2 is the variant, DIG_TONE stays)",
      any(g == "FELUCCA_DIG_TONE" and n == "TONE BELL" for g, _e, _k, _u, n in entries))
print("FAIL" if fails else "PASS")
sys.exit(1 if fails else 0)
