#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Ported from Felucca 1.0.1 (https://github.com/hugelton/Felucca, tools/size_fns.py at 20c275e,
# Leo Kuroshita) into SLOOP-plus: our file list, a definition finder for any return type, and a
# guard that never marks a function the sound code can reach (audio stays at -Os, in RAM where it is).
"""Build the main-loop code for size (build.py): the functions defined in SIZE_FILES get LLVM's
minsize (about -Oz) while everything else (the sound, render and ISR code, the flash and OTA code,
the main loop) keeps -Os. The JieLi clang 4 has no '#pragma clang attribute' and its minsize
attribute is an error on variables, so build.py compiles felucca.c to LLVM IR without the
optimizer, this script adds 'minsize' to those definitions, and the IR is then compiled at -Os
(without the edit, that round trip is byte-identical to compiling felucca.c directly).

Never marked, whatever file defines them: functions placed in RAM (a section attribute: HOT,
.ram_text) and every function the audio side can reach through direct calls or address
references. The audio side is the IRQ bodies (*_irq), the RAM code and every function defined in
AUDIO_FILES.

  size_fns.py IN.ll OUT.ll        mark and write OUT.ll
  size_fns.py --none IN.ll OUT.ll copy without marks (to check the round trip)
  size_fns.py --check             every firmware/src/*.c in one list (tests/run_tests.sh)"""
import re
import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parents[1]
SRC = _ROOT / "firmware" / "src"
# the UI, the stores, the editor and the console: main loop only
SIZE_FILES = ["ui.c", "ui_drums.c", "ui_colors.c", "ui_song.c", "ui_studio.c", "ui_fm6.c", "icons.c", "ui_draw.c",
              "ui_overview.c", "ui_drumstep.c", "ui_layers.c", "ui_menu.c", "ui_input.c", "splash.c",
              "storage.c", "upreset.c", "project.c", "arranger_scene.c", "drum_kits.c", "fm6_store.c",
              "editor.c", "ed_drums.c", "ed_backup.c", "console.c", "sec_log.c", "sections.c", "sec_codec.c",
              "ed_dsend.c", "ed_dsrc.c", "ed_macro.c", "ed_pages.c", "ed_snap.c", "ed_status.c", "ed_steps.c",
              "ed_user.c", "bp_set.c", "macro_ui.c", "param_help.c", "panel.c", "lights.c", "keylit.c",
              "settings_word.c", "miss.c", "undo.c", "drum_store.c", "motion_proj.c", "stepx_log.c", "stepx_proj.c",
              "snapshots.c", "snap_store.c", "sl24_guard.c", "sl24_import.c", "ed_sync9.c"]
# kept at -Os on purpose: boot and main loop, flash / OTA / USB, drawing primitives, libc, sound-side helpers,
# optional engines and effects (a new main-loop-only file goes in SIZE_FILES: --check, docs SLIM-CODE.md)
OS_FILES = ["felucca.c", "main.c", "recovery.c", "ota.c", "usb.c", "usb_audio.c", "motion_flash.c", "lcd.c",
            "lcd_dirty.c", "gfx.c", "libc.c", "bench.c", "simd_probe.c", "cpuguard.c", "bright.c", "meters.c",
            "motion.c", "macro.c", "master_comp.c", "seq_midi.c", "drum_sends.c", "chance.c", "qnt_seq.c",
            "bassplus.c", "spring.c", "reverb_alt.c", "rev_type.c", "eng_acid.c", "eng_cz.c", "cz_native.c",
            "eng_phys.c", "phys_dsp.c", "phys_symp.c"]
# the sound side: what the audio ISR, the second core and the voices run (never minsize)
AUDIO_FILES = ["engines.c", "dsp.c", "eng_analog.c", "eng_analog2.c", "eng_digital.c", "eng_phase.c",
               "eng_lofi.c", "eng_sample.c", "eng_formant.c", "eng_trio.c", "eng_drawbar.c", "eng_grain.c",
               "eng_super.c", "eng_fm6.c", "eng_slice.c", "drums.c", "drum_synth.c", "drum_edit.c", "drum_x0x.c",
               "params.c", "voice.c", "slicer.c", "fx.c", "punch.c", "dual.c", "audio.c", "seq.c",
               "arranger.c", "midi_control.c", "clock_sync.c", "midi_uart.c", "usb_audio_stream.c"]
