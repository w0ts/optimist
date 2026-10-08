#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Integer divides by a variable in the firmware (firmware/src, firmware/hal).

The HAL keeps the CPU's divide-by-zero exception off (fm1_irq.h: EMU_CON bit 2 cleared, as Felucca
852bc72 #61: the compiler can hoist a divide above its guard, so a guarded divide could still trap).
A zero divisor is then a wrong number rather than a crash, still a bug. Every '/' and '%' whose right operand is not a constant must be listed in
tools/div_audit.txt with why its divisor cannot be zero ("file:function: divisor: reason").

  tools/div_audit.py           check: every variable divide is listed, every listed one exists
  tools/div_audit.py --list    print the variable divides (file, function, expression)

A divisor is constant when it is a number, an all-caps name (a macro or enum), sizeof, or a
parenthesised expression of those. Anything else counts as variable. Comments and strings are
ignored. The key is file:function:divisor, so moving code keeps the entries."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIRS = [ROOT / "firmware" / "src", ROOT / "firmware" / "hal"]
AUDIT = ROOT / "tools" / "div_audit.txt"
FUNC = re.compile(r"^(?!#)[A-Za-z_][\w \t*]*?\b([A-Za-z_]\w*)\s*\([^;{]*\)\s*(?:__attribute__\s*\(\(.*?\)\)\s*)*\{", re.M | re.S)
CONST_TOKEN = re.compile(r"^(?:\d[\w.]*|sizeof|[+\-*/%<>()&|~^ ]|<<|>>)$")
CONSTS = set()                       # object-like #defines and enum constants of the firmware (macros())


# firmware/src is in folders by domain (docs/SOURCE-LAYOUT.md); X0X's float units were in subfolders this
# audit never read (it read firmware/src/*.[ch]), and stay out of it: auditing them is a task of its own
NOT_AUDITED = {
    "drums/x0x/drum808.c", "drums/x0x/drum808.h", "drums/x0x/drum909.c", "drums/x0x/drum909.h",
    "drums/x0x/drum909_dsp.h", "drums/x0x/x0x_drums.c", "engines/acid/acid_dsp.c", "engines/acid/bass303.c",
    "engines/acid/bass303.h"}


def sources(d):
    """the .c and .h files of d; firmware/src with its folders, without NOT_AUDITED"""
    src = ROOT / "firmware" / "src"
    if d != src:
        return [*d.glob("*.c"), *d.glob("*.h")]
    return [f for f in [*d.rglob("*.c"), *d.rglob("*.h")] if f.relative_to(src).as_posix() not in NOT_AUDITED]


def strip(text):
    """comments and string / char literals blanked, lines kept"""
    def blank(m):
        return re.sub(r"[^\n]", " ", m.group(0))
    return re.sub(r"/\*.*?\*/|//[^\n]*|\"(?:\\.|[^\"\\\n])*\"|'(?:\\.|[^'\\\n])*'", blank, text, flags=re.S)


def operand(text, i):
    """the right operand of the operator ending at i: a primary expression with postfixes"""
    j = i
    while j < len(text) and text[j] in " \t":
        j += 1
    if j < len(text) and text[j] == "(":
        depth, k = 0, j
        while k < len(text):
            depth += {"(": 1, ")": -1}.get(text[k], 0)
            k += 1
            if depth == 0:
                break
        # a cast "(type)x": keep going with the operand after it
        inner = text[j + 1:k - 1].strip()
        if re.fullmatch(r"(?:const\s+)?(?:unsigned\s+|signed\s+)?(?:u?int\d+_t|int|long|unsigned|char|short|size_t)", inner):
            rest = operand(text, k)
            return text[j:k] + rest
        return text[j:k]
    m = re.match(r"[A-Za-z_]\w*(?:\s*(?:\[[^\]]*\]|\.\s*\w+|->\s*\w+|\([^()]*\)))*|\d[\w.]*", text[j:])
    return m.group(0) if m else ""


def is_const(expr):
    expr = re.sub(r"sizeof\s*(?:\((?:[^()]|\([^()]*\))*\)|\w+(?:\[\w*\])?)", " 1 ", expr)
    expr = re.sub(r"\(\s*(?:const\s+)?(?:unsigned\s+|signed\s+)?(?:u?int\d+_t|int|long|unsigned|char|short|size_t)\s*\)", " ", expr)
    toks = re.findall(r"<<|>>|\d[\w.]*|[A-Za-z_]\w*|\S", expr)
    return bool(toks) and all(CONST_TOKEN.match(t) or t in CONSTS for t in toks)


def macros():
    """names #defined without parameters, and enum constants"""
    names = set()
    for d in [*DIRS, ROOT / "build" / "gen"]:             # (generated headers after a build: CTL, SMP_NSETS)
        for f in sources(d):
            text = strip(f.read_text())
            names.update(re.findall(r"^\s*#\s*define\s+([A-Za-z_]\w*)\b(?!\()", text, re.M))   # (\b: not a prefix of a function-like name)
            for body in re.findall(r"\benum\b[^{;]*\{([^}]*)\}", text):
                body = re.sub(r"^\s*#[^\n]*", "", body, flags=re.M)   # (#if / #endif inside: core.h P_TFLT .. P_COUNT)
                names.update(re.findall(r"(?:^|,)\s*([A-Za-z_]\w*)", body))
    return names


def divides():
    CONSTS.update(macros())
    out = []
    for d in DIRS:
        for f in sorted(sources(d)):
            text = strip(f.read_text())
            funcs = [(m.start(), m.group(1)) for m in FUNC.finditer(text)]
            for m in re.finditer(r"(?<![/*])([/%])(?![/*=])|([/%])=", text):
                op = m.group(1) or m.group(2)
                end = m.end()
                expr = operand(text, end)
                if not expr or is_const(expr):
                    continue
                if op == "/" and text[m.start() - 1:m.start()] in ("*",):
                    continue
                fn = "-"
                for pos, name in funcs:
                    if pos < m.start():
                        fn = name
                line = text.count("\n", 0, m.start()) + 1
                rel = f.relative_to(ROOT / "firmware")
                out.append((f"{rel}:{fn}:{re.sub(r'\s+', '', expr)}", f"{rel}:{line}"))
    return out


def main(argv):
    found = divides()
    if argv[:1] == ["--list"]:
        for key, where in found:
            print(f"{where:32} {key}")
        print(f"{len(found)} variable divides")
        return 0
    listed = {}
    for ln in AUDIT.read_text().splitlines():
        if ln.strip() and not ln.startswith("#"):
            key, _, why = ln.partition(" | ")
            listed[key.strip()] = why.strip()
    keys = {k for k, _ in found}
    missing = sorted({(k, w) for k, w in found if k not in listed})
    stale = sorted(k for k in listed if k not in keys)
    unexplained = sorted(k for k, why in listed.items() if not why)
    for k, w in missing:
        print(f"div_audit: {w}: {k}: a divide by a variable not in tools/div_audit.txt (why can it not be 0?)")
    for k in stale:
        print(f"div_audit: {k}: listed in tools/div_audit.txt but not found")
    for k in unexplained:
        print(f"div_audit: {k}: listed without a reason")
    if missing or stale or unexplained:
        return 1
    print(f"div_audit: {len(keys)} divides by a variable, each with its reason (tools/div_audit.txt)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
