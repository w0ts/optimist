#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The one-line parameter help (builder item PARAM_HELP): tools/param_help.json -> the firmware's header and the
web editor's copy (run by tools/build.py).

  gen_param_help.py OUT.h            the header, and web/editor.html's block brought up to date
  gen_param_help.py --check          exit 1 when web/editor.html's block differs from the table (tests)
  gen_param_help.py --stats          sizes: the strings as they are and packed
  gen_param_help.py --all OUT.h      every group without its condition, and PH_PLAIN, every line as it reads (for
                                     tests/param_help_pack_test.c)

The table (tools/param_help.json): groups, each {"kind": "page" | "eng" | "fm6", "ctx": ..., "if": C condition,
"params": {LABEL: TEXT | {"text": TEXT, "if": C condition}}, "slots": [null | [LABEL, TEXT] | [LABEL, TEXT, cond]]}.
  page  a device page by its title (params.c PAGES), its values by their label; also the live screens' knobs
        (TRACKS, DRUM GRID, DRUM KIT, SONG) and the pages without descriptors (STEP, MOTION, USER ...) by "slots":
        KNOB 1..4, the label is what the screen shows (the device looks them up as "1".."4")
  eng   an engine's EDIT values (P_E0..P_E7, also their mode-dependent labels: engine_t.desc) by engine name
  fm6   the FM6 operator editor (ui_fm6.c) by its page name
  op    the Optimist UI's help line (ui/optimist/op_help.c): its settings, actions and cells without a descriptor, by
        the row's name as the screen names it (GLOBAL and SYSTEM: the section, PROJECT: the row) or the screen's name,
        then the cell's label; built only with FELUCCA_UI == 1 (the editor's copy has them too: its GLOBAL / SYSTEM lines)
Each line fits the top bar: WIDTH_PX pixels of the UI font (Terminus 8x16, assets/fonts/ter-u16n.bdf).

The header: one const string in flash (PH_BLOB), the groups one after the other, each wrapped in its "if" so a
build carries the lines of the features it has: kind byte, context, NUL, then label NUL text NUL ..., an empty
label ends the group; kind 0 ends the table; each text packed (CODES, PH_PAIR below). ui/ph_blob.c looks a line up as
(kind, context, label) -> offset and unpacks it (ph_text).
The editor's copy (web/editor.html, between the PARAM_HELP markers): every line, whatever a build has (the editor
talks to any firmware): PARAM_HELP = {page: {CTX: {LABEL: TEXT}}, eng: {...}, fm6: {...}}.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TABLE = ROOT / "tools" / "param_help.json"
EDITOR = ROOT / "web" / "editor.html"
FONT = ROOT / "assets" / "fonts" / "ter-u16n.bdf"
WIDTH_PX = 232                     # the top bar from x 4 to 236 (ui_draw.c draw_head), the live screens' header too
KINDS = {"page": 1, "eng": 2, "fm6": 3, "op": 4}
EDITOR_KINDS = ("page", "eng", "fm6", "op")   # (op: the Optimist UI's own lines: the editor's SCRN lines, GLOBAL and SYSTEM, by their row)
OP_IF = "FELUCCA_UI == 1"          # (an op group: in the Optimist UI's builds only)
BEGIN = "/* PARAM_HELP BEGIN: tools/gen_param_help.py from tools/param_help.json (do not edit here) */"
END = "/* PARAM_HELP END */"


def font_widths(path=FONT):
    """code -> advance (BDF DWIDTH; the font's cell width when a glyph has none)"""
    lines = path.read_text(errors="replace").splitlines()
    cw = int(next(l for l in lines if l.startswith("FONTBOUNDINGBOX")).split()[1])
    w, code = {}, None
    for l in lines:
        if l.startswith("ENCODING"):
            code = int(l.split()[1])
            w[code] = cw
        elif l.startswith("DWIDTH") and code is not None:
            w[code] = int(l.split()[1])
    return w


def text_px(s, widths):
    return sum(widths.get(ord(c), widths.get(ord("?"), 8)) for c in s)


def load(path=TABLE):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def entries(group):
    """-> [(device label, shown label, text, condition or "")] of a group"""
    out = []
    for lab, v in group.get("params", {}).items():
        text, cond = (v, "") if isinstance(v, str) else (v["text"], v.get("if", ""))
        out.append((lab, lab, text, cond))
    for i, s in enumerate(group.get("slots", [])):
        if s:
            out.append((str(i + 1), s[0], s[1], s[2] if len(s) > 2 else ""))
    return out


