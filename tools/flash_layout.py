# SPDX-License-Identifier: GPL-3.0-only
"""The FM-1 flash layouts, the MEMORY steps (docs/SD-MODE-DESIGN.md section 9): the ONE table of where each step's app
slot ends and where it keeps what it moves. Everything else takes it from here:

  firmware/hal/fm1_steps.h   generated from this table (python3 tools/flash_layout.py --header): fm1_flash_map.h's
                             per-layout map, the update loader's switch clear (ldr_core.c), the updater's size check
                             (ota.c step 1)
  tools/build.py             app.ld's XIP length, the build's flash limit; tools/fm1pkg_make.py the package's flash.bin
  tools/fm1_layouts.py, fm1_rescue.py, fm1_route.py, fm1_install.py   the installers' sizes and clears
  web/install_route.js, ios/OptimistMirror/Sources/Flash/InstallRoute.swift, tools/builder/registry.py LAYOUT_STEPS
                             copies, checked against this table by tests/flash_layout_test.py (the web page and the
                             iOS app cannot import it)

The builder's three MEMORY choices (docs/SD-MODE-DESIGN.md 9.7): SD CARD MODE (L1 with the card), else SAMPLE SLOTS
(0, 1: USR3, 2: USR2 + USR3) x PROJECTS (2, 4, 16). Each pair is one step: the slots kept stay where normal has them,
and under them, top down, the project log, autosave copy A, the FM6 voices; the app ends there (`stacked`).

  0  N   normal           2 slots, 16 projects: the app slot [0x4000, 0x93000), the store as before
  1  L1  more flash       0 slots, 2 projects: [0x4000, 0xD0000), the store squeezed into 0xD0000..0xD9FFF; SD card mode
  3  S0  no user samples  0 slots, 16 projects: [0x4000, 0xB5000), above it the FM6 voices, autosave A, a 32-sector log
"""
from __future__ import annotations

from dataclasses import dataclass

APP_LO = 0x4000
APP_HEAD = 0x120          # the app area's own directory before app.bin (fm1pkg_make.py app_area_head)
APP_TAIL = 0xF24          # after app.bin: the SDK's cfg_tool.bin and the cfg directory (eq_cfg_hw.bin)
SECTOR = 0x1000
STORE_TOP = 0xE0000       # the update staging starts here: no layout's app or store passes it
CLEAR_TOP = 0xDA000       # no switch clears past here: the kits (0xDA000), the presets (0xDC000) and up are kept


@dataclass(frozen=True)
class Step:
    layout: int           # FELUCCA_LAYOUT, the LAYOUT builder item
    key: str              # the design's name (N, S0, L1)
    name: str             # MEMORY's word for it
    end: int              # the classic build's app slot end: flash.bin is [0, end)
    reloc: int            # R, the top of what this step stores elsewhere than normal (normal: its own end)
    release_end: int      # fm1-base's (the release's) app end for this step
    release_built: bool   # the release builds it (S0: not until fm1-base's loader takes the table)
    upf: int              # the presets' FM6 voices, A / B (two sectors)
    data_lo: int          # where Optimist's store starts (FL_DATA_LO)
    autosave_a: int       # the working project's autosave copy A (copy B 0xFE000 in every step)
    log_lo: int           # the project log (sec_log.c SEC_LOG_BASE)
    log_sectors: int      # its sectors; 0: the build's rule (normal: 20 with user samples, else 32)
    slots: int            # the user sample slots kept (USR3 1, USR2 + USR3 2; with USR3 the snapshots and the CZ
                          # collection); normal 2 (3 without PATTERNS: eng_sample.c)
    projects: int         # the projects the flash keeps (registry.h PROJ_SLOTS; SD card mode 1)


SLOT_TOP = {0: 0xD8000, 1: 0xC8000, 2: 0xB4000}   # where the kept slots start (0: OBJ_PANEL's 0xD8000, the release's)
LOG_SECTORS = {2: 7, 4: 11, 16: 32}               # the project log by PROJECTS (about 8 KiB a project, 16 the tightest)


