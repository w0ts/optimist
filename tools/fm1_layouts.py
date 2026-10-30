# SPDX-License-Identifier: GPL-3.0-only
"""The FM-1's firmware layouts and what a download-mode install writes, keeps and clears (docs/INSTALL-PATHS.md,
"Layouts"). M-Flasher's `firmware` writes a package's flash.bin [0x4000, its end), the sectors that differ only;
it knows nothing of Optimist's or fm1-base's data. What a switch between two layouts needs beyond that, this plans:

  plan(current, pkg_flash)   -> Plan: the sectors written (the package), cleared (what the other layout left where
                                the new one keeps its data), lost (user data under the new code), kept (asserted
                                untouched: the boot area, the update staging and crash ring, the drum records, the SDK's
                                VM / BTIF / USR (0xE7000-0xFBFFF), the settings 0xFC000-0xFEFFF, key_mac)
  target_image(current, p)   the whole flash after the plan: M-Flasher's `restore` of it writes exactly those sectors

The update loaders do the same clears themselves (Optimist's ldr_core.c ldr_layout_switch, flash_layout.switch_clear;
fm1-base's LDR_SLOT_SWITCH with app_clear_end); this is for download mode, where M-Flasher writes."""
from __future__ import annotations

from dataclasses import dataclass

import flash_layout
import fm1_rescue

SECTOR = 0x1000
FLASH_SIZE = 0x100000
APP_LO = 0x4000
NORMAL_END = flash_layout.app_hi(0)
CLEAR_TOP = flash_layout.CLEAR_TOP   # no switch clears at or past it (fm1.toml app_clear_end the same)
DATA_TOP = flash_layout.STORE_TOP    # no layout's app or store reaches past it
KEEP = (                       # never written by an install, in any layout
    (0x00000, 0x04000, "boot area (bootloader, isd_config): only an explicit bootloader step"),
    (0xE0000, 0xE5000, "update staging, fm1-base's backup journal 0xE1000 and crash ring 0xE2000-0xE3FFF"),
    (0xE5000, 0xE7000, "the projects' drum records (Optimist), XDATA1 (fm1-base)"),
    (0xE7000, 0xEA000, "the SDK's system flash: VM (settings, RF calibration) 0xE8000, BTIF (pairings) 0xE9000"),
    (0xEA000, 0xFC000, "the SDK's USR area (the stock sounds)"),
    (0xFC000, 0xFF000, "settings A/B, autosave B (Optimist), XDATA2 (fm1-base)"),
    (0xFF000, 0x100000, "key_mac: the Bluetooth address, the efuse page"),
)

# what erasing "the settings and pairings" means for the new firmware (fm1_route.settings_policy): its settings only,
# never the projects; never M-Flasher's --wipe-settings (the package's VM region: [app end, 0xE9000) in ours)
WIPE = {
    "optimist": ((0xFC000, 0xFE000, "Optimist's settings record A/B (storage.c: its BLE bonds, the Wi-Fi networks)"),),
    "felucca": ((0xFC000, 0xFE000, "Felucca's globals (its settings: FL_GLOB, the same sectors as Optimist's)"),),
    "stock": ((0xE9000, 0xEA000, "the SDK's BTIF: the stock firmware's pairings (its settings share 0xE8000 with the "
                                    "radio calibration: never erased)"),),
}

# (name, family, flash.bin end, where it keeps its user data)
LAYOUTS = (
    ("own-normal", "Optimist's own, MEMORY normal", 0x93000,
     "0x95000-0xDFFFF: FM6 voices, sections / projects, autosave A, USR1-3, snapshots, kits, presets"),
    ("own-s0", "Optimist's own, MEMORY no user samples (LAYOUT 3)", flash_layout.app_hi(3),
     "0xB5000-0xDFFFF: FM6 voices 0xB5000, autosave A 0xB7000, the project log 0xB8000-0xD7FFF (16 projects), kits, presets"),
    ("own-big", "Optimist's own, MEMORY more flash or SD card mode (LAYOUT 1)", flash_layout.app_hi(1),
     "0xD0000-0xDFFFF: the squeezed store (one project, autosave A, FM6 voices), kits, presets; SD mode: the card"),
    ("base-normal", "fm1-base (Optimist's release), the normal slot", 0x93000,
     "the platform's data area 0x97000-0xDFFFF (Optimist's store, OBJ_PANEL 0xD8000), XDATA0 0x93000-0x96FFF"),
    ("base-sd", "fm1-base, app_end 0xCE000 (the release's SD card mode, 823,228 B)", 0xCE000,
     "OBJ_PANEL 0xCE000 / 0xCF000, Optimist's store from 0xD0000, the rest on the card"),
)