def validate(table, widths=None):
    """-> [problems]: kinds, empty or too wide lines, characters outside printable ASCII, duplicates"""
    widths = widths or font_widths()
    bad, seen = [], set()
    for g in table["groups"]:
        if g.get("kind") not in KINDS:
            bad.append(f"{g.get('ctx')}: kind {g.get('kind')!r}")
        key = (g.get("kind"), g.get("ctx"), g.get("if", ""))
        if key in seen:
            bad.append(f"{g['kind']} {g['ctx']}: twice with the same condition")
        seen.add(key)
        for dev, shown, text, _ in entries(g):
            where = f"{g['kind']} {g['ctx']} / {shown}"
            if not text.strip():
                bad.append(f"{where}: empty")
            if any(not (32 <= ord(c) < 127) for c in text + dev + g["ctx"]):
                bad.append(f"{where}: a character outside printable ASCII")
            if text_px(text, widths) > WIDTH_PX:
                bad.append(f"{where}: {text!r} is {text_px(text, widths)} px (> {WIDTH_PX})")
    return bad


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def c_bytes(bs):
    """a packed text as a C string literal: printable ASCII as itself, a pair's code as a 3-digit octal escape (never
    taken with a digit after it, as a hex escape would be)"""
    return '"' + "".join(chr(b) if 32 <= b < 127 and chr(b) not in '"\\' else
                         ("\\" + chr(b) if chr(b) in '"\\' else f"\\{b:03o}") for b in bs) + '"'


# The texts are packed with a byte pair code (ph_blob.c ph_text unpacks them): a byte of CODES stands for a pair of
# bytes, each a character or an earlier code; printable ASCII stands for itself. Labels and contexts stay plain (the
# lookup compares them). The pairs: the most frequent adjacent pair over every text, again and again, while one
# saves bytes (its 2 B in PH_PAIR); one table for every build, made from every group.
CODES = list(range(0x80, 0x100)) + list(range(0x01, 0x20))   # (ph_blob.c: 0x80..0xFF pairs 0..127, 1..31 128..158)


def make_pairs(texts, codes=CODES):
    """-> [(a, b)]: the pair codes[i] stands for"""
    seqs = [list(t.encode("ascii")) for t in texts]
    pairs = []
    for code in codes:
        cnt = {}
        for s in seqs:
            for p in zip(s, s[1:]):
                cnt[p] = cnt.get(p, 0) + 1
        if not cnt:
            break
        n, p = max((n, (-p[0], -p[1])) for p, n in cnt.items())   # (ties: the lowest pair, so the table is stable)
        if n <= 2:
            break
        pair = (-p[0], -p[1])
        pairs.append(pair)
        seqs = [_merge(s, pair, code) for s in seqs]
    return pairs


def _merge(s, pair, code):
    out, i = [], 0
    while i < len(s):
        if i + 1 < len(s) and (s[i], s[i + 1]) == pair:
            out.append(code)
            i += 2
        else:
            out.append(s[i])
            i += 1
    return out


def pack(text, pairs, codes=CODES):
    s = list(text.encode("ascii"))
    for code, pair in zip(codes, pairs):
        s = _merge(s, pair, code)
    return bytes(s)


def unpack(bs, pairs, codes=CODES):
    """the text of a packed one (as ph_blob.c ph_text)"""
    m = dict(zip(codes, pairs))

    def put(c):
        return chr(c) if c not in m else put(m[c][0]) + put(m[c][1])
    return "".join(put(c) for c in bs)


def texts_of(table):
    return [text for g in table["groups"] for _, _, text, _ in entries(g)]