def stacked(layout: int, key: str, name: str, slots: int, projects: int) -> Step:
    """a step of the grid (docs/SD-MODE-DESIGN.md 9.7.1): under the kept slots the log, autosave A, the FM6 voices;
    the app ends at the FM6 voices; the release's end the same (OBJ_PANEL stays at 0xD8000)"""
    top, n = SLOT_TOP[slots], LOG_SECTORS[projects]
    log_lo = top - n * SECTOR
    end = log_lo - 3 * SECTOR
    return Step(layout, key, name, end, top, end, False, end, end, log_lo - SECTOR, log_lo, n, slots, projects)


STEPS = {
    0: Step(0, "N", "normal", 0x93000, 0x93000, 0x93000, True,
            0x95000, 0x97000, 0x9F000, 0xA0000, 0, 2, 16),
    1: Step(1, "L1", "more flash", 0xD0000, 0xDA000, 0xCE000, True,
            0xD8000, 0xD0000, 0xD7000, 0xD0000, 7, 0, 2),
    3: stacked(3, "S0", "no user samples", 0, 16),
}
ORDER = (0, 3, 1)         # by size: the builder's MEMORY choices and the tables list them so

APP_HI = {k: s.end for k, s in STEPS.items()}
NAMES = {k: s.name for k, s in STEPS.items()}
BIG = max(APP_HI.values())          # the largest step's end (fm1_flash_map.h FL_APP_HI_BIG)


def _check_table():
    ends = [s.end for s in STEPS.values()]
    assert len(set(ends)) == len(ends), "two steps with one end: an installed app's end must name its step"
    for s in STEPS.values():
        for v in (s.end, s.reloc, s.release_end, s.upf, s.data_lo, s.autosave_a, s.log_lo):
            assert v % SECTOR == 0, f"{s.key}: {v:#x} not a 4 KiB boundary"
        assert APP_LO < s.end <= s.reloc <= CLEAR_TOP and s.release_end <= s.reloc, s.key
        if s.layout:        # a relocated store: FM6 voices, autosave A and the log stacked, from the app's end up, to R
            log_hi = s.log_lo + s.log_sectors * SECTOR
            parts = sorted([(s.upf, s.upf + 2 * SECTOR), (s.autosave_a, s.autosave_a + SECTOR), (s.log_lo, log_hi)])
            assert all(a[1] <= b[0] for a, b in zip(parts, parts[1:])), f"{s.key}: its store's parts overlap"
            assert parts[0][0] >= s.end and parts[-1][1] <= s.reloc and s.data_lo == min(s.end, s.log_lo), s.key
            assert s.log_sectors and s.reloc <= (CLEAR_TOP if s.slots == 0 else SLOT_TOP[s.slots]), s.key
            assert s.slots == 0 or s.reloc == SLOT_TOP[s.slots], f"{s.key}: its kept slots stay where normal has them"
    rel = {s.release_end for s in STEPS.values()} - {s.end for s in STEPS.values()}
    assert not rel & set(ends), "a classic end equal to another step's release end"


_check_table()


def step(layout: int) -> Step:
    if layout not in STEPS:
        raise ValueError(f"flash layout {layout}: one of {sorted(STEPS)}")
    return STEPS[layout]


def app_hi(layout: int) -> int:
    """the app slot's end: flash.bin is [0, app_hi)"""
    return step(layout).end


def slot_of(end: int) -> int:
    """app.bin's room in a slot ending at end"""
    return end - APP_LO - APP_HEAD - APP_TAIL


def app_slot(layout: int) -> int:
    """app.bin's room (app.ld's XIP length, the build's flash limit): 581,564 B in the normal layout"""
    return slot_of(app_hi(layout))


def code_limit(layout: int) -> int:
    """how far app.bin's code and boot stream may reach: the whole slot in every step (the far probe's 64 KiB
    reserve in LAYOUT 1 is retired: the user, 2026-10-29, "drop the far probe, we don't need it")"""
    return app_slot(layout)


def layout_of(flash_len: int) -> int:
    """the layout whose flash.bin is flash_len bytes long (a package's flash entry)"""
    for k, v in APP_HI.items():
        if v == flash_len:
            return k
    raise ValueError(f"a flash.bin of {flash_len:#x} B matches no layout ({', '.join(hex(v) for v in APP_HI.values())})")


def known_ends() -> dict:
    """{app end: (layout, R)} of every firmware of ours an FM-1 may have installed: the classic steps' and the
    release's (fm1-base's app_end: the release's L1 ends at 0xCE000, the same step as the classic 0xD0000)"""
    out = {}
    for s in STEPS.values():
        out.setdefault(s.end, (s.layout, s.reloc))
        out.setdefault(s.release_end, (s.layout, s.reloc))
    return dict(sorted(out.items()))