SKIP = {"if", "for", "while", "switch", "return", "sizeof", "typedef", "else", "do", "case"}
DEF = re.compile(r"^(?!#)[A-Za-z_][\w \t*]*?\b([A-Za-z_]\w*)\s*\(", re.M)
IR_DEF = re.compile(r"^(define [^\n]*?@\"?([\w.]+)\"?\([^\n]*\)(?: unnamed_addr| local_unnamed_addr)?)( #\d+[^\n]*\{)$",
                    re.M)
IR_REF = re.compile(r"@\"?([\w.]+)\"?")


def definitions(text):
    """names of the functions text defines (a signature at column 0 whose ')' is followed by '{')"""
    names = []
    text = re.sub(r"/\*.*?\*/|//[^\n]*", " ", text, flags=re.S)        # comments (none hold code)
    text = re.sub(r"\b__attribute__\s*\(\((?:[^()]|\([^()]*\))*\)\)", " ", text)   # 'static __attribute__((noinline)) int f('
    for m in DEF.finditer(text):
        if m.group(1) in SKIP or re.match(r"(typedef|return)\b", m.group(0)):
            continue
        i, depth = m.end() - 1, 0
        while i < len(text):
            depth += {"(": 1, ")": -1}.get(text[i], 0)
            i += 1
            if depth == 0:
                break
        rest = re.sub(r"^(__attribute__\s*\(\(.*?\)\)\s*)+", "", text[i:i + 200].lstrip())
        if rest.startswith("{"):
            names.append(m.group(1))
    return names


def names_in(files):
    names = set()
    for f in files:
        p = SRC / f
        if p.exists():
            names.update(definitions(p.read_text()))
    return names


def ir_functions(ir):
    """{name: (define line, body text)} for every function the IR defines"""
    funcs = {}
    for m in re.finditer(r"^define [^\n]*?@\"?([\w.]+)\"?\([^\n]*\{\n(.*?)^\}", ir, re.M | re.S):
        funcs[m.group(1)] = (m.group(0).split("\n", 1)[0], m.group(2))
    return funcs


def audio_reach(funcs, audio_names):
    """every IR function the sound side can reach (direct calls and address references)"""
    roots = {n for n, (head, _) in funcs.items()
             if n.endswith("_irq") or n in audio_names or re.search(r'section "\.ram', head)}
    seen, todo = set(roots), list(roots)
    while todo:
        for ref in IR_REF.findall(funcs[todo.pop()][1]):
            if ref in funcs and ref not in seen:
                seen.add(ref)
                todo.append(ref)
    return seen


def check():
    """every firmware/src/*.c in exactly one list: a new main-loop file left out costs flash unseen"""
    lists = SIZE_FILES + AUDIO_FILES + OS_FILES
    have = sorted(p.name for p in SRC.glob("*.c"))
    bad = [f"{f}: in no list (SIZE_FILES for main-loop code)" for f in have if f not in lists]
    bad += [f"{f}: in two lists" for f in sorted(set(lists)) if lists.count(f) > 1]
    bad += [f"{f}: listed, not in firmware/src" for f in sorted(set(lists)) if f not in have]
    print("\n".join(bad) or f"size: {len(have)} sources, each in one list")
    return 1 if bad else 0


def main(argv):
    if argv == ["--check"]:
        return check()
    none = argv[:1] == ["--none"]
    src, dst = argv[1:3] if none else argv[0:2]
    ir = Path(src).read_text()
    if none:
        Path(dst).write_text(ir)
        print("size: IR round trip only (no function marked)")
        return 0
    funcs = ir_functions(ir)
    want = names_in(SIZE_FILES)
    keep = audio_reach(funcs, names_in(AUDIO_FILES))
    held = sorted(n for n in want if n in keep and n in funcs)
    hit = set()

    def mark(m):
        name = m.group(2)
        if name not in want or name in keep:
            return m.group(0)
        hit.add(name)
        return m.group(1) + " minsize" + m.group(3)
    Path(dst).write_text(IR_DEF.sub(mark, ir))
    print(f"size: {len(hit)} main-loop functions built for size "
          f"({len(held)} kept at -Os: the sound side reaches them; {len(want - set(funcs))} not in this build)")
    if held:
        print("size: kept at -Os: " + " ".join(held))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
