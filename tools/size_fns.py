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
SIZE_FILES = ["ui/sloop/ui.c", "ui/sloop/ui_drums.c", "ui/sloop/ui_colors.c", "ui/sloop/ui_song.c",
              "ui/sloop/ui_studio.c", "ui/sloop/ui_fm6.c", "ui/sloop/icons.c", "ui/sloop/ui_draw.c",
              "ui/sloop/ui_overview.c", "ui/sloop/ui_drumstep.c", "ui/sloop/ui_layers.c", "ui/sloop/ui_pat.c", "ui/sloop/ui_menu.c",
              "ui/sloop/ui_input.c", "ui/splash.c", "storage/storage.c", "storage/upreset.c", "storage/project.c",
              "seq/arranger_scene.c", "drums/drum_kits.c", "engines/fm6/fm6_store.c", "io/editor/editor.c",
              "io/editor/ed_drums.c", "io/editor/ed_backup.c", "io/editor/ed_cz.c", "storage/nbank.c",
              "io/console.c", "storage/sections/sec_log.c", "storage/sections/sections.c",
              "storage/sections/sec_codec.c", "io/editor/ed_dsend.c", "io/editor/ed_dsrc.c",
              "io/editor/ed_macro.c", "io/editor/ed_pages.c", "io/editor/ed_snap.c", "io/editor/ed_status.c",
              "io/editor/ed_steps.c", "io/editor/ed_user.c", "core/bp_set.c", "ui/sloop/macro_ui.c",
              "ui/sloop/param_help.c", "ui/panel.c", "ui/lights.c", "ui/sloop/keylit.c", "storage/settings_word.c",
              "storage/miss.c", "seq/undo.c", "storage/drum_store.c", "storage/motion_proj.c",
              "storage/sections/stepx_log.c", "storage/sections/pat.c", "storage/stepx_proj.c", "storage/snapshots/snapshots.c",
              "storage/snapshots/snap_store.c", "storage/sl24/sl24_guard.c", "storage/sl24/sl24_import.c",
              "io/editor/ed_sync9.c", "io/editor/ed_stepx.c", "io/editor/ed_pat.c", "io/editor/ed_sl24.c", "storage/sl24/sl24_export.c",
              "fx/fx_slots.c",
              "fx/fx_rec.c", "fx/fx_rec_log.c", "core/model.c", "drums/dsnd_desc.c",
              "ui/optimist/optimist.c", "ui/optimist/op_state.c", "ui/optimist/op_cells.c", "ui/optimist/op_screens.c",
              "ui/optimist/op_step.c", "ui/optimist/op_project.c", "ui/optimist/op_graph.c", "ui/optimist/op_draw.c",
              "ui/optimist/op_stepdraw.c", "ui/optimist/op_input.c"]
# kept at -Os on purpose: boot and main loop, flash / OTA / USB, drawing primitives, libc, sound-side helpers,
# optional engines and effects (a new main-loop-only file goes in SIZE_FILES: --check, docs SLIM-CODE.md)
OS_FILES = ["felucca.c", "system/main.c", "system/recovery.c", "system/ota.c", "io/usb/usb.c",
            "io/usb/usb_audio.c", "storage/motion_flash.c", "display/lcd.c", "display/lcd_dirty.c",
            "display/gfx.c", "system/libc.c", "system/bench.c", "system/simd_probe.c", "system/cpuguard.c",
            "ui/sloop/bright.c", "ui/meters.c", "seq/motion.c", "core/macro.c", "fx/master_comp/master_comp.c",
            "seq/seq_midi.c", "drums/drum_sends.c", "seq/chance.c", "seq/qnt_seq.c", "fx/bassplus/bassplus.c",
            "fx/spring/spring.c", "fx/reverb/reverb_alt.c", "fx/reverb/rev_type.c", "engines/acid/eng_acid.c",
            "engines/cz/eng_cz.c", "engines/cz/cz_native.c", "engines/phys/eng_phys.c", "engines/phys/phys_dsp.c",
            "engines/phys/phys_symp.c", "fx/reverb/rev_math.c", "fx/reverb/reverb_airwin.c", "ui/sloop/ui_vis.c"]
# the sound side: what the audio ISR, the second core and the voices run (never minsize)
AUDIO_FILES = ["engines/engines.c", "dsp/dsp.c", "engines/analog/eng_analog.c", "engines/analog/eng_analog2.c",
               "engines/digital/eng_digital.c", "engines/phase/eng_phase.c", "engines/lofi/eng_lofi.c",
               "engines/sample/eng_sample.c", "engines/formant/eng_formant.c", "engines/trio/eng_trio.c",
               "engines/drawbar/eng_drawbar.c", "engines/grain/eng_grain.c", "engines/super/eng_super.c",
               "engines/fm6/eng_fm6.c", "engines/slice/eng_slice.c", "drums/drums.c", "drums/synth/drum_synth.c",
               "drums/drum_edit.c", "drums/x0x/drum_x0x.c", "core/params.c", "core/voice.c", "fx/slicer/slicer.c",
               "fx/fx.c", "fx/punch/punch.c", "system/dual.c", "core/audio.c", "seq/seq.c", "seq/arranger.c",
               "io/midi/midi_control.c", "io/midi/clock_sync.c", "io/midi/midi_uart.c",
               "io/usb/usb_audio_stream.c", "seq/seq24.c"]
# X0X's float units: units of their own (tools/build.py), outside the unity build and these lists
FLOAT_UNITS = ("drums/x0x/drum808.c", "drums/x0x/drum909.c", "drums/x0x/x0x_drums.c", "engines/acid/acid_dsp.c",
               "engines/acid/bass303.c")
# X0X's float units: units of their own (tools/build.py), outside the unity build and these lists
FLOAT_UNITS = ("drums/x0x/drum808.c", "drums/x0x/drum909.c", "drums/x0x/x0x_drums.c", "engines/acid/acid_dsp.c",
               "engines/acid/bass303.c")
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
    have = sorted(p.relative_to(SRC).as_posix() for p in SRC.rglob("*.c")
                  if p.relative_to(SRC).as_posix() not in FLOAT_UNITS)
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