def layout_of(end: int) -> str:
    """the step an app end belongs to (tools/flash_layout.py known_ends): its MEMORY name, the end when two builds share it"""
    ends = flash_layout.known_ends()
    if end not in ends:
        return f"app end {end:#x}"
    layout = ends[end][0]
    shared = [e for e, (k, _) in ends.items() if k == layout]
    name = flash_layout.NAMES[layout]
    return f"{name} ({end:#x})" if len(shared) > 1 else name


def _up(n: int) -> int:
    return (n + SECTOR - 1) & ~(SECTOR - 1)


def installed_end(current: bytes, key: int = fm1_rescue.fm1pkg_make.KEY) -> int:
    """where the installed firmware's slot ends: its app_area_head's end rounded up to a sector (the tail after app.bin
    is under one: the slot's end), else up to the next known end (tools/flash_layout.py known_ends; a head that ends on
    a sector boundary below its slot's); 0 when there is no app of this kind"""
    end = fm1_rescue.installed_end(current, key)
    if not end:
        return 0
    ends = flash_layout.known_ends()
    if _up(end) in ends:
        return _up(end)
    return next((e for e in ends if e >= end), _up(end))


@dataclass(frozen=True)
class Plan:
    old_end: int
    new_end: int
    writes: tuple       # sector addresses the package changes
    clears: tuple       # sector addresses erased (0xFF) that are not already blank
    lost: tuple         # (lo, hi) of user data the new code or a clear covers, non-blank now
    notes: tuple

    @property
    def sectors(self) -> tuple:
        return tuple(sorted(set(self.writes) | set(self.clears)))

    def runs(self) -> list:
        out = []
        for a in self.sectors:
            if out and out[-1][1] == a:
                out[-1][1] = a + SECTOR
            else:
                out.append([a, a + SECTOR])
        return [tuple(r) for r in out]

    def describe(self) -> list:
        rng = ", ".join(f"0x{a:05X}-0x{b - 1:05X}" for a, b in self.runs()) or "none"
        out = [f"layout: {layout_of(self.old_end) if self.old_end else 'no app found'} -> {layout_of(self.new_end)}",
               f"writes {len(self.sectors)} sectors ({len(self.sectors) * 4} KiB): {rng}"]
        if self.clears:
            out.append(f"  of them erased (the other layout's leftovers, a settings wipe): {len(self.clears)}")
        out += [f"  user data overwritten: 0x{a:05X}-0x{b - 1:05X} (back up the projects first)" for a, b in self.lost]
        return out + [f"  {n}" for n in self.notes]


class PlanError(Exception):
    pass


def _clear_range(old_end: int, new_end: int) -> tuple:
    """the loaders' one rule (docs/SD-MODE-DESIGN.md 9.3, flash_layout.switch_clear_ends); a flash.bin no step has:
    only the old code's tail past it (as before)"""
    if new_end in flash_layout.known_ends():
        return flash_layout.switch_clear_ends(old_end, new_end)
    return (new_end, old_end) if old_end > new_end else (0, 0)


def _span(addrs, current) -> list:
    out = []
    for a in sorted(addrs):
        if current[a:a + SECTOR] == b"\xFF" * SECTOR:
            continue
        if out and out[-1][1] == a:
            out[-1][1] = a + SECTOR
        else:
            out.append([a, a + SECTOR])
    return [tuple(r) for r in out]


