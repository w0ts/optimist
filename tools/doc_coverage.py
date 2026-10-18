#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Documentation coverage: what the product has against what the user documents say (stdlib only).

  python3 tools/doc_coverage.py            print the missing list; exit 1 when anything is missing
  python3 tools/doc_coverage.py --write    also rewrite docs/DOC-COVERAGE.md (the full table)
  python3 tools/doc_coverage.py --all      print the full table

Sources, read from the tree (nothing is typed in twice):
  - every builder item of tools/builder/registry.py: its key must be in docs/BUILDER.md;
  - every page title of firmware/src/core/params.c (PAGES): it must be named in a user document;
  - every insert effect type (FXT_* in firmware/src/fx/fx_slots.c): named, with what it does, in a user document;
  - CONCEPTS below: the features a user looks for (gestures, sequencer tools, SYSTEM > INFO lines ...), each with the
    words a document has to carry before it counts as explained.

A document is a *user* document when it is the manual (OPTIMIST.md), the builder reference (docs/BUILDER.md), the guides
(docs/guide/*.md) or the README. A term found only in docs/UI-OPTIMIST-DESIGN.md is `design only`: written down for
the developers, not yet explained for a user. Status: ok, design only, MISSING (exit status 1).
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
USER_DOCS = ["OPTIMIST.md", "docs/BUILDER.md", "README.md"] + sorted(
    str(p.relative_to(ROOT)) for p in (ROOT / "docs" / "guide").glob("*.md"))
DESIGN_DOCS = ["docs/UI-OPTIMIST-DESIGN.md"]
TABLE = ROOT / "docs" / "DOC-COVERAGE.md"

# (area, name, [regexes that must all match in ONE document]); the regexes are case-sensitive unless they start (?i)
CONCEPTS = [
    # effects
    ("effects", "DYN: compressor and limiter in one", [r"\bDYN\b", r"(?i)limiter", r"\bCEIL\b", r"\bRATIO\b"]),
    ("effects", "insert slots S1 / S2 on tracks, master, drum sounds", [r"\bS1\b", r"\bS2\b", r"(?i)insert slot"]),
    ("effects", "master insert slots", [r"(?i)master.{0,60}insert|insert.{0,60}master"]),
    ("effects", "three master returns A B C", [r"(?i)returns?\*{0,2}\W+(buses\W+)?A, B and C\b"]),
    ("effects", "sends row REV DLY CHO and PAN", [r"\bREV\b", r"\bDLY\b", r"\bCHO\b", r"\bSENDS\b"]),
    ("effects", "USB audio in as a mixer track (USB IN)", [r"USB IN", r"(?i)track"]),
    ("effects", "DUCK sidechain", [r"\bDUCK\b"]),
    ("effects", "PUNCH-in effects", [r"(?i)punch-in"]),
    ("effects", "reverb algorithms", [r"\bPLATE\b", r"\bFDN8\b", r"\bSPRING\b"]),
    ("effects", "delay (time, feedback, colour, mix)", [r"(?i)delay", r"(?i)feedback"]),
    # sequencer
    ("sequencer", "p-locks: what can be locked, how to record and clear", [r"(?i)p-lock|parameter lock", r"SHIFT"]),
    ("sequencer", "motion recording", [r"(?i)motion recording"]),
    ("sequencer", "per-step chance", [r"(?i)chance"]),
    ("sequencer", "step conditions (COND)", [r"(?i)condition", r"(?i)1:2|every other|fill"]),
    ("sequencer", "nudge and micro timing", [r"(?i)nudge", r"(?i)micro"]),
    ("sequencer", "fills", [r"(?i)\bfills?\b"]),
    ("sequencer", "EUCLID note generator with keep / restore", [r"EUCLID", r"(?i)keep", r"(?i)restore"]),
    ("sequencer", "pattern tools (TOOLS: copy, rotate, ...)", [r"(?i)pattern tools|\bTOOLS\b"]),
    ("sequencer", "arpeggiator (modes, rate, order)", [r"(?i)arp", r"(?i)order"]),
    ("sequencer", "scale and chords (SCL)", [r"\bSCL\b", r"(?i)chord"]),
    ("sequencer", "patterns and scenes", [r"(?i)scene", r"(?i)pattern"]),
    ("sequencer", "song chain", [r"(?i)song"]),
    ("sequencer", "snapshots", [r"(?i)snapshot"]),
    ("sequencer", "undo and redo", [r"(?i)undo", r"(?i)redo"]),
    ("sequencer", "mute and solo", [r"(?i)\bmute\b", r"(?i)\bsolo\b"]),
    ("sequencer", "performance macros COLOR MOTION SPACE ENERGY", [r"\bCOLOR\b", r"\bMOTION\b", r"\bSPACE\b", r"\bENERGY\b"]),
    ("sequencer", "drum ACCENT lane", [r"\bACCENT\b"]),
    ("sequencer", "drum step cycle normal / hard / ghost / off", [r"(?i)ghost", r"(?i)hard"]),
    # modulation and voices
    ("modulation", "LFO 1 and LFO 2", [r"\bLFO 2\b|\bLFO2\b", r"\bLFO\b"]),
    ("modulation", "ENV 1 and ENV 2 with two free destinations", [r"\bENV 2\b|\bENV2\b", r"(?i)destination"]),
    ("modulation", "modulation of modulation (LFO or ENV on an LFO's rate / amount)", [r"(?i)modulation of modulation|L1 RT|L2 AMT"]),
    ("modulation", "VOICES per track", [r"\bVOICES\b"]),
    ("modulation", "voice modes (POLY, MONO, LEGATO, UNISON)", [r"\bUNISON\b", r"\bLEGATO\b"]),
    # engines
    ("engines", "ANALOG 2", [r"ANALOG 2"]), ("engines", "DIGITAL", [r"\bDIGITAL\b"]), ("engines", "PHASE", [r"\bPHASE\b"]),
    ("engines", "LOFI", [r"\bLOFI\b"]), ("engines", "SAMPLE", [r"\bSAMPLE\b"]), ("engines", "VOICE", [r"\bVOICE\b"]),
    ("engines", "TRIO", [r"\bTRIO\b"]), ("engines", "WHEEL", [r"\bWHEEL\b"]), ("engines", "GRAIN", [r"\bGRAIN\b"]),
    ("engines", "FM6", [r"\bFM6\b"]), ("engines", "SLICE", [r"\bSLICE\b"]), ("engines", "PHYS", [r"\bPHYS\b"]),
    ("engines", "ACID", [r"\bACID\b"]), ("engines", "CZ", [r"\bCZ\b"]), ("engines", "JUNO", [r"\bJUNO\b"]),
    ("engines", "NOISE", [r"\bNOISE\b"]),
    # interface and project
    ("interface", "SHIFT = OCT- held, SHIFT2 = OCT+ held", [r"\bSHIFT2\b", r"OCT-"]),
    ("interface", "both OCT buttons together", [r"(?i)both OCT|OCT- and OCT\+ together|OCT- \+ OCT\+"]),
    ("interface", "octave changes on release", [r"(?i)octave", r"(?i)let go|release"]),
    ("interface", "favorites", [r"(?i)favorite"]),
    ("interface", "PROJECT: banks, LOAD / SAVE / DELETE, USER", [r"\bPROJECT\b", r"(?i)bank", r"\bDELETE\b"]),
    ("interface", "lit = works LED rule", [r"(?i)\bLEDs?\b.{0,120}(glow|lit|dark)"]),
    ("interface", "GLOBAL page", [r"\bGLOBAL\b"]),
    ("interface", "SYSTEM page", [r"\bSYSTEM\b"]),
    ("interface", "SYSTEM > INFO lines and what they mean", [r"\bINFO\b", r"(?i)underrun", r"CPU1"]),
    ("interface", "mixer: volume, pan, sends, mute strike-through", [r"(?i)strike|struck"]),
    ("interface", "tempo page (PLAY held), tap tempo", [r"(?i)tap tempo|tempo page"]),
    # connectivity
    ("connectivity", "BLE MIDI", [r"\bBLE\b"]),
    ("connectivity", "USB audio", [r"(?i)USB audio"]),
    ("connectivity", "USB MIDI and TRS MIDI", [r"(?i)TRS"]),
    ("connectivity", "MIDI clock in and out, sync", [r"(?i)clock in|clock out|CLK IN|CLK OUT"]),
    ("connectivity", "local web installer and editor (make web)", [r"make web"]),
    ("connectivity", "update loader progress, package ids", [r"FM-1_7"]),
]

FX_NAMES = {"FXT_DIST": "DIST", "FXT_COMP": "DYN", "FXT_FILT": "FILT", "FXT_FOLD": "FOLD", "FXT_FUZZ": "FUZZ",
            "FXT_PHSR": "PHSR", "FXT_FLNG": "FLNG", "FXT_FRMT": "FRMT", "FXT_WIDE": "WIDE", "FXT_CHRS": "CHRS",
            "FXT_ECHO": "ECHO", "FXT_GATE": "GATER", "FXT_DUST": "DUST", "FXT_JUNO": "JUNO",
            "FXT_SFLD": "SFLD", "FXT_WRAP": "WRAP", "FXT_RECT": "RECT", "FXT_HARM": "HARM", "FXT_RING": "RING",
            "FXT_SUBO": "SUB", "FXT_SLEW": "SLEW"}


def read(rel):
    p = ROOT / rel
    return p.read_text(encoding="utf-8", errors="replace") if p.is_file() else ""


def hits(texts, pats):
    """The documents (names) in which every pattern matches."""
    out = []
    for name, t in texts.items():
        if all(re.search(p, t) for p in pats):
            out.append(name)
    return out


def classify(user, design, pats):
    u, d = hits(user, pats), hits(design, pats)
    if u:
        return "ok", u
    if d:
        return "design only", d
    return "MISSING", []


def registry_keys():
    sys.path.insert(0, str(ROOT / "tools" / "builder"))
    import registry
    return [(k, it.label) for k, it in registry.ITEMS.items()]


def page_titles():
    s = read("firmware/src/core/params.c")
    a = s.index("static const page_t PAGES[] = {")
    b = s.index("};", a)
    seen = []
    for m in re.finditer(r'^\s*\{"([^"]+)",\s*FAM_', s[a:b], re.M):
        if m.group(1) not in seen:
            seen.append(m.group(1))
    return seen


def fx_types():
    s = read("firmware/src/fx/fx_slots.c")
    m = re.search(r"enum \{ (FXT_NONE.*?)\};", s, re.S)
    names = re.findall(r"FXT_[A-Z0-9]+", m.group(1)) if m else []
    return [FX_NAMES[n] for n in names if n in FX_NAMES]


def build_rows():
    user = {d: read(d) for d in USER_DOCS}
    design = {d: read(d) for d in DESIGN_DOCS}
    builder = user["docs/BUILDER.md"]
    rows = []
    for key, label in registry_keys():
        ok = f"`{key}`" in builder
        rows.append(("builder item", f"{key} ({label})", "docs/BUILDER.md" if ok else "-", "ok" if ok else "MISSING"))
    for t in page_titles():
        pats = [r"(?<![A-Za-z0-9])" + re.escape(t) + r"(?![A-Za-z0-9])"]
        st, where = classify(user, design, pats)
        rows.append(("page", t, ", ".join(where) or "-", st))
    for n in fx_types():
        pats = [r"\b" + n + r"\b"]
        st, where = classify(user, design, pats)
        rows.append(("insert type", n, ", ".join(where) or "-", st))
    for area, name, pats in CONCEPTS:
        st, where = classify(user, design, pats)
        rows.append((area, name, ", ".join(where) or "-", st))
    return rows


def table(rows):
    out = ["# Documentation coverage", "",
           "Generated by `python3 tools/doc_coverage.py --write`; do not edit by hand. A row is **ok** when a user "
           "document (OPTIMIST.md, docs/BUILDER.md, docs/guide/*.md, README.md) carries the words that explain it, "
           "**design only** when only docs/UI-OPTIMIST-DESIGN.md does, **MISSING** when nothing does. The sources "
           "and the words each concept needs are in the tool.", "",
           "| Area | Item | Documented in | Status |", "|---|---|---|---|"]
    out += [f"| {a} | {n.replace('|', '/')} | {w} | {s} |" for a, n, w, s in rows]
    n_bad = sum(1 for r in rows if r[3] != "ok")
    out += ["", f"{len(rows)} items, {n_bad} not ok.", ""]
    return "\n".join(out)


def main(argv):
    rows = build_rows()
    bad = [r for r in rows if r[3] == "MISSING"]
    soft = [r for r in rows if r[3] == "design only"]
    if "--write" in argv:
        TABLE.write_text(table(rows), encoding="utf-8")
    if "--all" in argv:
        print(table(rows))
    for a, n, _, _ in soft:
        print(f"design only: [{a}] {n}")
    for a, n, _, _ in bad:
        print(f"MISSING: [{a}] {n}")
    print(f"doc coverage: {len(rows)} items, {len(bad)} missing, {len(soft)} design only")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
