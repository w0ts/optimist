#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Move firmware/src into folders by domain (docs/SOURCE-LAYOUT.md): one folder per engine, FX, drum source, the
sequencer, storage, the UI, I/O, the system. File names do not change, only their folders, so a bare reference such
as `seq.c:968` in a doc or a comment stays right.

  python3 tools/reorg.py check    every file in firmware/src has a place in LAYOUT (exit 1 if not); nothing moves
  python3 tools/reorg.py plan     the old -> new table (Markdown), from LAYOUT
  python3 tools/reorg.py move     git mv per LAYOUT, and every quoted #include in firmware/ and tests/ rewritten
                                  so it names the same file as before (checked: each include resolves, in the new
                                  tree, to the moved copy of what it resolved to in the old one); commit this alone
  python3 tools/reorg.py fix     for a branch rebased onto the moved tree: every quoted #include (in firmware/ and tests/)
                                  that no longer resolves, and whose file name is one tracked file under firmware/src,
                                  is respelled relative to its includer; prints what it changed and what it could not
  python3 tools/reorg.py paths    the tools, tests, web tests and docs: `firmware/src/<old>` -> `firmware/src/<new>`
                                  and the path lists the tools build in code (size_fns.py, build.py, cpu_costs.py...);
                                  upstream provenance (another project's firmware/src/...) is left as it is and listed

Re-runnable on a later optimist: `check` names any new file; give it a folder in LAYOUT and run again. Run from
anywhere; it works on the git tree it lives in. Nothing is committed."""
import os
import posixpath
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = "firmware/src"
HAL = "firmware/hal"

# new folder (under firmware/src) -> the files it takes (their paths under firmware/src today). felucca.c, the unity
# build's one translation unit, stays where it is.
LAYOUT = {
    "core": ["core.h", "registry.h", "backports.h", "backports23.h", "backports24.h",
             "backports24seq.h", "params.c", "bp_set.c", "voice.c", "audio.c",
             "macro.c"],
    "seq": ["seq.c", "qnt_seq.c", "chance.c", "motion.c", "undo.c", "arranger.c", "arranger.h", "arranger_scene.c",
            "seq24.c", "seq_midi.c", "stepx.h"],
    "storage": ["storage.c", "project.c", "upreset.c", "drum_store.c", "motion_flash.c", "motion_proj.c", "miss.c",
                "settings_word.c", "stepx_proj.c", "nbank.c"],
    "storage/sl24": ["sl24_import.c", "sl24_export.c", "sl24_guard.c"],
    "storage/sections": ["sections.c", "sec_codec.c", "sec_log.c", "stepx_log.c"],
    "storage/snapshots": ["snapshots.c", "snap_store.c"],
    "engines": ["engines.c", "preset_trim.h"],
    "engines/analog": ["eng_analog.c", "eng_analog2.c"],
    "engines/digital": ["eng_digital.c"],
    "engines/phase": ["eng_phase.c"],
    "engines/lofi": ["eng_lofi.c"],
    "engines/sample": ["eng_sample.c"],
    "engines/formant": ["eng_formant.c"],
    "engines/trio": ["eng_trio.c"],
    "engines/drawbar": ["eng_drawbar.c"],
    "engines/grain": ["eng_grain.c"],
    "engines/super": ["eng_super.c"],
    "engines/fm6": ["eng_fm6.c", "eng_fm6_rom.h", "fm6_store.c"],
    "engines/slice": ["eng_slice.c"],
    "engines/cz": ["eng_cz.c", "cz_native.c"],
    "engines/phys": ["eng_phys.c", "phys_dsp.c", "phys_symp.c"],
    "engines/acid": ["eng_acid.c", "acid/acid_dsp.c", "acid/bass303.c", "acid/bass303.h", "acid/fastmath.h",
                     "acid/x0x_param.h"],
    "dsp": ["dsp.c", "dsp_common.h", "dsp_float.h", "x0x_param.h"],
    "fx": ["fx.c"],
    "fx/reverb": ["rev_type.c", "rev_math.c", "reverb_alt.c", "reverb_airwin.c"],
    "fx/master_comp": ["master_comp.c", "master_comp.h"],
    "fx/slicer": ["slicer.c"],
    "fx/spring": ["spring.c"],
    "fx/bassplus": ["bassplus.c"],
    "fx/punch": ["punch.c"],
    "drums": ["drums.c", "drum_edit.c", "drum_sends.c", "drum_kits.c"],
    "drums/synth": ["drum_synth.c"],
    "drums/x0x": ["drum_x0x.c", "x0x/x0x_drums.c", "x0x/drum808.c", "x0x/drum808.h", "x0x/drum909.c",
                  "x0x/drum909.h", "x0x/drum909_dsp.h", "x0x/fastmath.h", "x0x/x0x_param.h"],
    "display": ["lcd.c", "lcd_dirty.c", "gfx.c"],
    "ui": ["panel.c", "lights.c", "knob_accel.h", "splash.c", "meters.c"],
    "ui/sloop": ["ui.c", "ui_input.c", "ui_layers.c", "ui_draw.c", "ui_menu.c", "ui_overview.c", "ui_song.c",
                 "ui_studio.c", "ui_drums.c", "ui_fm6.c", "macro_ui.c", "keylit.c", "bright.c", "icons.c",
                 "param_help.c", "ui_colors.c", "ui_vis.c", "ui_drumstep.c"],
    "io": ["console.c"],
    "io/usb": ["usb.c", "usb_audio.c", "usb_audio_stream.c", "usb_audio_desc.h"],
    "io/midi": ["midi_control.c", "midi_uart.c", "clock_sync.c"],
    "io/editor": ["editor.c", "ed_backup.c", "ed_drums.c", "ed_dsend.c", "ed_dsrc.c", "ed_pages.c", "ed_snap.c",
                  "ed_status.c", "ed_macro.c", "ed_sl24.c", "ed_steps.c", "ed_stepx.c", "ed_sync9.c",
                  "ed_user.c", "ed_cz.c"],
    "system": ["main.c", "libc.c", "bootguard.h", "cpuguard.c", "cpuguard.h", "cpuguard_costs.h", "ota.c",
               "recovery.c", "dual.c", "bench.c", "simd_probe.c"],
}
STAY = {"felucca.c"}
DIRS = {"acid/": "engines/acid/", "x0x/": "drums/x0x/"}     # whole folders that move (for text references)
SCAN = ("firmware/", "tests/")                              # where quoted #includes are rewritten
C_EXT = (".c", ".h", ".cc", ".S")


def git(*a, check=True):
    p = subprocess.run(["git", "-C", str(ROOT), *a], capture_output=True, text=True)
    if check and p.returncode:
        raise SystemExit(f"git {' '.join(a)}: {p.stderr.strip()}")
    return p.stdout


def mapping():
    """{old path under firmware/src: new path under firmware/src}, for the files that exist in this tree"""
    m, seen = {}, {}
    for d, files in LAYOUT.items():
        for f in files:
            new = f"{d}/{posixpath.basename(f)}"
            if new in seen:
                raise SystemExit(f"LAYOUT: {f} and {seen[new]} both become {new}")
            seen[new] = f
            if (ROOT / SRC / f).exists():
                m[f] = new
    return m


def tracked():
    return [ln for ln in git("ls-files", "-z").split("\0") if ln]


def cmd_check():
    m = mapping()
    have = {p[len(SRC) + 1:] for p in tracked() if p.startswith(SRC + "/")}
    missing = sorted(have - set(m) - STAY)
    absent = sorted(f for fs in LAYOUT.values() for f in fs if f not in have and not (ROOT / SRC / f).exists())
    for f in absent:
        print(f"check: in LAYOUT, not in this tree (fine on an older tree): {f}")
    for f in missing:
        print(f"check: NO PLACE in LAYOUT: firmware/src/{f}")
    print(f"check: {len(m)} files to move, {len(missing)} without a place")
    return 1 if missing else 0


def cmd_plan():
    m = mapping()
    print("| Old (`firmware/src/`) | New (`firmware/src/`) |\n|---|---|")
    for d, files in LAYOUT.items():
        for f in files:
            if f in m:
                print(f"| `{f}` | `{m[f]}` |")
    return 0


# ---- #include rewriting

INC = re.compile(r'^([ \t]*#[ \t]*include[ \t]*")([^"]+)(")', re.M)


def resolve(includer, name, exists):
    """the repo path a quoted #include names: the includer's folder first, then -Ifirmware/hal -Ifirmware/src
    (tools/build.py's order; build/gen holds only generated headers, never a tracked file)"""
    for d in (posixpath.dirname(includer), HAL, SRC):
        p = posixpath.normpath(posixpath.join(d, name))
        if exists(p):
            return p
    return None


def spelling(new_includer, target, old_name):
    """how new_includer names target: relative to the includer's folder, as the flat tree's sibling includes were,
    so no unit needs an -I it did not need before (some host tests build without -Ifirmware/src); a HAL header
    named by its bare name (found through -Ifirmware/hal) keeps it"""
    if target.startswith(HAL + "/") and not old_name.startswith("."):
        return old_name
    return posixpath.relpath(target, posixpath.dirname(new_includer))


def cmd_move():
    if cmd_check():
        raise SystemExit("move: give every file a place in LAYOUT first")
    if git("status", "--porcelain", "--untracked-files=no").strip():
        raise SystemExit("move: the tree has uncommitted changes")
    m = mapping()
    old_files = set(tracked())
    new_of = {f"{SRC}/{o}": f"{SRC}/{n}" for o, n in m.items()}
    new_files = {new_of.get(f, f) for f in old_files}
    # every include, resolved in the old tree, spelled for the new one
    plans, errors = {}, []
    for f in sorted(old_files):
        if not f.startswith(SCAN) or not f.endswith(C_EXT):
            continue
        text = (ROOT / f).read_text(encoding="utf-8")
        nf = new_of.get(f, f)

        def sub(mo, f=f, nf=nf):
            name = mo.group(2)
            target = resolve(f, name, old_files.__contains__)
            if target is None:                 # a generated header (build/gen) or a host header: unchanged,
                if resolve(nf, name, new_files.__contains__):          # but it must not hit a tracked file now
                    errors.append(f"{nf}: \"{name}\" now resolves to a tracked file")
                return mo.group(0)
            nt = new_of.get(target, target)
            spell = spelling(nf, nt, name) if (nt != target or nf != f) else name
            if resolve(nf, spell, new_files.__contains__) != nt:
                errors.append(f"{nf}: \"{spell}\" does not resolve to {nt}")
            return mo.group(1) + spell + mo.group(3)
        new_text = INC.sub(sub, text)
        if new_text != text:
            plans[nf] = new_text
    if errors:
        raise SystemExit("move: include check failed:\n  " + "\n  ".join(errors))
    for o, n in m.items():
        (ROOT / SRC / n).parent.mkdir(parents=True, exist_ok=True)
        git("mv", f"{SRC}/{o}", f"{SRC}/{n}")
    for nf, text in plans.items():
        (ROOT / nf).write_text(text, encoding="utf-8")
    git("add", "-A", "--", "firmware", "tests")
    for d in sorted({posixpath.dirname(o) for o in m if "/" in o}, reverse=True):
        p = ROOT / SRC / d
        if p.is_dir() and not any(p.iterdir()):
            p.rmdir()
    print(f"move: {len(m)} files moved, includes rewritten in {len(plans)} files; review, then commit alone")
    return 0


# ---- text references and the paths the tools build in code

REF = re.compile(r"(?<![\w-])(?<![\w-]/)firmware/src/([\w./-]*[\w/])")
# files that name our sources as src/<file> (relative to firmware/): the divide audit's keys, two licence notes, a doc
SRC_REL_FILES = ("tools/div_audit.txt", "LICENSES/MIT-DaisySP.txt", "LICENSES/MIT-Rings.txt", "docs/DUAL-CORE.md")
SRC_REL = re.compile(r"(?<![\w./-])src/([\w./-]*[\w/])")
# X0X's float units: in subfolders (acid/, x0x/) that tools/div_audit.py never scanned (it read firmware/src/*.c only)
FLOAT_UNITS = ("acid/", "x0x/")
# a line naming another project's tree: its firmware/src/... is that project's path, not ours
PROVENANCE = re.compile(r"github\.com|hugelton/|keremimo/|charlesvestal/|flowstate-fm1|sloop-fm1|Melodee 0\.11 \(|"
                        r"Felucca/firmware|\bat (?:v\d|[0-9a-f]{7})\b")
TEXT_EXT = (".py", ".sh", ".md", ".json", ".mjs", ".js", ".c", ".h", ".cc", ".txt", ".html", ".ps1", ".bat",
            ".config", "Makefile")
SKIP_TEXT = ("tools/reorg.py", "docs/SOURCE-LAYOUT.md")
TEXT_ROOTS = ("tools/", "tests/", "web/", "docs/", "firmware/", "LICENSES/", "config/")
TOP_TEXT = ("BUILDING.md", "OPTIMIST.md", "LICENSING.md", "README.md", "CONTRIBUTING.md", "Makefile", "build.sh",
            "build-sloop.ps1")


def new_ref(old, m):
    """firmware/src/<old...> -> the new path, for a file, a moved folder or the longest file prefix"""
    if old in m:
        return m[old]
    for d, nd in DIRS.items():
        if old.rstrip("/") + "/" == d:
            return nd if old.endswith("/") else nd.rstrip("/")
    best = max((k for k in m if old.startswith(k) and not re.match(r"[\w]", old[len(k):len(k) + 1])),
               key=len, default=None)
    return m[best] + old[len(best):] if best else None


def rewrite_refs(path, text, m, report):
    if path in SRC_REL_FILES:
        def sub_rel(mo):
            n = new_ref(mo.group(1), m)
            return f"src/{n}" if n else mo.group(0)
        text = SRC_REL.sub(sub_rel, text)
    out, in_source = [], False
    is_md, is_json = path.endswith(".md"), path.endswith(".json")
    for ln in text.splitlines(keepends=True):
        if is_json and '"source": {' in ln:
            in_source = True
        prov = in_source or bool(PROVENANCE.search(ln))
        if is_json and in_source and ln.strip().startswith("}"):
            in_source = False
        head, body = "", ln
        if is_md and ln.lstrip().startswith("|") and prov:
            cut = ln.rstrip().rstrip("|").rfind("|")              # a table row: only its last cell is ours
            head, body, prov = ln[:cut], ln[cut:], False
        if prov:
            if REF.search(ln):
                report.append(f"{path}: left (provenance): {ln.strip()[:150]}")
            out.append(ln)
            continue

        def sub(mo):
            n = new_ref(mo.group(1), m)
            return f"firmware/src/{n}" if n else mo.group(0)
        out.append(head + REF.sub(sub, body))
    return "".join(out)


# code that builds a source path without spelling it: (file, pattern, replacement); a pattern that matches nothing
# is reported (the tool changed: look at it by hand)
def code_edits(m):
    def py_list(name):            # size_fns.py's SIZE_FILES / AUDIO_FILES: names -> paths under firmware/src
        return (rf'"({"|".join(re.escape(k) for k in m if "/" not in k)})"',
                lambda mo: f'"{m[mo.group(1)]}"', name)
    return [
        ("tools/size_fns.py", *py_list("SIZE_FILES/AUDIO_FILES/OS_FILES")),
        ("tools/size_fns.py", r'sorted\(p\.name for p in SRC\.glob\("\*\.c"\)\)',
         lambda mo: 'sorted(p.relative_to(SRC).as_posix() for p in SRC.rglob("*.c")\n'
                    '                  if p.relative_to(SRC).as_posix() not in FLOAT_UNITS)',
         "size_fns.py --check: every folder, minus the float units"),
        ("tools/size_fns.py", r"(?m)^SKIP = ", lambda mo: float_units(m) + "SKIP = ", "FLOAT_UNITS"),
        ("tools/size_fns.py", r"(?ms)^((?:SIZE|AUDIO|OS)_FILES = )\[(.*?)\]", reflow, "the lists, wrapped"),
        ("tools/build.py", r'FW / "src" / "acid" / "acid_dsp\.c"',
         lambda mo: 'FW / "src" / "engines" / "acid" / "acid_dsp.c"', "the ACID unit"),
        ("tools/build.py", r'FW / "src" / "x0x" / "x0x_drums\.c"',
         lambda mo: 'FW / "src" / "drums" / "x0x" / "x0x_drums.c"', "the X0X unit"),
        ("tools/build.py", r'\(FW / "src"\)\.glob\("\*\.\[ch\]"\)', lambda mo: '(FW / "src").rglob("*.[ch]")',
         "mmio_check: every folder"),
        ("tests/fm1_cpu_test.py", r'ROOT / "firmware" / "src" / "(console|audio|cpuguard)\.c"',
         lambda mo: 'ROOT / "firmware" / "src" / ' + " / ".join(f'"{s}"' for s in m[mo.group(1) + ".c"].split("/")),
         "console.c, audio.c, cpuguard.c"),
        ("tools/builder/cpu_costs.py", r'ROOT / "firmware" / "src" / "(registry\.h|cpuguard_costs\.h)"',
         lambda mo: 'ROOT / "firmware" / "src" / ' + " / ".join(f'"{s}"' for s in m[mo.group(1)].split("/")),
         "registry.h, cpuguard_costs.h"),
        ("tools/div_audit.py", r'\[\*d\.glob\("\*\.c"\), \*d\.glob\("\*\.h"\)\]', lambda mo: 'sources(d)',
         "every folder"),
        ("tools/div_audit.py", r'(?m)^(CONSTS = set\(\)[^\n]*\n)', lambda mo: mo.group(1) + '\n\n' + div_helper(m), "sources()"),
    ]


def float_units(m):
    """size_fns.py --check read firmware/src/*.c; now the folders too, but not the float units (acid/, x0x/ before)"""
    skip = sorted(n for o, n in m.items() if o.startswith(FLOAT_UNITS) and o.endswith(".c"))
    rows, row = [], "               "
    for n in skip:
        item = f'"{n}", '
        if len(row) + len(item) > 116:
            rows.append(row.rstrip())
            row = "               "
        row += item
    rows.append(row.rstrip().rstrip(",") + ")")
    return ("# X0X's float units: units of their own (tools/build.py), outside the unity build and these lists\n"
            "FLOAT_UNITS = (" + "\n".join(rows).lstrip() + "\n")


def reflow(mo):
    """NAME = ["a", "b", ...] wrapped at 116 columns, continuation lines under the first item"""
    head, items = mo.group(1), re.findall(r'"[^"]*"', mo.group(2))
    pad, lines, row = " " * (len(head) + 1), [], head + "["
    for i, it in enumerate(items):
        piece = it + ("," if i < len(items) - 1 else "]")
        if len(row) + len(piece) + 1 > 116 and row.strip() not in (head.strip() + "[",):
            lines.append(row.rstrip())
            row = pad
        row += piece + " "
    lines.append(row.rstrip())
    return "\n".join(lines)


def div_helper(m):
    skip = sorted(n for o, n in m.items() if o.startswith(FLOAT_UNITS))
    rows, row = [], "    "
    for s in skip:
        item = f'"{s}", '
        if len(row) + len(item) > 116:
            rows.append(row.rstrip())
            row = "    "
        row += item
    rows.append(row.rstrip().rstrip(","))
    body = "\n".join(rows)
    return ("# firmware/src is in folders by domain (docs/SOURCE-LAYOUT.md); X0X's float units were in subfolders this\n"
            "# audit never read (it read firmware/src/*.[ch]), and stay out of it: auditing them is a task of its own\n"
            f"NOT_AUDITED = {{\n{body}}}\n\n\n"
            "def sources(d):\n"
            '    """the .c and .h files of d; firmware/src with its folders, without NOT_AUDITED"""\n'
            '    src = ROOT / "firmware" / "src"\n'
            '    if d != src:\n'
            '        return [*d.glob("*.c"), *d.glob("*.h")]\n'
            '    return [f for f in [*d.rglob("*.c"), *d.rglob("*.h")] if f.relative_to(src).as_posix() not in NOT_AUDITED]\n')


def cmd_paths():
    m = mapping()
    if any((ROOT / SRC / o).exists() for o in m):
        raise SystemExit("paths: run `move` (and commit it) first")
    # after the move, mapping() finds nothing at the old places: rebuild it from LAYOUT
    m = {f: f"{d}/{posixpath.basename(f)}" for d, fs in LAYOUT.items() for f in fs
         if (ROOT / SRC / d / posixpath.basename(f)).exists()}
    report, changed = [], []
    for f in tracked():
        if f in SKIP_TEXT or not (f.startswith(TEXT_ROOTS) or f in TOP_TEXT) or not f.endswith(TEXT_EXT):
            continue
        p = ROOT / f
        try:
            text = p.read_text(encoding="utf-8")
        except (UnicodeDecodeError, FileNotFoundError):
            continue
        new = rewrite_refs(f, text, m, report)
        if new != text:
            p.write_text(new, encoding="utf-8")
            changed.append(f)
    for f, pat, rep, what in code_edits(m):
        p = ROOT / f
        text = p.read_text(encoding="utf-8")
        new, n = re.subn(pat, rep, text)
        if not n:
            report.append(f"{f}: NOT FOUND ({what}): look at it by hand")
        elif new != text:
            p.write_text(new, encoding="utf-8")
            changed.append(f)
    for r in report:
        print(r)
    print(f"paths: {len(set(changed))} files changed: " + " ".join(sorted(set(changed))))
    return 0


def cmd_fix():
    files = tracked()
    have = set(files)
    by_name = {}
    for f in files:
        if f.startswith(SRC + "/"):
            by_name.setdefault(posixpath.basename(f), []).append(f)
    changed, left = 0, []
    for f in files:
        if not f.startswith(SCAN) or not f.endswith(C_EXT):
            continue
        text = (ROOT / f).read_text(encoding="utf-8")

        def sub(mo, f=f):
            name = mo.group(2)
            if resolve(f, name, have.__contains__) or name.startswith("."):
                return mo.group(0)
            cand = by_name.get(posixpath.basename(name), [])
            if len(cand) != 1:                  # generated (build/gen), a host header, or ambiguous
                if cand:
                    left.append(f"{f}: \"{name}\" is ambiguous: {', '.join(cand)}")
                return mo.group(0)
            return mo.group(1) + posixpath.relpath(cand[0], posixpath.dirname(f)) + mo.group(3)
        new = INC.sub(sub, text)
        if new != text:
            (ROOT / f).write_text(new, encoding="utf-8")
            changed += 1
            print(f"fix: {f}")
    print("\n".join(left))
    print(f"fix: {changed} files changed. New files of the branch still at the old place: `git mv` them into their"
          " folder (docs/SOURCE-LAYOUT.md), add them to LAYOUT, run `fix` again")
    return 0


def main(argv):
    os.chdir(ROOT)
    cmds = {"check": cmd_check, "plan": cmd_plan, "move": cmd_move, "paths": cmd_paths, "fix": cmd_fix}
    if len(argv) != 1 or argv[0] not in cmds:
        print(__doc__)
        return 2
    return cmds[argv[0]]()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