def own_slots() -> tuple:
    """the flash.bin sizes Optimist's own updater takes (ota.c step 1) and its loaders write: the classic steps' ends"""
    return tuple(sorted(APP_HI.values()))


def switch_clear_ends(old_end: int, new_end: int) -> tuple:
    """the rule of every switch (docs/SD-MODE-DESIGN.md 9.3): an install of the app ending at new_end (a known end,
    classic or release) over an app ending at old_end writes [0x4000, new_end) and first erases
    [new_end, max(R_old, R_new)); between two builds of one step (own <-> release, L1's 0xD0000 <-> 0xCE000) only the
    old code's tail [new_end, old_end), the stores being the same. old_end 0 (no app header of ours), or an end at
    or below normal's that no step has: as after normal; an end above it no step has (a newer step): [new_end,
    CLEAR_TOP), the safe side. (0, 0): nothing to clear"""
    ends = known_ends()
    if new_end not in ends:
        raise ValueError(f"app end {new_end:#x}: no step's ({', '.join(hex(e) for e in ends)})")
    new_layout, new_reloc = ends[new_end]
    old_end = (old_end + SECTOR - 1) & ~(SECTOR - 1)   # (an app area head's end: its slot's end, the tail under a sector)
    if old_end in ends:
        old_layout, old_reloc = ends[old_end]
    elif old_end <= STEPS[0].end:                     # (no header, or one no slot of ours ends at: as after normal)
        old_layout, old_reloc = 0, STEPS[0].reloc
    else:
        old_layout, old_reloc = None, CLEAR_TOP
    if old_layout == new_layout:
        return (new_end, old_end) if old_end > new_end else (0, 0)
    hi = min(max(old_reloc, new_reloc), CLEAR_TOP)
    return (new_end, hi) if hi > new_end else (0, 0)


BACKUP_WHERE = "Back up first: web editor > Backup."


def switch_loss(old_end: int | None, new_end: int) -> tuple:
    """what an install of the app ending at new_end erases of the user's data after the app ending at old_end, by the
    switch rule (docs/SD-MODE-DESIGN.md 9.7.2): () for the same step (own <-> release in one step too); the projects in
    flash and the user presets' FM6 voices whenever the step changes (the log, autosave A and the voices move); the user
    sample slots the old step kept and the new one does not (USR2; USR3 with the snapshots and the CZ collection).
    old_end None: not known (the words of a change from normal)"""
    ends = known_ends()
    if new_end not in ends:
        raise ValueError(f"app end {new_end:#x}: no step's")
    new = STEPS[ends[new_end][0]]
    old_up = None if old_end is None else (old_end + SECTOR - 1) & ~(SECTOR - 1)
    old = STEPS[ends[old_up][0]] if old_up in ends else STEPS[0]
    if old_up in ends and old.layout == new.layout:
        return ()
    out = ["the projects in flash", "the FM6 voices of the user presets"]
    usr2, usr3 = old.slots >= 2 > new.slots, old.slots >= 1 > new.slots
    if usr2 and usr3:
        out.append("the user samples (USR2, USR3) and the snapshots")
    elif usr3:
        out.append("the user samples in USR3 and the snapshots")
    elif usr2:
        out.append("the user samples in USR2")
    return tuple(out)


def switch_warning(old_end: int | None, new_end: int) -> str:
    """the one line every installer and the builder show before a switch (the user, 2026-10-31: "when we flash a
    different layout, we warn the user to backup their data first"); "" when nothing is erased. old_end None: the
    installed step is not known: the line says so"""
    loss = switch_loss(old_end, new_end)
    if not loss:
        return ""
    what = ", ".join(loss[:-1]) + " and " + loss[-1] if len(loss) > 1 else loss[0]
    name = STEPS[known_ends()[new_end][0]].name
    if old_end is None:
        return f"If the FM-1 has another memory layout than this firmware's ({name}), installing it erases {what}. {BACKUP_WHERE}"
    return f"New memory layout ({name}): installing it erases {what}. {BACKUP_WHERE}"


