#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""SLOOP 2.5 / 2.6's own stored types, cut out of their sources for tests/sl25_gen.c (not run by run_tests.sh).

    python3 tests/sl25_extract.py <sloop-fm1 checkout>/firmware/src <out.h>

Writes, as SLOOP has them (isod89/sloop-fm1 v2.5 fa9ce57 / v2.6 d30d655): panel.c's button and encoder enums and
panel_t; drum_synth.c's DS_LANES, dsnd_t, DSU_*, dsu_kit_t, dsu_bank_t; project.c's PROJ_* defines, proj_trk_t,
project_t and persist_t (with its PERSIST_MAGIC, the arranger's). core.h and arranger.h are included as they are."""
import re
import sys


def block(src, name):
    """the 'typedef struct { ... } name;' that ends at '} name;' (nested braces counted back)"""
    end = src.index("} %s;" % name)
    depth, i = 0, end
    while True:
        c = src[i]
        if c == "}":
            depth += 1
        elif c == "{":
            depth -= 1
            if depth == 0:
                break
        i -= 1
    start = src.rindex("typedef struct", 0, i)
    return src[start:end + len("} %s;" % name)] + "\n"


def line(src, pat):
    m = re.search(r"^.*%s.*$" % pat, src, re.M)
    if not m:
        raise SystemExit("not found: " + pat)
    return m.group(0) + "\n"


def bare(path):
    """the source without its comments (theirs hold braces and span lines)"""
    return re.sub(r"/\*.*?\*/", " ", open(path).read(), flags=re.S)


def main():
    r, out = sys.argv[1], sys.argv[2]
    panel = bare(r + "/panel.c")
    dsyn = bare(r + "/drum_synth.c")
    proj = bare(r + "/project.c")
    o = ["/* cut out of SLOOP's sources by tests/sl25_extract.py */\n"]
    o.append(line(panel, r"^enum \{ B_FX").rstrip("\n") + "\n" + line(panel, r"B_OCTDN, B_OCTUP, NB \};"))
    o.append(line(panel, r"^enum \{ EN_SELECT"))
    o.append(block(panel, "panel_t"))
    o.append(line(dsyn, r"^#define DS_LANES"))
    o.append(block(dsyn, "dsnd_t"))
    o.append(line(dsyn, r"^#define DSU_N"))
    o.append(line(dsyn, r"^#define DSU_MAGIC"))
    o.append(block(dsyn, "dsu_kit_t"))
    o.append(block(dsyn, "dsu_bank_t"))
    for m in re.finditer(r"^#define PROJ_(NG|NC6|NX6|NP_V5)\b.*$", proj, re.M):
        o.append(m.group(0) + "\n")
    o.append(block(proj, "proj_trk_t"))
    o.append(block(proj, "project_t"))
    o.append("#define FELUCCA_ARRANGER 1\n")
    o.append(block(proj, "persist_t"))
    o.append("#define SL_PERSIST_MAGIC 0x50455233u  /* \"PER3\" (project.c, with the arranger) */\n")
    open(out, "w").write("".join(o))


main()