def plan(current: bytes, pkg_flash: bytes, key: int = fm1_rescue.fm1pkg_make.KEY, wipe: str = "") -> Plan:
    """wipe: a WIPE key (the settings and pairings of that firmware erased too), "" none"""
    if len(current) != FLASH_SIZE:
        raise PlanError(f"the flash image is {len(current)} B, not 1 MiB")
    new_end = len(pkg_flash)
    if new_end % SECTOR or not APP_LO < new_end <= DATA_TOP:
        raise PlanError(f"a flash.bin of {new_end:#x} B ends outside the app area [0x4000, 0xE0000)")
    old_end = installed_end(current, key)
    writes = tuple(a for a in range(APP_LO, new_end, SECTOR) if current[a:a + SECTOR] != pkg_flash[a:a + SECTOR])
    lo, hi = _clear_range(old_end, new_end)
    wiped = [a for w_lo, w_hi, _ in WIPE.get(wipe, ()) for a in range(w_lo, w_hi, SECTOR)]
    clears = tuple(sorted({a for a in [*range(lo, hi, SECTOR), *wiped] if current[a:a + SECTOR] != b"\xFF" * SECTOR}))
    data_lo = old_end or NORMAL_END                 # (no app of ours found: its data from the normal layout's end)
    grown = list(range(data_lo, new_end, SECTOR)) if new_end > data_lo else []
    lost = _span(grown + (list(clears) if grown else []), current)   # (a smaller slot clears old code, not data)
    notes = []
    if old_end and old_end != new_end:
        notes.append(f"a layout switch: {layout_of(old_end)} to {layout_of(new_end)}")
    notes += [f"settings erased: 0x{w_lo:05X}-0x{w_hi - 1:05X} ({what})" for w_lo, w_hi, what in WIPE.get(wipe, ())]
    p = Plan(old_end, new_end, writes, clears, tuple(lost), tuple(notes))
    for a in p.sectors:
        if a in wiped:
            continue
        for k_lo, k_hi, what in KEEP:
            if k_lo <= a < k_hi:
                raise PlanError(f"internal: the plan would write 0x{a:05X} ({what})")
    return p


def target_image(current: bytes, pkg_flash: bytes, p: Plan) -> bytes:
    out = bytearray(current)
    for a in p.writes:
        out[a:a + SECTOR] = pkg_flash[a:a + SECTOR]
    for a in p.clears:
        out[a:a + SECTOR] = b"\xFF" * SECTOR
    return bytes(out)


def changed(a: bytes, b: bytes) -> list:
    """the sector addresses where two whole images differ"""
    return [s for s in range(0, min(len(a), len(b)), SECTOR) if a[s:s + SECTOR] != b[s:s + SECTOR]]


def layouts_markdown() -> str:
    rows = ["| layout | flash.bin | user data |", "|---|---|---|"]
    rows += [f"| {fam} (`{name}`) | [0x4000, {end:#x}) | {data} |" for name, fam, end, data in LAYOUTS]
    return "\n".join(rows) + "\n"


def switches_markdown() -> str:
    """every switch between two layouts: what the download-mode install clears and overwrites"""
    rows = ["| from \\ to | " + " | ".join(n for n, *_ in LAYOUTS) + " |", "|" + "---|" * (len(LAYOUTS) + 1)]
    for n1, _, e1, _ in LAYOUTS:
        cells = []
        for n2, _, e2, _ in LAYOUTS:
            lo, hi = _clear_range(e1, e2)
            parts = [f"writes 0x4000-{e2 - 1:#x}"]
            if e2 > e1:
                parts.append(f"overwrites data {e1:#x}-{e2 - 1:#x}")
            if hi > lo:
                parts.append(f"clears {lo:#x}-{hi - 1:#x}")
            cells.append("same layout: " + parts[0] if n1 == n2 else "; ".join(parts))
        rows.append(f"| {n1} | " + " | ".join(cells) + " |")
    return "\n".join(rows) + "\n"


assert all(lo >= CLEAR_TOP or hi <= APP_LO for lo, hi, _ in KEEP[1:]), "a KEEP range below the switch clears' top"
