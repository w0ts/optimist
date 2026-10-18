# SPDX-License-Identifier: GPL-3.0-only
"""The 32 DX7 algorithms as FM6 runs them (eng_fm6.c FM6_ALG, read from the source): who modulates whom, the carriers,
the feedback operator. `python3 tools/fm6_packs/algs.py [n ...]` prints them; dx7.py's ALG_CARRIERS / ALG_FB are checked
against this by tests/builder_test.py."""
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[2] / "firmware/src/engines/fm6/eng_fm6.c"


def table():
    text = SRC.read_text()
    m = re.search(r"FM6_ALG\[32\]\[6\] = \{(.*?)\};", text, re.S)
    rows = re.findall(r"\{([^{}]*)\}", m.group(1))
    return [[int(x, 16) for x in r.split(",")] for r in rows]


def topology(alg):
    """-> (mods, carriers, fb): mods[n] the set of operators (1..6) that modulate operator n"""
    fl = table()[alg - 1]
    b = {1: set(), 2: set()}
    mods = {n: set() for n in range(1, 7)}
    car = []
    fb = None
    for i in range(6):                       # the operators run OP6 .. OP1
        f, op = fl[i], 6 - i
        out, inb = f & 3, (f >> 4) & 3
        if inb:
            mods[op] = set(b[inb])
        if out == 0:
            car.append(op)
        else:
            b[out] = (b[out] | {op}) if f & 4 else {op}
        if (f & 0xC0) == 0xC0:
            fb = op
    return mods, sorted(car), fb


def show(alg):
    mods, car, fb = topology(alg)
    parts = [f"OP{n}<-" + "+".join(f"OP{m}" for m in sorted(s)) for n, s in mods.items() if s]
    return f"alg {alg:>2}: carriers {car}  feedback OP{fb}  " + "  ".join(parts)


if __name__ == "__main__":
    for a in ([int(x) for x in sys.argv[1:]] or range(1, 33)):
        print(show(a))