def switch_clear(old_end: int, new_layout: int) -> tuple:
    """the sectors [lo, hi) a switch to the classic new_layout clears before the new app is written (ldr_core.c
    ldr_layout_switch). old_end: where the installed app area ends (its app_area_head; rounded up to a sector here)"""
    return switch_clear_ends(old_end, app_hi(new_layout))


def _h(v: int) -> str:
    return f"0x{v:08X}u"


def c_header() -> str:
    """firmware/hal/fm1_steps.h, made from the table"""
    lines = ["/* SPDX-License-Identifier: GPL-3.0-only",
             " * Copyright (C) 2026 Henri David */",
             "/* GENERATED by tools/flash_layout.py --header from its STEPS table (the MEMORY steps, docs/SD-MODE-DESIGN.md",
             " * section 9): do not edit; tests/flash_layout_test.py fails when this file is not what the table makes.",
             " * Included by fm1_flash_map.h, after FELUCCA_LAYOUT has its value. */",
             "#pragma once",
             "",
             "/* every classic step: X(layout, app end, R = the top of what it stores elsewhere than normal) */",
             "#define FL_STEPS(X) " + " ".join(f"X({s.layout}, {_h(s.end)}, {_h(s.reloc)})" for s in
                                               sorted(STEPS.values(), key=lambda s: s.end)),
             "/* every app end an installed firmware of ours may have (the classic steps' and the release's): X(end, its step's",
             " * layout, its R): the update loader's switch clear (ldr_core.c ldr_layout_switch) */",
             "#define FL_ENDS(X) " + " ".join(f"X({_h(e)}, {l}, {_h(r)})" for e, (l, r) in known_ends().items()),
             f"#define FL_CLEAR_TOP {_h(CLEAR_TOP)}                 /* no switch clears at or past it: kits, presets, settings */",
             f"#define FL_APP_HI_NORMAL {_h(STEPS[0].end)}",
             f"#define FL_APP_HI_BIG {_h(BIG)}                /* the largest step's app end */",
             f"#define FL_APP_LO {_h(APP_LO)}                    /* the app slot's start (fm1pkg_make.py, ldr_core.c) */",
             ""]
    first = True
    for s in sorted(STEPS.values(), key=lambda s: s.layout):
        lines.append(f"#{'if' if first else 'elif'} FELUCCA_LAYOUT == {s.layout}                  /* {s.key}, {s.name} */")
        first = False
        lines += [f"#define FL_APP_HI {_h(s.end)}",
                  f"#define FL_RELOC_HI {_h(s.reloc)}",
                  f"#define FL_UPF_LO {_h(s.upf)}                /* FELUCCA_UP_FM6: OBJ_UPFM6, A/B */",
                  f"#define FL_UPF_HI {_h(s.upf + 2 * SECTOR)}",
                  f"#define FL_DATA_LO {_h(s.data_lo)}               /* Optimist's store starts here */",
                  f"#define FL_AUTOSAVE_A {_h(s.autosave_a)}",
                  f"#define FL_LOG_LO {_h(s.log_lo)}",
                  f"#define FL_LOG_SECTORS {s.log_sectors}u" + ("                       /* 0: sec_log.c's rule (20 with user "
                                                                 "samples, else 32) */" if not s.log_sectors else ""),
                  f"#define FL_USER_SAMPLES {int(s.slots > 0)}                       /* user sample slots, snapshots, the CZ "
                  "collection */",
                  f"#define FL_USER_SLOTS {s.slots}u                        /* the slots kept, from USR3 down (normal: "
                  "eng_sample.c's rule) */",
                  f"#define FL_PROJ_SLOTS {s.projects}u"]
    names = ", ".join(f"{s.layout} ({s.name})" for s in sorted(STEPS.values(), key=lambda s: s.layout))
    lines += ["#else", f'#error "FELUCCA_LAYOUT: {names} (tools/flash_layout.py)"', "#endif", ""]
    return "\n".join(lines)


if __name__ == "__main__":
    import sys
    from pathlib import Path
    out = Path(__file__).resolve().parents[1] / "firmware" / "hal" / "fm1_steps.h"
    if sys.argv[1:] == ["--header"]:
        out.write_text(c_header())
        print(f"wrote {out}")
    elif sys.argv[1:] == ["--check"]:
        sys.exit(0 if out.read_text() == c_header() else f"{out}: not what tools/flash_layout.py --header makes")
    else:
        sys.exit("flash_layout.py --header | --check")