def header(table, every=False):
    """the firmware's header text; every: every group without its condition, and PH_PLAIN, every line as it reads
    (the test of the packing: tests/param_help_pack_test.c)"""
    texts = texts_of(table)
    pairs = make_pairs(texts)
    bad = [t for t in texts if unpack(pack(t, pairs), pairs) != t]
    if bad:
        raise SystemExit(f"gen_param_help: the packing loses {bad[:3]}")
    out = ["/* generated by tools/gen_param_help.py from tools/param_help.json: do not edit */",
           "/* PH_BLOB: kind (PH_*) context NUL, then label NUL text NUL ..., NUL; kind 0 ends it (ph_blob.c); a text",
           " * packed: a byte 0x80..0xFF or 0x01..0x1F stands for a pair of PH_PAIR, the rest for itself */",
           "#define PH_PAGE 1", "#define PH_ENG 2", "#define PH_FM6 3", "#define PH_OP 4",
           f"#define PH_LINE {max(len(t) for t in texts) + 1}   /* the longest text and its NUL */",
           f"static const uint8_t PH_PAIR[{len(pairs)}][2] = {{"]
    for i in range(0, len(pairs), 8):
        out.append("    " + " ".join(f"{{{a}, {b}}}," for a, b in pairs[i:i + 8]))
    out += ["};", "static const char PH_BLOB[] ="]
    for g in table["groups"]:
        cond = "" if every else g.get("if", "")
        if g["kind"] == "op" and not every:
            cond = f"{OP_IF} && ({cond})" if cond else OP_IF
        if cond:
            out.append(f"#if {cond}")
        out.append(f'    "\\{KINDS[g["kind"]]:03o}" {c_str(g["ctx"])} "\\0"')
        for dev, _, text, ec in entries(g):
            if ec and not every:
                out.append(f"#if {ec}")
            out.append(f"        {c_str(dev)} \"\\0\" {c_bytes(pack(text, pairs))} \"\\0\"")
            if ec and not every:
                out.append("#endif")
        out.append('    "\\0"')
        if cond:
            out.append("#endif")
    out.append('    "";   /* (+ the implied NUL: the end) */')
    if every:
        out.append("static const char *const PH_PLAIN[][3] = {   /* every line: context, label, text */")
        out += [f"    {{{c_str(g['ctx'])}, {c_str(dev)}, {c_str(text)}}}," for g in table["groups"]
                for dev, _, text, _ in entries(g)]
        out.append("};")
    return "\n".join(out) + "\n"


def editor_table(table):
    """the editor's copy: {kind: {ctx: {shown label: text}}}, every line"""
    t = {k: {} for k in EDITOR_KINDS}
    for g in table["groups"]:
        if g["kind"] not in t:
            continue
        d = t[g["kind"]].setdefault(g["ctx"], {})
        for _, shown, text, _ in entries(g):
            d.setdefault(shown, text)
    return t


def editor_block(table):
    return f"{BEGIN}\nconst PARAM_HELP = {json.dumps(editor_table(table), separators=(',', ':'), sort_keys=True)};\n{END}"


def editor_sync(table, write=True, path=EDITOR):
    """-> True when web/editor.html holds the table's block (written when write and it did not)"""
    html = path.read_text(encoding="utf-8")
    pat = re.compile(re.escape(BEGIN) + r".*?" + re.escape(END), re.S)
    if not pat.search(html):
        raise SystemExit(f"gen_param_help: no PARAM_HELP markers in {path}")
    new = pat.sub(lambda m: editor_block(table), html, count=1)
    if new == html:
        return True
    if write:
        path.write_text(new, encoding="utf-8")
    return False


def blob_bytes(table, pairs=None):
    """the bytes PH_BLOB takes with every group in (the largest build), its texts packed with pairs (None: plain)"""
    n = 1
    for g in table["groups"]:
        n += 1 + len(g["ctx"]) + 1 + 1
        n += sum(len(dev) + 1 + len(pack(text, pairs) if pairs else text) + 1 for dev, _, text, _ in entries(g))
    return n


def main(argv):
    table = load()
    bad = validate(table)
    if bad:
        print("gen_param_help: tools/param_help.json:\n  " + "\n  ".join(bad), file=sys.stderr)
        return 1
    if argv[:1] == ["--check"]:
        if not editor_sync(table, write=False):
            print("gen_param_help: web/editor.html's PARAM_HELP block is out of date (python3 tools/gen_param_help.py "
                  "build/gen/felucca_param_help.h)", file=sys.stderr)
            return 1
        return 0
    if argv[:1] == ["--stats"]:
        texts = texts_of(table)
        pairs = make_pairs(texts)
        plain, packed = sum(len(t) + 1 for t in texts), sum(len(pack(t, pairs)) + 1 for t in texts)
        print(f"param help: {len(texts)} lines in {len(table['groups'])} groups, blob {blob_bytes(table)} B plain, "
              f"{blob_bytes(table, pairs)} B packed + {2 * len(pairs)} B of pairs (all groups); texts {plain} B -> "
              f"{packed} B")
        return 0
    every = argv[:1] == ["--all"]
    if len(argv) != 1 + every:
        print(__doc__, file=sys.stderr)
        return 2
    out = Path(argv[every])
    out.parent.mkdir(parents=True, exist_ok=True)
    text = header(table, every)
    if not out.exists() or out.read_text() != text:
        out.write_text(text)
    if every:
        return 0
    wrote = not editor_sync(table)
    print(f"param help: {sum(len(entries(g)) for g in table['groups'])} lines, {blob_bytes(table)} B at most plain"
          + (" (web/editor.html updated)" if wrote else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
