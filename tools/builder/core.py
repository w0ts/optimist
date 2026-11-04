# SPDX-License-Identifier: GPL-3.0-only
"""The builder's core: one implementation of the rules for the menu, the CLI, CI and (later) a web page in Pyodide.

Pure: no file, git, subprocess or UI at import or in any call. What the runner reads from disk comes in as data:
the costs (tools/builder/costs.json) as a dict, the items the firmware drops without the automation store
(setup(needs_auto=...): configure.py reads them from firmware/src/core/*.h), a real build's sizes as `exact`.
The registry (registry.py) must be complete first: configure.py imports backports.py, which adds the backported
items from tools/backports.json.

  parse, validate, flags, header, budget, fits, fit ...   the rules (configure.py re-exports them unchanged)
  effective / canonical / cloud_key                       the image's values, their text and the cache key
  describe() / evaluate(request)                          the JSON API: dict in, dict out

The key (docs: optimist-work cloudbuild DESIGN.md, "Cache key and normalization"):
sha256("optimist-cloud/1\\n" + commit + "\\n" + canonical), canonical the sorted KEY=value lines of what the image
is built with (an option of an item left out at its default, the retired items at the
value the image is still built as)."""
import functools
import hashlib
import os
import re
import sys
import textwrap

import registry as R

KEY_TAG = "optimist-cloud/1"
NEEDS_AUTO = ()                 # (setup: the items the firmware switches off without the automation store)


_UNSET = object()


def _release():
    """which firmware the rules are for: configure.py sets CLASSIC_HOOK to its classic() (Optimist's own firmware,
    OPTIMIST_CLASSIC=1); the default is the release (Optimist as an fm1-base app)"""
    return False


CLASSIC_HOOK = _release
LIMITS_HOOK = None              # (setup: configure.py's kind_limits, the slots of the firmware built, read from its build system)
SETUP_LIMITS = None             # (setup: the costs' "limits", where no build system is around: the web page)


def setup(needs_auto=None, limits=_UNSET, limits_hook=_UNSET):
    """the data the core cannot read itself: needs_auto, the keys of configure._needs_auto (firmware headers); limits:
    the "limits" of the costs in use ({"flash", "sram", "flash_by_layout": {layout: bytes}}: the slots of the firmware
    they were measured on, measure_costs.py writes them from the build system), the page's; limits_hook: a function -> those
    slots for the firmware built now (configure.py kind_limits: the build system itself, which wins over the costs' copy),
    None where it cannot tell"""
    global NEEDS_AUTO, SETUP_LIMITS, LIMITS_HOOK
    if needs_auto is not None:
        NEEDS_AUTO = tuple(needs_auto)
    if limits is not _UNSET:
        SETUP_LIMITS = dict(limits) if limits else None
    if limits_hook is not _UNSET:
        LIMITS_HOOK = limits_hook


def norm_limits(lim):
    """a "limits" record (JSON: the layouts as strings) -> {"flash", "sram", "flash_by_layout": {int layout: bytes}}"""
    return {"flash": int(lim["flash"]), "sram": int(lim["sram"]),
            "flash_by_layout": {int(k): int(v) for k, v in lim.get("flash_by_layout", {}).items()}}


def limits_in_use():
    """-> {"flash", "sram", "flash_by_layout": {layout: bytes}}: the slots of the firmware built (the hook: its build
    system), else the costs' (setup); no number of its own (tools/flash_layout.py, tools/build.py, fm1-base's build hold them)"""
    lim = LIMITS_HOOK() if LIMITS_HOOK else None
    lim = lim or SETUP_LIMITS
    if not lim:
        raise ConfigError("no flash and SRAM limits: the costs carry none and no build system says them (core.setup)")
    return norm_limits(lim)


def _stderr(msg):
    print(f"warning: {msg}", file=sys.stderr)


# the budgets: the app slot and the packed SRAM (app.ld: RAM code, .data/.bss and the pool in one region since
# 2026-10-25, docs/MEMORY-MAP.md 2.3-2.4), their sizes limits_in_use(); build.py keeps 8 KiB of SRAM spare below the stacks
SPARE = {"flash": 0, "sram": 8192}
BUDGETS = ("flash", "sram")
# the measured parts (costs.json, the deltas): "ram" = .ram_hot2 + .data + .bss, "pool", "ramtext" = .ram_text +
# .ram_hot; the three together fill the SRAM budget
REGIONS = ("flash", "ram", "pool", "ramtext")
SRAM_PARTS = ("ram", "pool", "ramtext")


def used(total, budget):
    """what a total (per measured part) puts against a budget: the SRAM budget is the sum of its parts"""
    return sum(total[r] for r in SRAM_PARTS) if budget == "sram" else total[budget]
GRAIN_SETS = ("SET_PIANO", "SET_VIBES", "SET_FLUTE")       # the sets of GRAIN's presets (eng_grain.c)


class ConfigError(Exception):
    pass


# ---- reading and writing
def defaults():
    return {k: it.default for k, it in R.ITEMS.items()}


# ---- the memory fronts (registry.py SMP_SLOTS, PROJECTS; MEMORY the old one-row front): views of LAYOUT and PATTERNS,
# never stored in a .config (docs/SD-MODE-DESIGN.md 9.7)
FRONTS = ("MEMORY", "SMP_SLOTS", "PROJECTS")
MEMORY_KEYS = FRONTS + ("LAYOUT",)                   # (a change of one may move the flash layout: change_notice)


def _grid():
    """{(sample slots, projects): layout} of every step (registry.py LAYOUT_STEPS)"""
    return {(s, p): k for k, (s, p, _) in R.LAYOUT_STEPS.items()}


def derive_memory(cfg):
    """-> MEMORY's (old) value for this LAYOUT: 1 more flash (LAYOUT 1), 4 no user samples (LAYOUT 3), 0 normal (a layout
    no step has reads normal), 5 another step of the grid"""
    layout = cfg.get("LAYOUT", 0)
    if layout not in R.LAYOUT_STEPS:
        return 0
    return R.MEMORY_OF.get(layout, 5)


def derive_fronts(cfg):
    """-> {SMP_SLOTS, PROJECTS} as this LAYOUT / PATTERNS read: normal with PATTERNS off: 3 slots"""
    layout = cfg.get("LAYOUT", 0)
    slots, projects, _ = R.LAYOUT_STEPS.get(layout, R.LAYOUT_STEPS[0])
    if layout == 0 and not cfg.get("PATTERNS", 0):
        slots = 3
    return {"SMP_SLOTS": slots, "PROJECTS": projects}


def sync_memory(cfg):
    """-> cfg with MEMORY and the two fronts read back from the items they front (a copy)"""
    return dict(cfg, MEMORY=derive_memory(cfg), **derive_fronts(cfg))


def set_memory(cfg, mode):
    """-> cfg with MEMORY = mode and the items it fronts set to match (a copy): 0 normal; 4 no user samples (LAYOUT 3);
    1 more flash (LAYOUT 1). 2 was the SD card mode (removed 2026-11-04: an old line reads as more flash)"""
    layouts = {m: k for k, m in R.MEMORY_OF.items()}
    if mode == 2:
        mode = 1
    if mode not in layouts:
        raise ConfigError(f"MEMORY={mode}: one of 0, 4, 1")
    out = dict(cfg)
    out.update(LAYOUT=layouts[mode])
    return sync_memory(out)


def _near(pairs, want):
    """the (slots, projects) of pairs closest to want: the same slots first, then the projects nearest (more first)"""
    return min(pairs, key=lambda sp: (sp[0] != want[0], abs(sp[1] - want[1]), -sp[1], abs(sp[0] - want[0])))


def set_front(cfg, key, value):
    """-> cfg with the front key (MEMORY, SMP_SLOTS, PROJECTS) at value and LAYOUT / PATTERNS set to match (a copy).
    SMP_SLOTS keeps the projects and PROJECTS the slots when the grid has the pair, else the nearest step; SMP_SLOTS 3
    is normal with PATTERNS off (no projects: USR1 is a slot again), 0..2 turn PATTERNS on"""
    if key == "MEMORY" and value == 5:                  # (another step, derived: LAYOUT and the fronts say which; a
        return sync_memory(cfg)                         # config handed in as KEY=value lines names it, not an error)
    if key == "MEMORY":
        return set_memory(cfg, value)
    if key not in FRONTS:
        raise ConfigError(f"{key}: not a memory front")
    now = derive_fronts(cfg)
    out = dict(cfg)
    if key == "SMP_SLOTS" and value == 3:
        out.update(LAYOUT=0, PATTERNS=0)
        return sync_memory(out)
    want = (value, now["PROJECTS"]) if key == "SMP_SLOTS" else (min(now["SMP_SLOTS"], 2), value)
    grid = _grid()
    if not any((s if key == "SMP_SLOTS" else p) == value for s, p in grid):
        raise ConfigError(f"{key}={value}: no flash step has it "
                          f"({', '.join(str(v) for v, _ in R.ITEMS[key].choices)})")
    pair = want if want in grid else _near([sp for sp in grid if (sp[0] if key == "SMP_SLOTS" else sp[1]) == value],
                                           want)
    out.update(LAYOUT=grid[pair], PATTERNS=1)
    return sync_memory(out)


def layout_room(layout):
    """app.bin's room in the classic build for this step (registry.py LAYOUT_STEPS)"""
    return R.LAYOUT_STEPS.get(layout, R.LAYOUT_STEPS[0])[2]


def switch_loss(old_layout, new_layout):
    """what installing new_layout's firmware over old_layout's erases (the builder's copy of tools/flash_layout.py
    switch_loss, checked against it by tests/flash_layout_test.py): () the same step"""
    if old_layout == new_layout:
        return ()
    old = R.LAYOUT_STEPS.get(old_layout, R.LAYOUT_STEPS[0])[0]
    new = R.LAYOUT_STEPS.get(new_layout, R.LAYOUT_STEPS[0])[0]
    out = ["the projects in flash", "the FM6 voices of the user presets"]
    usr2, usr3 = old >= 2 > new, old >= 1 > new
    if usr2 and usr3:
        out.append("the user samples (USR2, USR3) and the snapshots")
    elif usr3:
        out.append("the user samples in USR3 and the snapshots")
    elif usr2:
        out.append("the user samples in USR2")
    return tuple(out)


def change_notice(key, before, after):
    """the notice the menu and the web builder show when key changed from before to after (both whole, synced): for
    the memory items the backup warning of a layout change (the user, 2026-10-31: "when we flash a different layout,
    we warn the user to backup their data first"), with the new code room; "" when the layout stays. Any other item: its own notice"""
    it = R.ITEMS[key]
    if key in MEMORY_KEYS:
        old, new = before.get("LAYOUT", 0), after.get("LAYOUT", 0)
        loss = switch_loss(old, new)
        if not loss:
            return ""
        what = ", ".join(loss[:-1]) + " and " + loss[-1]
        room = layout_room(new)
        return (f"New memory layout: {room:,} B for code ({room - R.NORMAL_APP:+,} B against normal). Installing it on "
                f"an FM-1 with the layout of before erases {what}. Back up first: web editor > Backup. The first "
                f"install of a new layout goes over a normal build of this version.")
    return it.notice if it.notice and (after[key] or it.notice_on_change and after[key] != before[key]) else ""


def parse(text, base=None, strict=True, warn=None, reject_unknown=False):
    """.config text -> (values, name); values start from base (default: the registry defaults). warn: called with
    each warning (default: printed on stderr); strict=False: an unknown key is ignored without a word;
    reject_unknown: an unknown key is a ConfigError (untrusted input: a cloud build request)"""
    say = warn or _stderr
    cfg = dict(base if base is not None else defaults())
    name = ""
    merged_on = {}                                      # (a merged item on in the text: its survivor is on, whatever order)
    named = set()                                       # (the keys the text sets itself)
    for n, ln in enumerate(text.splitlines(), 1):
        s = ln.strip()
        m = re.match(r"#\s*name:\s*(.+)", s)
        if m:
            name = m.group(1).strip()
            continue
        if not s or s.startswith("#"):
            continue
        m = re.fullmatch(r"([A-Z0-9_]+)\s*=\s*(-?\d+)", s)
        if not m:
            raise ConfigError(f"line {n}: expected KEY=number, got {s!r}")
        k, v = m.group(1), int(m.group(2))
        if k in FRONTS:                                 # (a front: sets LAYOUT; the lines after it refine)
            cfg = set_front(cfg, k, v)
            continue
        if k in R.FORBIDDEN:
            raise ConfigError(f"{k}: never offered ({R.FORBIDDEN[k]})")
        if k in R.MERGED:                               # (merged into another: the survivor is on if either was)
            into, why = R.MERGED[k]
            say(f"line {n}: {k} was merged into {into} ({why})")
            merged_on[into] = merged_on.get(into, 0) | int(v != 0)
            continue
        if k in R.RETIRED:                              # (dropped: still loads, ignored, said so)
            say(f"line {n}: {k} was dropped and the setting is ignored ({R.RETIRED[k]})")
            continue
        if k in R.MIGRATE:                              # (an item that became others: registry.py MIGRATE)
            cfg.update(R.MIGRATE[k](v))
            continue
        if k not in R.ITEMS:                            # (a newer or an older builder's item: ignored, said so)
            if reject_unknown:
                raise ConfigError(f"line {n}: {k} is not an item of this builder")
            if strict:
                say(f"line {n}: {k} is not an item of this builder and the setting is ignored")
            continue
        if k in R.WAS_COUNT and v:                      # (a count of instances before the effects memory: built)
            v = 1
        if k in R.FAST_NAMES and v == R.ENG_LEGACY_ON:  # (an engine "on" before its place was a choice: its place by
            v = R.eng_place(k)                          # default, registry.py FAST_NAMES: the image it built)
        cfg[k] = v
        named.add(k)
    for k, it in R.ITEMS.items():                      # (a fixed item: what every build has, whatever the text says)
        if it.fixed:
            cfg[k] = it.default
    cfg = sync_memory(cfg)                              # (MEMORY: read from LAYOUT, whatever the text says)
    for into, on in merged_on.items():                  # (a survivor the text does not set: what its old items were, so
        cfg[into] = cfg[into] | on if into in named else on   # an older .config with all of them off keeps it off)
    return cfg, name


def dump(cfg, name="", full=False):
    """values -> .config text (only what differs from the defaults unless full)"""
    out = [f"# name: {name}"] if name else []
    out.append("# firmware builder configuration (tools/builder/registry.py keys; others take their defaults)")
    for g in R.GROUPS:
        rows = []
        for k, it in R.ITEMS.items():
            if it.group == g and k not in FRONTS and (full or cfg[k] != it.default):
                rows.append(f"{k}={cfg[k]}")
        if rows:
            out.append(f"# {g}")
            out += rows
    return "\n".join(out) + "\n"


# ---- meaning
def built(cfg, key):
    """the item is in the build: on (a choice: non-zero) and its parent on"""
    it = R.ITEMS[key]
    if it.parent and not built(cfg, it.parent):
        return False
    if key in R.DRUMS_ONLY and not built(cfg, "DRUMS"):
        return False
    return cfg[key] != 0


def dimmed_choices(cfg, key):
    """-> {choice value: reason} of the item's choices greyed in this configuration (registry dim_choice: not offered)"""
    return {v: why for v, (item, why) in R.ITEMS[key].dim_choice.items() if built(cfg, item)}


# the drum synths, each a drum source on its own (no parent: the user, 2026-10-22, "3 drum synths, independent of each
# other"): the 32 synthesised kits, KAWA, PLAITS (their shared voice built with any of them: registry.h DRUM_DS), the X0X kits
DRUM_SYNTHS = ("DRUM_SYNTH", "DS_KAWA", "DS_PLAITS", "DRUM_X0X909", "DRUM_X0X808")


class Issue(str):
    """a validate() message that names the registry items it concerns (.keys): the menu marks their lines. It is
    a str, so printing, joining and `in` work as on a plain message"""

    def __new__(cls, text, keys=()):
        s = super().__new__(cls, text)
        s.keys = tuple(k for k in keys if k in R.ITEMS)
        return s


def validate(cfg):
    """-> (errors, warnings, notices), each a list of Issue (a str with .keys: the items it concerns); errors stop
    a build"""
    err, warn, note = [], [], []
    for k, it in R.ITEMS.items():
        v = cfg.get(k, it.default)
        if it.is_choice and v not in [c[0] for c in it.choices]:
            err.append(Issue(f"{k}={v}: one of {[c[0] for c in it.choices]}", [k]))
        elif not it.is_choice and v not in (0, 1):
            err.append(Issue(f"{k}={v}: 0 or 1", [k]))
    if os.environ.get("OPTIMIST_ALLOW_UNBUILT") != "1":   # (measure_costs.py probes the marked values: it sets this)
        kind = "classic" if CLASSIC_HOOK() else "release"
        for k, it in R.ITEMS.items():
            v = cfg.get(k, it.default)
            why = R.not_built(k, v, kind)
            if why:
                err.append(Issue(f"{k}={v} is not in this build ({kind}): {why}", [k]))
    engines = [k for k, it in R.ITEMS.items() if it.group == "Synth engines" and not it.parent]
    if not any(built(cfg, k) for k in engines):
        err.append(Issue("at least one synth engine", engines))
    if built(cfg, "ENG_FM6"):
        if not cfg["FM6_KEYS"] and not cfg["FM6_SYSEX"]:
            warn.append(Issue("FM6 without the operator editor and without DX7 SysEx: INIT VOICE and the voice packs only (no voice editing)",
                              ("FM6_KEYS", "FM6_SYSEX")))
        if not cfg["FM6_SYSEX"] and not any(cfg[k] for k in ("FM6_PK_KEYS", "FM6_PK_BASS", "FM6_PK_BELLS", "FM6_PK_PADS", "FM6_PK_80S")):
            warn.append(Issue("FM6 without a voice pack and without DX7 SysEx: INIT VOICE is its only voice to start from",
                              ("FM6_SYSEX", "FM6_PK_KEYS")))
    sx_users = [k for k in ("MICRO", "FILLS", "PLOCK") if k in R.ITEMS and cfg.get(k)]   # (SLOOP 2.4's sequencer)
    if sx_users and "SL24_XSTEP" in R.ITEMS and not cfg.get("SL24_XSTEP"):
        err.append(Issue("micro timing, fills and parameter locks need their storage: switch on SL24_XSTEP",
                         ["SL24_XSTEP"] + sx_users))
    if cfg.get("PATTERNS") and cfg.get("SECTIONS") == 4:
        err.append(Issue("patterns live in the section log: SECTIONS 8 or 16 (or PATTERNS off)", ["PATTERNS", "SECTIONS"]))
    layout = cfg.get("LAYOUT", 0)
    if layout != 0:                                    # (every step but normal: a relocated store, the project log;
        slots = R.LAYOUT_STEPS.get(layout, (0,))[0]    # SAMPLE SLOTS 0: the slots' flash is the app's or the store's;
        clash = [] if slots else [k for k in ("DRUM_USR", "SNAPSHOTS") if k in R.ITEMS and built(cfg, k)]   # an unknown
        if not slots and built(cfg, "NATIVE_BANKS") and built(cfg, "ENG_CZ"):   # layout: the same, validate refuses it)
            clash.append("NATIVE_BANKS")
        if clash or not cfg.get("PATTERNS"):
            err.append(Issue(f"MEMORY: SAMPLE SLOTS {slots} (LAYOUT {layout}) "
                             + ("keeps no user drum samples, snapshots or CZ collection" if clash else "")
                             + (" and " if clash and not cfg.get("PATTERNS") else "")
                             + ("needs PATTERNS (the project log)" if not cfg.get("PATTERNS") else "")
                             + (": " + ", ".join(clash) if clash else ""),
                             ["LAYOUT"] + clash + ([] if cfg.get("PATTERNS") else ["PATTERNS"])))
        if slots and not any(built(cfg, k) for k in ("ENG_SAMPLE", "DRUM_USR", "SNAPSHOTS", "NATIVE_BANKS") if k in R.ITEMS):
            warn.append(Issue(f"MEMORY: SAMPLE SLOTS {slots} keeps flash for user samples that nothing in this build "
                              "uses (no SAMPLE, user drum samples, snapshots or CZ collection): SAMPLE SLOTS 0 gives it "
                              "to the code", ["LAYOUT"]))
    revs = [k for k in R.REV_ALGOS if k in R.ITEMS]
    if built(cfg, "FX_REVERB") and not any(cfg[k] for k in revs):
        err.append(Issue("the reverb bus needs an algorithm: tick ROOM, PLATE, FDN8, SPRING or VTINY "
                         "(or the reverb bus off)",
                         ["FX_REVERB"] + revs))
    kits = [k for k in R.ITEMS if k.startswith("KIT_")]
    synths = list(DRUM_SYNTHS)   # (the drum synths, first in Drums: each a drum source on its own)
    if built(cfg, "DRUMS") and not any(built(cfg, k) for k in synths + kits):
        err.append(Issue("the drum track needs a drum source: a drum synth (the 32 synthesised kits, KAWA, PLAITS, "
                         "X0X 909, X0X 808) or a sampled kit", synths + ["DRUM_SAMPLED"] + kits))
    if built(cfg, "ENG_STRETCH") and not built(cfg, "ENG_SAMPLE"):
        err.append(Issue("STRETCH stretches SAMPLE's material: it needs SAMPLE", ["ENG_STRETCH", "ENG_SAMPLE"]))
    sets = set_keys()
    if any(cfg[k] for k in sets) and not built(cfg, "ENG_SAMPLE") and not built(cfg, "ENG_GRAIN"):
        warn.append(Issue("sample sets without SAMPLE or GRAIN: nothing plays them (only the drums use PERC)",
                          ["ENG_SAMPLE", "ENG_GRAIN"] + [k for k in sets if cfg[k]]))
    if (built(cfg, "ENG_SAMPLE") or built(cfg, "ENG_GRAIN")) and not any(cfg[k] for k in sets):
        warn.append(Issue("SAMPLE / GRAIN without a built-in set: they play the USR slots only (their PRESETS "
                          "entry: INIT, the engine's defaults; SAMPLE: GM KIT with the sampled kits)",
                          ["ENG_SAMPLE", "ENG_GRAIN"] + sets))
    elif built(cfg, "ENG_GRAIN") and not any(cfg[k] for k in GRAIN_SETS):
        warn.append(Issue("GRAIN's presets play PIANO, VIBES or FLUTE: without them it has GRAIN PAD on the first "
                          "melodic set built (none: INIT, its defaults)", ["ENG_GRAIN"] + list(GRAIN_SETS)))
    if built(cfg, "FX_DUCK") and not any(built(cfg, k) for k in DRUM_SYNTHS + ("DRUM_SAMPLED",)):
        warn.append(Issue("DUCK follows the kick", ("FX_DUCK", "DRUM_SYNTH", "DRUM_SAMPLED")))
    mem = fx_memory(cfg)
    if mem["types"]:                                    # (the user, 2026-10-29: no red error for a memory too small:
        for k, t in mem["types"].items():
            if not t["own"] and not t["arena"]:         # (borrowing a send's line only: silent while the sends are on
                                                        # the master, as by default; the FM-1, 2026-10-25: XDLY did
                                                        # nothing, "it should be addressed at the builder stage")
                note.append(Issue(f"{FXM_NAMES[k]} plays only while the delay is in no master slot: its line is "
                                  f"{t['kb']} KB and the effects memory has {mem['own']} KB beside the delay's "
                                  f"{mem['send']} KB", ["INS_DELAY", k]))
        note.append(Issue(f"effects memory (the delay's line and one line of each insert built): {fx_memory_text(mem)}",
                          [k for k in FXM_TYPES if cfg.get(k)]))
    if cfg.get("ARENA", 0):                             # (the free SRAM: what the link leaves, the estimate's free)
        need = arena_need(cfg)
        what = ", ".join(f"{k} {-(-v // 1024)} KB" for k, v in need["what"].items())
        if need["kb"] > ARENA_MAX_KB:                   # (more than the SRAM can give the arena: the one error left)
            err.append(Issue(f"the arena would need {need['kb']} KB, more than the {ARENA_MAX_KB} KB the SRAM can give "
                             f"it ({what}): a shorter DLY_LEN or XDLY_LEN, a smaller reverb, fewer engines, or ARENA off",
                             ["ARENA", "DLY_LEN"]))
        held = "the parts' engine states, the effects' lines, the SLICER's recordings, WAVE's table, the X0X machines"
        note.append(Issue(f"{R.ITEMS['ARENA'].label}: the SRAM the link leaves free (the estimate's free, less the undo "
                          f"history's 16 KB) holds {held} instead of {-arena_mem(cfg)['pool'] // 1024} KB of fixed "
                          f"memories; a new project needs about {need['kb']} KB of it together ({what}); a project "
                          "using more than there is is refused and offered what to drop", ["ARENA"]))
    for k, it in R.ITEMS.items():
        if it.off_warning and not cfg[k] and (not it.parent or built(cfg, it.parent)):
            warn.append(Issue(f"{it.label} off: {it.off_warning}", [k]))
        if built(cfg, k):
            if it.experimental:
                warn.append(Issue(f"{it.label}: EXPERIMENTAL ({it.tested})", [k]))
            if it.notice:
                note.append(Issue(f"{it.label}: {it.notice}", [k]))
    if built(cfg, "WIFI_LINK") and not built(cfg, "BLE"):
        err.append(Issue("Wi-Fi + Link runs on the radio BLE starts: switch on BLE (or Wi-Fi off)", ["WIFI_LINK", "BLE"]))
    elif built(cfg, "WIFI_LINK") and CLASSIC_HOOK() and not cfg["USB_CONSOLE"]:
        err.append(Issue("Wi-Fi + Link on Optimist's own firmware runs its manager on the serial console: switch on "
                         "USB_CONSOLE (or Wi-Fi off; the release runs it on fm1-base's own Wi-Fi stack)",
                         ["WIFI_LINK", "USB_CONSOLE"]))
    if built(cfg, "USB_HOST") and CLASSIC_HOOK():
        err.append(Issue("the USB MIDI host is the release's (fm1-base's USB host): Optimist's own firmware has none "
                         "(USB_HOST off, or build the release)", ["USB_HOST"]))
    if cfg["USB_CONSOLE"] and cfg["USB_AUDIO"]:
        err.append(Issue("the serial console or USB audio, not both: the USB port has three data endpoints, USB MIDI "
                         "takes one and USB audio the other two", ["USB_CONSOLE", "USB_AUDIO"]))
    elif cfg["USB_AUDIO"]:
        warn.append(Issue("USB audio: EXPERIMENTAL, its audio quality on the FM-1 not checked yet (+12 KB pool)",
                          ["USB_AUDIO"]))
    return err, warn, note


def conflicts(cfg):
    """-> {key: [error messages]}: the items each validate() error concerns (an option's error marks its parent
    too: it may be folded away)"""
    out = {}
    for e in validate(cfg)[0]:
        for k in e.keys:
            out.setdefault(k, []).append(e)
            p = R.ITEMS[k].parent
            if p and e not in out.get(p, []):
                out.setdefault(p, []).append(e)
    return out


def flags(cfg):
    """-> ({C macro: value}, {generator env}) for this configuration"""
    out = {}
    for k, it in R.ITEMS.items():
        if not it.flag:
            continue
        v = cfg[k]
        if it.parent and not built(cfg, it.parent):
            v = it.default                              # (an option of an item left out: its default, ignored)
        if any(R.ITEMS[a].head and not cfg[a] for a in R.ancestors(k)) or (k in R.DRUMS_ONLY and not cfg["DRUMS"]):
            v = 0                                       # (a group's head off: none of its code, registry.h checks it)
        if k in R.FAST_NAMES:                           # (an engine: built or not, and where its hot code runs: core.h)
            if v == R.ENG_LEGACY_ON:                    # (a 1 set in code, not parsed: its place by default)
                v = R.eng_place(k)
            for nm in R.FAST_NAMES[k]:
                out[f"FAST_RAM_{nm}"] = int(v == R.ENG_RAM)
            v = int(v != R.ENG_OFF)
        out[it.flag] = v
    if cfg.get("ARENA", 0):
        out[R.ITEMS["ARENA"].flag] = 1                  # (on: the free SRAM, engines.c ARN_FREE; a KB of before too)
    out.update(R.ALWAYS_ON_TARGET)                  # (the second core and the stock supply: every target build, no item)
    if os.environ.get("OPTIMIST_ONE_CORE") == "1":
        out["FELUCCA_DUAL"] = 0   # (emulator packages only: one core runs ~4x faster there; the FM-1 default stays two cores)
    out["FELUCCA_USB_AUDIO"] = cfg["USB_AUDIO"]
    out["FELUCCA_CDC"] = cfg["USB_CONSOLE"]
    out["FELUCCA_LINK"] = out["FELUCCA_WIFI"]       # (Ableton Link comes with Wi-Fi: SYNC LINK, wifi/wifi_link.c)
    out["FELUCCA_SLICE"] = cfg["ENG_SLICE"]
    if not built(cfg, "DRUM_SAMPLED") or not any(cfg[k] for k in R.ITEMS if k.startswith("KIT_")):
        out["FELUCCA_DRUM_SAMPLED"] = 0
    skip = [R.ITEMS[k].env for k in set_keys() if not cfg[k]]
    if not out.get("FELUCCA_DRUM_SAMPLED", 1):
        skip.append("PERC")
    env = {"FELUCCA_SAMPLES_SKIP": ",".join(skip), "FELUCCA_SLICE": str(cfg["ENG_SLICE"])}
    return out, env


# the arena the build can hold at most
ARENA_MAX_KB = 320     # (more than this in the arena leaves no SRAM for the rest: the one error left, validate)
def arena_need(cfg):
    """-> {"kb": the arena this build needs together, "must": what it must hold at least (KB), "what": {name: bytes}}.
    What the firmware puts in it (every ARENA build: registry.h FELUCCA_FX_ARENA): the largest part's engine state, the
    three send lines (a new project has them all in its master slots), the inserts' own room and the largest other
    buffer. It must hold its largest block and the send lines together"""
    blocks = arena_blocks(cfg)
    what = {"a part's engine state": blocks.get("a part's engine state", 0)}
    must = max(blocks.values(), default=0)
    sends = arena_sends(blocks)                         # (every ARENA build: the extras, registry.h FELUCCA_FX_ARENA)
    if sends:
        what.update(sends)
        for k in ("the inserts' own room", "the largest limited insert's line"):
            if blocks.get(k):
                what[k] = blocks[k]
        other = {k: v for k, v in blocks.items() if k not in what}
        if other:
            k = max(other, key=other.get)
            what[k] = other[k]
        must = max(must, sum(sends.values()))
    what = {k: v for k, v in what.items() if v}
    return {"kb": -(-sum(what.values()) // 1024), "must": -(-must // 1024), "what": what}


def fnv32(text):
    h = 0x811C9DC5
    for b in text.encode():
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def cfg_hash(cfg):
    """of what the image holds: the items that change nothing in it (R.Item.no_image) are not in the hash; a
    retired item the image is still built as (R.HASH_AS) counts at that value, so the hash of a configuration
    does not move when it is retired (the same image: its check builds in costs.json stay valid)"""
    full = dict(R.HASH_AS, **cfg)
    for k in R.FAST_NAMES:                              # (an engine at its place by default: 1, as before the choice, so
        if full.get(k) == R.eng_place(k):               # a configuration's hash, its image's, stays what it was)
            full[k] = 1
    return fnv32("".join(f"{k}={full[k]}\n" for k in sorted(full)
                         if k in R.HASH_AS or not R.ITEMS[k].no_image)) & 0x7FFFFFFF


def cfg_bits(cfg):
    n = max(it.bit for it in R.ITEMS.values()) + 1
    bits = [0] * ((n + 6) // 7)
    for k, it in R.ITEMS.items():
        if built(cfg, k) and not it.no_image:
            bits[it.bit // 7] |= 1 << (it.bit % 7)
    return bits


def header(cfg, name="custom"):
    f, _ = flags(cfg)
    name = re.sub(r"[^A-Za-z0-9 +._-]", "", name or "custom")[:16]
    L = ["/* generated by tools/builder/configure.py: the build's configuration (firmware/src/core/registry.h) */",
         "#pragma once", f'#define FELUCCA_CFG_NAME "{name}"', f"#define FELUCCA_CFG_HASH {cfg_hash(cfg)}u",
         "#define FELUCCA_CFG_BITS {" + ", ".join(map(str, cfg_bits(cfg))) + "}"]
    target = {R.ITEMS[k].flag for k in R.ITEMS if R.ITEMS[k].target_only} | {"FELUCCA_LINK"} | set(R.ALWAYS_ON_TARGET)
    for k in sorted(f):
        suffix = "u" if k in ("FELUCCA_DLY_LEN", "FELUCCA_PUNCH_N", "FELUCCA_SL_LEN", "LCD_BAUD") else ""
        body = [f"#ifndef {k}", f"#define {k} {f[k]}{suffix}", "#endif"]
        L += (["#ifdef __PI32V2__"] + body + ["#endif"]) if k in target else body
    return "\n".join(L) + "\n"


def _eng_delta(costs, key, value):
    """an engine at a place (registry.py FAST_NAMES), on: what costs.json measured for it, else (its other place, not
    measured yet) its measured place moved: the render path's bytes (FAST_BYTES) out of or into the RAM code, and the
    flash that saves (FAST_FLASH_SAVED: measured; else a third of the bytes, the boot stream coding RAM code)"""
    it, place = R.ITEMS[key], R.eng_place(key)
    dd = costs.get("deltas", {}).get(key, {})
    if str(value) in dd:
        return dd[str(value)]
    at = {r: 0 for r in REGIONS} if it.default else dd.get(str(R.ENG_LEGACY_ON), dd.get(str(place)))
    if value == place or at is None:
        return at
    s = R.FAST_BYTES.get(key, 0)
    sign = 1 if value == R.ENG_RAM else -1
    return dict(at, ramtext=at.get("ramtext", 0) + sign * s, flash=at.get("flash", 0) - sign * R.fast_flash_saved(key))


def item_delta(costs, key, value):
    """bytes this item at this value adds to the default build, per region (None: not measured). An item switched
    off or down (a value under its default) never adds: a region it measured as growing counts 0 (the user, 2026-10-19,
    "clamp at 0": FX_DIST, MIDI_NOTEOUT, MISSING_WARN off made the default build 300-700 B larger in flash, the RAM code
    placed again, and the figure grew as a feature went). Not the other way: an item on that saves a region is often
    real (X909_CYM=2's smaller cymbals)"""
    if key in R.FAST_NAMES and value != R.ENG_OFF:   # (an engine on: at its place or the other, _eng_delta)
        return _eng_delta(costs, key, value)
    d = costs.get("deltas", {}).get(key, {}).get(str(value))
    if d is None and R.ITEMS[key].no_image:             # (a front or a record, MEMORY / RAM_PACKED: nothing in the image)
        return {r: 0 for r in REGIONS}
    if d is None or not value < R.ITEMS[key].default or key in NOT_A_FEATURE:
        return d
    return {r: min(0, b) for r, b in d.items()}


# items whose lower value is no feature switched off but another way to build the same firmware: their delta counts as
# measured, both ways (the clamp would count a delta that is bigger as 0 B)
#
# CANVAS_STRIP (default 2 = 31 rows: 0 = 124 rows +44,640 B of pool, 1 = 62 rows +14,880 B, 3 = 16 rows -7,200 B, costs measured 2026-11-02) and
# INS_RAM (0 = the insert code in the flash: +396 B of flash) are the same kind: a value under the default that costs
# MORE. The clamp counted them 0 and the builder showed no pool change at all for the canvas
NOT_A_FEATURE = ("CANVAS_STRIP", "INS_RAM")


def item_delta_alone(costs, key, value):
    """what this item at this value adds to the default build, per region, the computed terms (model_terms: the
    reverb's line buffer, the automation store) included: the figure the menu shows (None: not measured; a value that is not valid
    alone, e.g. the last reverb algorithm off, gets its delta only)"""
    d = item_delta(costs, key, value)
    if d is None:
        return None
    cfg = dict(defaults(), **{key: value})
    parent = R.ITEMS[key].parent
    if parent and not R.ITEMS[parent].is_choice:
        cfg[parent] = 1
    if validate(cfg)[0]:
        return d
    t = model_terms(cfg)
    return {r: d.get(r, 0) + t[r] for r in REGIONS}


@functools.lru_cache(maxsize=None)
def _pair_conds(name):
    return tuple((k, int(v)) for k, v in (c.split("=") for c in name.split(",")))


def pair_conds(name):
    """a costs.json "pairs" key ("MOTION=1,SECTIONS=4") -> [(key, value)]"""
    return list(_pair_conds(name))


def built_parent(cfg, key):
    """the item's parent (if any) is in the build"""
    parent = R.ITEMS[key].parent
    return not parent or built(cfg, parent)


def holds(cfg, key, v):
    """key = v in this configuration; a retired item: at the value every image is built as (R.HASH_AS: UI=1, the
    Optimist UI, since SLOOP's UI went, 2026-10-19), so the costs measured "with UI=1" apply to every build"""
    if key in R.RETIRED:
        return R.HASH_AS.get(key) == v
    return key in cfg and cfg[key] == v and built_parent(cfg, key)


def pair_delta(cfg, costs):
    """what items set together cost beyond their own deltas (costs.json "pairs": measured with all of them set,
    less each one's delta; and the groups, group_delta), per region"""
    out = group_delta(cfg, costs)
    for name, d in (costs or {}).get("pairs", {}).items():
        conds = _pair_conds(name)
        if all(holds(cfg, k, v) for k, v in conds):   # (an option of an item that is off is not in the build)
            for r in REGIONS:
                out[r] += d.get(r, 0)
    return out


def group_delta(cfg, costs):
    """costs.json "groups": items (off by default) that share code and state built with any of them (the insert
    effects' slots, core.h INS_FX): each one's delta pays the shared part, so with m of them on the sum counts it
    m - 1 times too often. Per region"""
    out = {r: 0 for r in REGIONS}
    groups = (costs or {}).get("groups", {})
    for name, g in groups.items():
        if "when" in g:
            continue
        for v in groups.values():                       # (a variant measured where its "when" holds: NAME|UI=1)
            if v.get("when") and v["members"] == g["members"] and all(holds(cfg, k, x) for k, x in v["when"].items()):
                g = v
        m = sum(1 for k in g["members"] if k in cfg and built(cfg, k) and cfg[k] != R.ITEMS[k].default)
        for r in REGIONS:
            out[r] -= max(0, m - 1) * g["shared"].get(r, 0)
    return out


REV_ROOM_LEN = 1559 + 1931 + 2389 + 2791 + 12 + 2   # fx.c REV_ROOM_LEN (the half-rate reverb, REV_HALF, was dropped 2026-10-29)


def rev_lines(cfg):
    """-> {region: bytes} of the reverb's shared line buffer (fx.c and reverb_alt.c rev_line): the size of the
    largest algorithm built (ROOM 8684 samples; PLATE / FDN8 / VTINY RV_N: 8192, FDN8 twice that in the pool; SPRING 4096), in the pool with REV_POOL, else in main RAM. A maximum over
    several items and a region that REV_POOL picks: no per-item delta adds up to it, so the estimate computes it
    (model_terms) and the measured deltas and pairs leave it out (measure_costs.py)"""
    f, _ = flags(cfg)

    def on(key):                                        # (flags: an option of an item left out has its default)
        return int(f.get(R.ITEMS[key].flag, 0) or 0)
    pool = on("REV_POOL")
    own = max(REV_ROOM_LEN if on("REV_ROOM") else 0, 4096 * on("SPRING"))
    n = own
    if on("REV_PLATE") or on("REV_FDN8") or on("REV_AIRWIN"):
        n = max(8192 << (1 if on("REV_FDN8") and pool else 0), own)
    if not on("FX_REVERB") or not n:
        n = 1                                           # (the C array's 1 when the reverb is not built)
    return {"pool" if pool else "ram": 2 * n}


AUTO_ITEMS = ("MOTION", "CHANCE", "SL24_XSTEP", "MICRO", "FILLS", "PLOCK", "COND")   # (backports24seq.h FELUCCA_AUTO)
AUTO_MAX, AUTO_NTRK = 128, 4                                                 # (seq/auto.h)
AUTO_LIST = 1 + 3 * AUTO_MAX                                                 # auto_list_t: n, AUTO_MAX x 3 bytes
AUTO_STORE = 4 + 4 + AUTO_NTRK * AUTO_LIST                                   # auto_store_t (seq/auto.c): psum, on + rsv, 4 lists
AUTO_AUX = 4                                                                 # storage/auto_proj.c AUTO_AUX
AUTO_ENC_MAX = 3 + AUTO_NTRK * (1 + 3 * AUTO_MAX)                            # auto.h AUTO_ENC_MAX (> the stepx form's)


# items only another item's code includes (the build is the same without it): counted only with that one. Found
# 2026-10-14 (SEQTOOLS and ED_SCREENS needed UI=1, the Optimist UI: always built since SLOOP's UI went, 2026-10-19)
NEEDS = {"GRIDS_LFO": {"DRUM_EDIT": 1}}   # (GRIDS_LFO: the lanes' LFOs, drums/lane_play.c DLFO_ON)
# (the web editor's ED_* commands: only editor.c's live dispatch calls them; ED_LIVE off builds ed_lite.c, none of them.
#  ED_MIRROR is the exception: ed_lite.c answers it too and display/lcd.c calls it in every build, 2026-10-22)
ED_ANY_EDITOR = ("ED_LIVE", "ED_MIRROR")
NEEDS = {**NEEDS, **{k: {**NEEDS.get(k, {}), "ED_LIVE": 1} for k in R.ITEMS if k.startswith("ED_") and k not in ED_ANY_EDITOR}}


def counted(cfg, key):
    """the item's measured delta is in this configuration's build (NEEDS_AUTO, NEEDS)"""
    if key in NEEDS_AUTO and not auto_built(cfg):
        return False
    return all(holds(cfg, k, v) if k in R.RETIRED else cfg.get(k) == v for k, v in NEEDS.get(key, {}).items())


def auto_built(cfg):
    """the automation store is in the build (backports24seq.h FELUCCA_AUTO)"""
    f, _ = flags(cfg)
    return any(f.get(R.ITEMS[k].flag, 0) for k in AUTO_ITEMS)


def auto_store(cfg):
    """-> {region: bytes} of the automation store's storage (seq/auto.h, auto.c, storage/auto_proj.c), built with ANY
    of MOTION, CHANCE, SL24_XSTEP (and so MICRO, FILLS, PLOCK: FELUCCA_AUTO): the working store (auto_w) and the
    undo's copy of a list in main RAM; the project buffers' stores (auto_aux x AUTO_AUX, auto_slot x 4 with
    SECTIONS 4) and the extras record's read buffer (sx_rbuf) in the pool. Shared by those
    items: the estimate counts it once (model_terms) and the measured deltas and pairs leave it out (measure_costs.py)"""
    f, _ = flags(cfg)
    if not any(f.get(R.ITEMS[k].flag, 0) for k in AUTO_ITEMS):
        return {"ram": 0, "pool": 0}
    slots = 0 if f.get("FELUCCA_SECTIONS", 16) > 4 else 4           # (SEC_LOGGED: the sections live in the log)
    rbuf = (4 + AUTO_ENC_MAX + 3) // 4 * 4 if not slots else (AUTO_ENC_MAX + 3) // 4 * 4   # (stepx_log.c / snapshots.c)
    return {"ram": AUTO_STORE + AUTO_LIST, "pool": (AUTO_AUX + slots) * AUTO_STORE + rbuf}


# engines.c part_mem: NPART engine states in the pool, a union of the engines built that keep one there; bytes of
# the array with each member alone (sizes.json "symbols", measure_costs.py holds these against every build it makes)
# (2026-10-26: JUNO, DRAWBAR, CZ and SLICE's states moved in from their static arrays: their measured item costs in
# costs.json still count those arrays as RAM until a re-measure)
PART_MEM = {"ENG_FM6": 11712, "ENG_PHYS": 38700, "ENG_GRAIN": 21060, "ENG_ACID": 1440, "ENG_PLAITS": 1920,
            "ENG_JUNO": 1332, "ENG_DRAWBAR": 2496, "ENG_CZ": 3120, "ENG_SLICE": 3072}   # (2026-10-26: part_mem, measured on main)
PART_MEM_NONE = 0                                   # (none built: not in the image)


def part_mem(cfg):
    """-> {region: bytes} of the synth parts' engine states (engines.c part_mem, the pool): the largest member
    built. A maximum: no per-item delta adds up to it (GRAIN off frees 9.3 KB, but not with PHYS on), so the estimate
    computes it (model_terms) and the measured deltas and pairs leave it out (measure_costs.py)"""
    sizes = [n for k, n in PART_MEM.items() if built(cfg, k)]
    return {"pool": max(sizes) // NPART * nparts(cfg) if sizes else PART_MEM_NONE}


NTRK = 4
# the effects memory (fx/insfx/fxi_mem.h, fxi.c, 2026-10-23): one pool memory in 1 KB units for the delay send's line and
# every line of the limited inserts; the line of each (KB, by item; XDLY's: two lines of XDLY_LEN samples) and its
# instance's state on the target (bytes: measure_costs.py holds the tables against the firmware's symbols)
FXM_UNIT = 1024        # fxi_mem.h FXA_U: 512 samples, bytes
FXM_OWNERS = 9         # fxi_mem.h FXI_NK: T1 T2 T3 DR, the master, the USB input, the returns A B C
# (2026-10-29: CHO DLY REV are inserts wherever their effect is built, fxi_send.c: keyed by the effect's item; the
# insert copies' items INS_CHORUS INS_REVERB INS_TDELAY are retired)
FXM_TYPES = ("FX_CHORUS", "INS_DELAY", "INS_JCHORUS", "INS_FLANGER", "INS_XDLY", "FX_REVERB", "FX_DELAY")   # (fxi_kern.c's
                                                                                                     # order, j)
FXM_NAMES = {"FX_CHORUS": "CHO", "INS_DELAY": "ECHO", "INS_JCHORUS": "JUNO CHORUS", "INS_FLANGER": "FLNG",
             "INS_XDLY": "XDLY", "FX_REVERB": "REV", "FX_DELAY": "DLY"}
FXM_STATE = {"FX_CHORUS": ("ifx_choi", 28), "INS_DELAY": ("ifx_echo", 20), "INS_JCHORUS": ("ifx_jcho", 36),
             "INS_FLANGER": ("ifx_flng", 24), "INS_XDLY": ("ifx_xdly", 40), "FX_REVERB": ("ifx_room", 52),
             "FX_DELAY": ("ifx_tdly", 60)}   # (CHO REV DLY: fxi_send.c, their structs on the 32-bit target)
FXM_COPIES = ("FX_CHORUS", "FX_REVERB", "FX_DELAY")   # (with an arena a block each: fxi_mem.h FXI_IS_CP)
FXM_CPN = 8            # engines.c FXM_CPN: the arena's blocks for them, at most
FXM_ROOM_KB = 19       # fxi_send.c IFX_RM_N: the ROOM's four lines and its diffusers, 9681 samples in 19 units
FXM_SYMBOLS = ("fxa_ins", "fxa_bm") + tuple(v[0] for v in FXM_STATE.values())   # (tools/build.py SIZED_SYMBOLS)
FXM_CHO_KB = 4         # fx.c CHO_LEN: the chorus send's line, 2048 samples
XD_STATE = FXM_STATE["INS_XDLY"][1]


def dly_send_kb(cfg, f=None):
    """the delay send's line in KB (fx.c dly_buf), 0 without the delay; f: the configuration's flags, when at hand"""
    f = f if f is not None else flags(cfg)[0]
    return int(f.get("FELUCCA_DLY_LEN", 0) or 0) * 2 // 1024 if f.get("FELUCCA_FX_DELAY") else 0


def fx_memory(cfg):
    """-> the effects memory (fx/insfx/fxi_mem.h) of this configuration: {"send", "cho": the send lines' KB, "own": the
    inserts' own KB (one line of each type built; CHO DLY REV: none, with an arena a block of it each, fxi_mem.h
    FXA_AUTO), "types": {item: {"kb": its line, "max": the most at once (an owner each at most, fxi_mem.h
    FXI_TN), "own": in the own room alone, "dly", "cho": what each send's line adds while it is in no master slot,
    "arena": the arena's blocks it may take besides (FXM_CPN, as many as the free RAM holds)}}} (types: the ones built)"""
    f, _ = flags(cfg)

    def on(key):
        return int(f.get(R.ITEMS[key].flag, 0) or 0)
    send, cho = dly_send_kb(cfg, f), FXM_CHO_KB if on("FX_CHORUS") else 0
    kb = {"FX_CHORUS": FXM_CHO_KB, "INS_DELAY": 8, "INS_JCHORUS": 4, "INS_FLANGER": 1,   # (JUNO CHORUS: its line 32-bit)
          "INS_XDLY": on("XDLY_LEN") // 256, "FX_REVERB": FXM_ROOM_KB, "FX_DELAY": send}   # (DLY: the delay's line)
    arena = bool(cfg.get("ARENA", 0))                    # (engines.c FXM_NEED: every ARENA build)
    types = [k for k in FXM_TYPES if on(k)]
    # (fxi_mem.h FXA_AUTO: CHO DLY REV none of their own; with an arena none at all, every line a block of its own:
    # engines.c FELUCCA_FXI_LINE_BLOCKS, 2026-10-31)
    own = 0 if arena else sum(kb[k] for k in types if k not in FXM_COPIES)
    out = {}
    for k in types:
        z = kb[k]
        cp = FXM_CPN if arena else 0
        out[k] = {"kb": z, "max": min(FXM_OWNERS, own // z + send // z + cho // z + cp),
                  "own": min(FXM_OWNERS, own // z), "dly": send // z, "cho": cho // z, "arena": cp}
    return {"send": send, "cho": cho, "own": own, "types": out}


def fx_memory_text(mem):
    """the "up to N at once" line of the effects memory (validate's notice)"""
    parts = []
    for k, t in mem["types"].items():
        more = [f"+{min(t['dly'], FXM_OWNERS - t['own'])} while the delay is in no master slot" if t["dly"] and
                t["own"] < FXM_OWNERS else "",
                f"+{min(t['cho'], FXM_OWNERS - t['own'])} while the chorus is in none" if t["cho"] and
                t["own"] < FXM_OWNERS else ""]
        more = ", ".join(m for m in more if m)
        if t.get("arena"):
            more = ", ".join(m for m in (more, "more in the arena's free RAM, a block each") if m)
        parts.append(f"{FXM_NAMES[k]} up to {t['own']}" + (f" ({more})" if more else ""))
    return (f"{mem['own']} KB of its own beside the delay's {mem['send']} KB line: at once (each type alone) "
            + "; ".join(parts))


def dly_arena(cfg):
    """-> {"units": the own room's KB (fxa_ins), "pool": bytes} of the effects memory (fx/insfx/fxi_mem.h, fxi.c): the
    own room, the instance tables (ifx_choi .. ifx_tdly: an owner each at most, no more than the memory holds) and the
    units' bitmap (fxi.c fxa_bm). Set by the five types, XDLY_LEN, DLY_LEN, FX_DELAY and FX_CHORUS together:
    no per-item delta adds up to it, so the estimate computes it (model_terms) and the measured deltas leave it out
    (measure_costs.py)"""
    mem = fx_memory(cfg)
    tables = {FXM_STATE[k][0]: t["max"] * FXM_STATE[k][1] for k, t in mem["types"].items()}
    bm = 4 * ((mem["send"] + mem["own"] + mem["cho"] + 31) // 32) if mem["types"] else 0
    return {"units": mem["own"], "tables": tables, "bitmap": bm,
            "pool": mem["own"] * FXM_UNIT + sum(tables.values()) + bm}


NPART = 3                  # core.h: the synth parts with the drums (PART_MEM's sizes are for NPART states)


def nparts(cfg):
    """-> core.h NPART: 3 synth parts, 4 without the drums (DRUMS off: track 4 a 4th synth part)"""
    return NPART if "DRUMS" not in R.ITEMS or built(cfg, "DRUMS") else NPART + 1


REV_AP = 556 + 441   # fx.c REV_A0 + REV_A1, the ROOM's diffusers rev_ap, samples
WAVE_MEM = 16 * 2032 * 2 + 2 * 1024 * 4   # eng_wave.c WT_MEM_BYTES: the table's copies and the frame builder's scratch
# the X0X machines' states on the FM-1 (x0x_drums.c x909 x808; build/sizes.json of the default profile, 2026-10-25): the
# 909's 5,344 B with its 6-bit samples (X909_CYM or X909_HAT 2: a field more in each of its 4 sample voices), 5,328 without
X909_STATE, X808_STATE = (5328, 5344), 3376
# FM6's sine table (eng_fm6.c FM6_SIN[1025], int32: 4,100 B; in the arena while a part plays FM6, 2026-10-26): what the
# pool gives back without it, 4,096 B (its packing; measured 2026-10-26 on sd-card-mots and the reference profile)
FM6_SIN = 4096


def arena_bufs(cfg):
    """-> {name: bytes} the other buffers the arena holds only while a project uses them (core/arena_bufs.c; in the
    pool, static, without ARENA): each track's SLICER recording, WAVE's table, the X0X machines' states, the punch's
    ring (held while the project's PUNCH switch is on), FM6's sine table (while a part plays FM6)"""
    f, _ = flags(cfg)

    def on(key):
        return int(f.get(R.ITEMS[key].flag, 0) or 0)
    out = {"a track's SLICER recording": 2 * on("SL_LEN") if built(cfg, "FX_SLICER") else 0,
           "WAVE's table": WAVE_MEM if built(cfg, "ENG_WAVE") else 0,
           "the X0X 909's state": X909_STATE[on("X909_CYM") == 2 or on("X909_HAT") == 2] if built(cfg, "DRUM_X0X909") else 0,
           "the X0X 808's state": X808_STATE if built(cfg, "DRUM_X0X808") else 0,
           "the PUNCH ring": 2 * on("PUNCH_N") if built(cfg, "FX_PUNCH") else 0,
           "FM6's sine table": FM6_SIN if built(cfg, "ENG_FM6") else 0}
    return {k: v for k, v in out.items() if v}


# the tables in the pool (core.h IN_POOL_AUDIO, always since TAB_IN_POOL was retired, 2026-10-29): a layout, not an item.
# With the arena's extras (FELUCCA_FX_ARENA) FM6's sine table is an arena block, not moved (FM6_SIN[1025] int32 4,100 B)
FM6_SIN_RAM = 4100


def tab_pool(cfg):
    """-> {region: bytes} what the always-pooled tables move from RAM to the pool beyond the defaults' set (model_terms)"""
    fx_arena = cfg.get("ARENA", 0) > 0              # (every ARENA build: the extras)
    moved = -(FM6_SIN_RAM if fx_arena and built(cfg, "ENG_FM6") else 0)
    return {"ram": -moved, "pool": moved}


def set_keys():
    """the builder's sample sets (registry.py SET_*): the generator's set names by item"""
    return [k for k in R.ITEMS if k.startswith("SET_")]


def arena_mem(cfg):
    """-> {region: bytes} of the arena (ARENA: core/arena.c, fx/fx_arena.c, core/arena_bufs.c) against the fixed
    memories it replaces: none of its own (the free SRAM, taken at boot: engines.c ARN_FREE, the estimate's free shows
    it), less the parts' engine states (part_mem), the delay and chorus sends' lines, the reverb's line (rev_lines: in
    main RAM without REV_POOL) and the ROOM's diffusers after it, the inserts' own room (fx_memory), the SLICER's
    recordings (a track each), WAVE's table and the X0X machines' states (arena_bufs); {} with ARENA off. The code and
    bookkeeping it adds is its measured delta (costs.json)"""
    if not cfg.get("ARENA", 0):
        return {}
    rev, mem, bufs = rev_lines(cfg), fx_memory(cfg), arena_bufs(cfg)
    lines = (mem["send"] + mem["cho"] + mem["own"]) * 1024 + rev.get("pool", 0) + rev_ap(cfg)
    other = sum(v * (NTRK if k.startswith("a track's") else 1) for k, v in bufs.items())
    return {"pool": -part_mem(cfg)["pool"] - lines - other, "ram": -rev.get("ram", 0)}


def rev_ap(cfg):
    """-> bytes of the ROOM's diffusers (fx.c rev_ap, always in the pool; in the reverb's block with ARENA), as the
    pool holds them: 4-aligned (the default profile with ROOM, the SLICER and WAVE, 2026-10-25: 1,996 B of pool for
    the 1,994 B array)"""
    if not built(cfg, "FX_REVERB") or not built(cfg, "REV_ROOM"):
        return 0
    return (2 * REV_AP + 3) & ~3


def arena_blocks(cfg):
    """-> {name: bytes} the blocks the arena places (a part's engine state, each effect's line, the other buffers;
    validate: each must fit)"""
    rev, mem = rev_lines(cfg), fx_memory(cfg)
    out = {"a part's engine state": part_mem(cfg)["pool"] // nparts(cfg), "the delay's line": mem["send"] * 1024,
           "the chorus send's line": mem["cho"] * 1024,
           "the reverb's line": sum(rev.values()) + rev_ap(cfg) if built(cfg, "FX_REVERB") else 0,
           "the inserts' own room": mem["own"] * 1024,
           "the largest limited insert's line": max((t["kb"] for k, t in mem["types"].items() if k not in FXM_COPIES),
                                                     default=0) * 1024 if cfg.get("ARENA", 0) else 0, **arena_bufs(cfg)}
    return {k: v for k, v in out.items() if v}


SEND_LINES = ("the delay's line", "the chorus send's line", "the reverb's line")


# ---- the arena one instance of an item takes (the user, 2026-10-31: "list the arena ram (assuming one instance) and allow
# sorting on that"). One instance = one part's engine state, one effect's line, one buffer: the same numbers arena_blocks
# places (PART_MEM / NPART, fx_memory, arena_bufs), not a second table. A size option (DLY_LEN, XDLY_LEN, SL_LEN, PUNCH_N,
# the reverb algorithms, the X909 sample widths) shows the block it sizes. 0: the item keeps nothing in the arena.
# None ("?"): it does, but the size is not defined by a constant in this source: the features lent by the includer (core/feat_mem.c
# FMEM_FEATS: BT AUDIO, the mirror's Bluetooth ring, Wi-Fi Link, its access point) are sized in fm1-base (plat.h
# plat_*_mem_bytes), which this checkout does not carry
ARENA_UNKNOWN = {
    "BT_AUDIO": "a lent block, sized in fm1-base (plat_bta_mem_bytes), not in this source",
    "ED_MIRROR": "its Bluetooth ring is lent only by the includer (MIR_LEND; MIR_BLE_RING 8192 B when it is)",
    "WIFI_LINK": "a lent block, sized in fm1-base (plat_link_mem_bytes), not in this source",
    "WIFI_AP": "a lent block, sized in fm1-base (plat_wifi_ap_mem_bytes), not in this source",
}


def _reverb_arena(cfg, algo=None):
    """-> bytes of the reverb send's block (the line and, with ROOM, its diffusers): the algorithms as set, or algo alone"""
    c = dict(cfg, FX_REVERB=1)
    if algo:
        c.update({a: 0 for a in R.REV_ALGOS if a in c}, **{algo: 1})
    return sum(rev_lines(c).values()) + rev_ap(c)


def item_arena(cfg, key):
    """-> bytes of the arena one instance of the item takes (see above), 0 for none, None when it is not known"""
    if key in ARENA_UNKNOWN:
        return None
    if key in PART_MEM:
        return PART_MEM[key] // NPART + (FM6_SIN if key == "ENG_FM6" else 0)
    if key in ("FX_DELAY", "DLY_LEN"):
        return 2 * cfg["DLY_LEN"]
    if key in ("INS_XDLY", "XDLY_LEN"):
        return 4 * cfg["XDLY_LEN"]                      # (two lines of XDLY_LEN samples, 2 B each)
    if key in ("FX_SLICER", "SL_LEN"):
        return 2 * cfg["SL_LEN"]
    if key in ("FX_PUNCH", "PUNCH_N"):
        return 2 * cfg["PUNCH_N"]
    if key == "FX_CHORUS":
        return FXM_CHO_KB * 1024
    fixed = {"INS_DELAY": 8, "INS_JCHORUS": 4, "INS_FLANGER": 1}   # (fx_memory: KB of a line)
    if key in fixed:
        return fixed[key] * 1024
    if key == "FX_REVERB":
        return _reverb_arena(cfg)
    if key in R.REV_ALGOS:
        return _reverb_arena(cfg, key)
    if key == "ENG_WAVE":
        return WAVE_MEM
    if key in ("DRUM_X0X909", "X909_CYM", "X909_HAT"):
        return X909_STATE[cfg.get("X909_CYM", 0) == 2 or cfg.get("X909_HAT", 0) == 2]
    if key == "DRUM_X0X808":
        return X808_STATE
    return 0


def arena_items(cfg):
    """-> {key: bytes | None} item_arena of every item, as the configuration sets its sizes (the builder's Arena column)"""
    return {k: item_arena(cfg, k) for k in R.ITEMS}




def arena_sends(blocks):
    """-> {name: bytes} the send lines among arena_blocks: a new project has the chorus, the delay and the reverb in
    its master slots (fx/fx_arena.c places each line while its type is in one), so the arena must hold them together,
    or the ones that do not fit are refused and their returns stay silent (the FM-1, 2026-10-28: ARENA 32 with a
    32 KB delay line and a 32 KB reverb line, no reverb and no chorus at all)"""
    return {k: blocks[k] for k in SEND_LINES if blocks.get(k)}


# ---- what the arena will get, and what fits in it (the user, 2026-10-29: the builder shows the arena's size and gives a
# feel for what fits). On the device the arena is the SRAM the link leaves free (engines.c ARN_BOOT: _pool_end .. _guard0
# less the 8 KiB spare = undo_ring) less the undo history's least in the SRAM (ARN_UNDO_KEEP)
ARN_UNDO_LEAST = 16 * 1024      # engines.c ARN_UNDO_LEAST: the undo's least in the SRAM without the cache RAM
ARN_UNDO_SRAM = 1024            # engines.c ARN_UNDO_SRAM: its SRAM part with the cache RAM, at least
CACHE_RAM_BYTES = {2: 8192, 4: 16384, 7: 28672}   # hal/fm1_cache.h: the ways as RAM (4 KB each); the tested free part is a little less
ARENA_ESTIMATE_KB = 4           # the estimate's stated tolerance (KB) against a real build of a configuration the anchors know
STG_RESERVE = 16600             # seq/undo.c, engines.c STG_MEM: the save / load staging buffers past the arena and its undo keep
                                # (the undo history's while nothing stores), with the section log (SECTIONS 8, 16); measured on
                                # the user's mots-config (st_buf, proj_tmp, sec_rb, sx_rbuf, two automation stores)


def stg_reserve(cfg):
    """-> bytes of the STORAGE reserve a build keeps after the arena (0: its buffers in the pool, no arena or no log)"""
    return STG_RESERVE if cfg.get("ARENA", 0) and cfg.get("SECTIONS", 16) > 4 else 0


def arena_undo_keep(cfg):
    """-> bytes the undo history keeps in the SRAM behind the arena (engines.c ARN_UNDO_KEEP): 16 KB, or with the cache
    RAM (CACHE_RAM, its test passed) 1 KB at least: the arena gets the SRAM the cache RAM frees"""
    ways = cfg.get("CACHE_RAM", 0)
    free = CACHE_RAM_BYTES.get(ways, ways * 4096)
    if not ways:
        return ARN_UNDO_LEAST
    return ARN_UNDO_LEAST - free if free + ARN_UNDO_SRAM < ARN_UNDO_LEAST else ARN_UNDO_SRAM


ARN_SPARE = 0   # engines.c ARN_SPARE: the arena runs to the stacks' guard band (2026-10-30; the 8 KiB spare is the link's check only)


def sram_free(total):
    """the SRAM the link leaves free (the pool's end to the stacks' guard band, less ARN_SPARE): what the arena is cut from"""
    return max(0, limits_in_use()["sram"] - ARN_SPARE - used(total, "sram"))


def undo_info(cfg, total):
    """-> {"ring", "arena", "keep", "cache"} the undo history: with ARENA it keeps only ARN_UNDO_KEEP of the SRAM (and the
    cache RAM's ways, CACHE_RAM) and borrows what the arena does not use, giving it back (seq/undo.c undo_yield): it never
    takes the arena's room; without, its ring is the free SRAM less the spare (undo_ring)"""
    if cfg.get("ARENA", 0):
        ways = cfg.get("CACHE_RAM", 0)
        return {"ring": undo_ring(total), "arena": True, "keep": arena_undo_keep(cfg),
                "cache": CACHE_RAM_BYTES.get(ways, ways * 4096) if ways else 0}
    return {"ring": undo_ring(total), "arena": False, "keep": 0, "cache": 0}


def undo_text(cfg, total):
    """the undo line of the budget (the console's, the menu's): with ARENA its keep and that it borrows the arena's free top"""
    u = undo_info(cfg, total)
    if not u["arena"]:
        return f"undo ring {u['ring']:,} B"
    cache = f" + cache RAM {u['cache'] // 1024} KB" if u["cache"] else ""
    return f"undo keeps {_kb1(u['keep'])} KB{cache}, borrows the arena's free top"


def arena_size(cfg, total, exact=False):
    """-> {"bytes", "kb", "free", "keep", "exact"} the arena a build gets: the free SRAM (sram_free of the total) less the
    undo's keep; exact: the total is a real build's sizes (else the estimate); None with ARENA off"""
    if not cfg.get("ARENA", 0) or not total:
        return None
    free, keep, stg = sram_free(total), arena_undo_keep(cfg), stg_reserve(cfg)
    n = max(0, free - keep - stg) & ~7
    return {"bytes": n, "kb": n // 1024, "free": free, "keep": keep, "stg": stg, "exact": bool(exact)}


def _kb1(n):
    return f"{n / 1024:.1f}"


def arena_fits(cfg):
    """-> {"rows": [(name, bytes each, count, note)], "all": bytes of everything at once, "typical": ...} what a project can put in
    the arena, from the builder's own model (arena_blocks: the parts' engine states, the sends, the inserts' room, the other buffers). "all" = every row at its count: a project using all of it at once"""
    c = cfg                                         # (the effects memory is always auto: fx_memory's rule)
    blocks = arena_blocks(c)
    rows = []
    sends = arena_sends(blocks)
    if sends:
        rows.append(("the 3 sends' lines" if len(sends) == 3 else "the sends' lines (" + ", ".join(
            k.split("'")[0].replace("the ", "") for k in sends) + ")", sum(sends.values()), 1,
            "always placed while the sends are used"))
    eng = max((k for k in PART_MEM if built(cfg, k)), key=PART_MEM.get, default=None)
    if blocks.get("a part's engine state"):
        rows.append(("a part's " + (R.ITEMS[eng].label.split(" (")[0] if eng else "engine") + " state",
                     blocks["a part's engine state"], nparts(cfg), "the largest engine built, a part each"))
    if blocks.get("the inserts' own room"):
        rows.append(("the inserts' room", blocks["the inserts' own room"], 1, "the limited inserts' lines"))
    if blocks.get("the largest limited insert's line"):
        rows.append(("a limited insert's line", blocks["the largest limited insert's line"], 1,
                     "the largest built (ECHO JUNO FLNG XDLY: each copy its own line's size)"))
    for k, v in arena_bufs(cfg).items():
        if k == "FM6's sine table":
            continue                                    # (held with FM6's engine state: a few KB, in the table's note)
        rows.append((k, v, 1, "a track each" if k.startswith("a track's") else "while the project uses it"))
    total = sum(b * n for _, b, n, _ in rows)
    return {"rows": rows, "all": total}


HINT = ("a typical project uses the sends and its parts' engines; WAVE, SLICER, PUNCH, the X0X states only when it plays them",)


def arena_text(cfg, total, exact=False):
    """-> [str] (<= 78 columns) the arena panel: its size, what fits, whether all of it at once does; [] with ARENA off"""
    a = arena_size(cfg, total, exact)
    if not a:
        return []
    f = arena_fits(cfg)
    how = "from the last real build" if a["exact"] else "estimate"
    less = f"undo {_kb1(a['keep'])} + staging {_kb1(a['stg'])} KB" if a["stg"] else f"undo keep {_kb1(a['keep'])} KB"
    out = [f"arena {'' if a['exact'] else '~'}{a['kb']} KB ({how}: free SRAM {_kb1(a['free'])} KB - {less})"]
    for name, b, n, note in f["rows"]:
        out.append(f"  {name:30.30s} {(str(n) + ' x ' if n > 1 else '').rjust(4)}{_kb1(b):>6s} KB = {_kb1(b * n):>6s} KB")
    tot = f["all"] / 1024
    out.append(f"  {'all at once':30s}{'':4s}{'':>6s}    = {tot:6.1f} KB of {a['kb']} KB")
    if f["all"] > a["bytes"]:
        out.append(f"  a project using all of it at once is refused something (MEM FULL) by {(f['all'] - a['bytes']) / 1024:.1f} KB")
    else:
        out.append("  all of it at once fits")
    out.extend(textwrap.wrap(HINT[0], 76, initial_indent="  ", subsequent_indent="  "))
    return out


def ram_engine_code(cfg):
    """-> bytes of hot code the built engines run from RAM (their place ENG_RAM: registry.py FAST_BYTES)"""
    fl = flags(cfg)[0]
    return sum(R.FAST_BYTES.get(k, 0) for k, names in R.FAST_NAMES.items() if fl.get(f"FAST_RAM_{names[0]}") and built(cfg, k))


def model_terms(cfg):
    """-> {region: bytes} the computed part of the estimate, against the default build: what is not a sum of
    per-item deltas (the reverb's line buffer, rev_lines; the automation store, auto_store; the engines' part
    states, part_mem; the delay arena, dly_arena; the arena for both, arena_mem; the pooled tables' set, tab_pool)"""
    d = defaults()
    arena = ({"pool": dly_arena(cfg)["pool"]}, {"pool": dly_arena(d)["pool"]})
    terms = [(rev_lines(cfg), rev_lines(d)), (auto_store(cfg), auto_store(d)), (part_mem(cfg), part_mem(d)), arena,
             (arena_mem(cfg), arena_mem(d)), (tab_pool(cfg), tab_pool(d))]
    return {r: sum(a.get(r, 0) - b.get(r, 0) for a, b in terms) for r in REGIONS}


def cfg_diff(cfg):
    """-> {key: value} the image items a configuration sets away from the defaults (costs.json "checks" and
    build/exact-sizes.json keep this: the configuration a real build was made as, an anchor of the estimate)"""
    d, e = effective(defaults()), effective(cfg)
    return {k: v for k, v in e.items() if v != d[k]}


def anchor_record(cfg):
    """-> {"cfg": cfg_diff, "built_hash"}: what costs.json and build/exact-sizes.json keep of a real build's
    configuration; built_hash checks it is read back as it was built (a registry default changed since: not)"""
    diff = cfg_diff(cfg)
    return {"cfg": diff, "built_hash": f"{cfg_hash(dict(defaults(), **diff)):08x}"}


def anchor_cfg(rec):
    """-> the configuration of an anchor_record, None when it no longer reads back as built"""
    if not isinstance(rec, dict) or not isinstance(rec.get("cfg"), dict) or any(k not in R.ITEMS for k in rec["cfg"]):
        return None
    cfg = dict(defaults(), **rec["cfg"])
    return cfg if f"{cfg_hash(cfg):08x}" == rec.get("built_hash") else None


def effective(cfg):
    """-> {key: value} what the image is built with: every image item, an option of an item left out at its default
    (the build ignores it), so two configurations that build the same image compare equal"""
    full = dict(defaults(), **cfg)
    return {k: full[k] if not it.parent or built(full, it.parent) else it.default
            for k, it in R.ITEMS.items() if not it.no_image}


def distance(a, b):
    """the number of image items two configurations build differently"""
    ea, eb = effective(a), effective(b)
    return sum(1 for k in ea if ea[k] != eb[k])


_ANCHOR_MEMO = {}          # (proposals() fills it for one costs while it runs: the anchors do not change within it)


def anchors(costs, exclude=(), extra=()):
    """-> [(name, cfg, real sizes)] the configurations whose real build is known: the defaults (costs.json "base"),
    the check configurations measured with the deltas (costs.json "checks", each with the configuration it was built
    as) and extra, more of them (configure.py: the real builds made from this very source, build/exact-sizes.json);
    exclude: names left out (tests/builder_test.py: a check configuration is estimated without its own build)"""
    memo = _ANCHOR_MEMO.get(id(costs)) if not exclude and not extra else None
    if memo is not None:
        return list(memo)
    out = [("defaults", defaults(), costs["base"])] if "defaults" not in exclude else []
    for n, e in sorted(costs.get("checks", {}).items()):
        acfg = anchor_cfg(e)
        if n not in exclude and e.get("sizes") and acfg:
            out.append((n, acfg, e["sizes"]))
    if not exclude and not extra and id(costs) in _ANCHOR_MEMO:
        _ANCHOR_MEMO[id(costs)] = out
    return out + list(extra)


# the estimate's margin per item between the configuration and the anchor it is estimated from (budget): the
# error of a change grows with the number of items changed (LTO shares and inlines differently with what else is
# built). Flash only: 150 single changes from a user's configuration (2026-10-14) were off by 0.2 KB of flash at the
# median and 1.5 KB at the 90th percentile, either way, while RAM, pool and RAM code were 0 at the median (a RAM
# margin of 32 B an item put the BLE test builds, 18 items from user-default, 576 B over). Errs over: a
# configuration predicted to fit must fit
ANCHOR_MARGIN = {"flash": 256, "ram": 0, "pool": 0, "ramtext": 0}
ALIGN_NOISE = 32       # flash bytes a change can move by alignment alone (measured: SNAPSHOTS 4 -> 2 +4 B, PAGE_LINES and
                       # ED_STEPLANE off +8 B, 2026-10-18): growth within it is not an item adding code (_grown)


def additive(cfg, costs):
    """-> (total, items, unmeasured) the sum model: the default build, each item's measured delta, the pairs, the
    computed terms. Its error grows with the distance from the defaults (budget anchors it nearer)"""
    total = dict(costs["base"])
    items, missing = {}, []
    for k, it in R.ITEMS.items():
        if it.parent and not built(cfg, it.parent):
            continue                                    # (the parent's own delta covers its options)
        if cfg[k] == it.default:
            continue
        d = item_delta(costs, k, cfg[k])
        if d is None:
            missing.append(k)
            continue
        if not counted(cfg, k):
            continue                                    # (the firmware builds it as off: measured where it is on)
        items[k] = d
        for r in REGIONS:
            total[r] += d.get(r, 0)
    pairs, terms = pair_delta(cfg, costs), model_terms(cfg)
    for r in REGIONS:
        total[r] += pairs[r] + terms[r]
    return total, items, missing


# a probe (measure_costs.py PROBE: a check configuration with 4 items changed by a hash, costs.json "checks" NAME~)
# counts this many items further than it is: a profile's own build PROBE_PENALTY items away or less wins over it.
# A probe sits on random items, where the sum model is least right (2026-10-26: the default profile less ENG_FM6
# anchored on dev~, 6 items away, over its own build, 7 away, and its RAM came 176 B over the profile's although the
# real build has 32 B less: INS_SFOLD's RAM among the other shaper inserts 144 B off)
PROBE_PENALTY = 2


def nearest_anchor(cfg, costs, exclude=(), extra=()):
    """-> (name, cfg, real sizes, distance) the anchor (anchors) the fewest items away (a probe PROBE_PENALTY
    further); at the same distance the later one (a real build from this source over costs.json's)"""
    best, best_rank = None, None
    for n, acfg, real in anchors(costs, exclude, extra):
        d = distance(cfg, acfg)
        rank = d + (PROBE_PENALTY if n.endswith("~") else 0)
        if best is None or rank <= best_rank:
            best, best_rank = (n, acfg, real, d), rank
    return best


def _grown(cfg, acfg, costs):
    """the items whose change from the anchor's configuration to cfg adds flash (the margin counts these only: an
    item switched OFF never raises the estimate, 2026-10-16: a margin per changed item made switching a 40 B feature off
    show 216 B MORE flash, and a user saw the figure grow as he removed features); a growth within ALIGN_NOISE is alignment,
    not code (2026-10-19: three items measured +4..+8 B off and drew the whole 256 B margin)"""
    ic, ia = additive(cfg, costs)[1], additive(acfg, costs)[1]
    ec, ea = effective(cfg), effective(acfg)
    return sum(1 for k in ec if ec[k] != ea[k] and ic.get(k, {}).get("flash", 0) - ia.get(k, {}).get("flash", 0) > ALIGN_NOISE)


# the context items: what they change depends on most of the rest of the configuration, so no one delta from the
# defaults stands for them. Each is built flipped on every check configuration too (measure_costs.py measure_checks:
# costs.json "checks" NAME "contexts" KEY: {"from", "to", "delta"}, the real difference), and an estimate that
# crosses one from its anchor counts the difference measured on the check configuration nearest to it (among those
# agreeing on the other context items) in place of the sum model's. Measured 2026-10-30 (release, gpufarm, 3e0c9439):
#   LAYOUT 1: -0.9 KB on the defaults (the SD card items that made it differ were removed 2026-11-04)
CONTEXT_ITEMS = ("LAYOUT",)


def _ctx_class(key, v):
    """a context item's value as the measures agree on it: LAYOUT is the big step (1) or not: the other steps (normal, the
    sample-slot x project grid) build the same code, a few hundred bytes apart (a step no check configuration has
    made 4 projects drop the measured delta and show less flash than 16 projects)"""
    return (v == 1) if key == "LAYOUT" else v


def _context_measured(key, a, b, near, costs, exclude):
    """-> {region: bytes} what key going from a to b really changed on the check configuration nearest to near among
    those that agree with it on the other context items (2026-10-30: LAYOUT measured without the card, +0.8 KB, stood
    for LAYOUT with it, -29 KB), None when none has it measured (the sum model's then)"""
    en, best, best_rank = effective(near), None, None
    for n, e in (costs or {}).get("checks", {}).items():
        c = e.get("contexts", {}).get(key)
        if n in exclude or not c or c.get("delta") is None or {c["from"], c["to"]} != {a, b}:
            continue
        acfg = anchor_cfg(e)
        if acfg is None:
            continue
        ea = effective(acfg)
        if any(_ctx_class(k, ea.get(k)) != _ctx_class(k, en.get(k)) for k in CONTEXT_ITEMS if k != key):
            continue                                    # (measured where another context item differs: no measure of it here)
        rank = distance(near, acfg)
        if best_rank is None or rank < best_rank:
            sign = 1 if (c["from"], c["to"]) == (a, b) else -1
            best, best_rank = {r: sign * c["delta"].get(r, 0) for r in REGIONS}, rank
    return best


def context_terms(cfg, acfg, costs, exclude=()):
    """-> {region: bytes} the context items (CONTEXT_ITEMS) cfg sets otherwise than its anchor acfg: each one's real
    difference measured nearest to cfg (_context_measured) less what the sum model says it changes there (its delta, its
    pairs, its computed terms), one item at a time from cfg towards the anchor"""
    out = {r: 0 for r in REGIONS}
    ec, ea = effective(cfg), effective(acfg)
    cur = dict(defaults(), **cfg)
    for k in CONTEXT_ITEMS:
        if k not in ec or ec[k] == ea[k]:
            continue
        nxt = dict(cur, **{k: ea[k]})
        got = _context_measured(k, ea[k], ec[k], cur, costs, exclude)
        if got is not None:
            hi, lo = additive(cur, costs)[0], additive(nxt, costs)[0]
            for r in REGIONS:
                out[r] += got[r] - (hi[r] - lo[r])
        cur = nxt
    return out


def budget(cfg, costs, anchor=True, exclude=(), exact=None, extra_anchors=()):
    """-> {"total": {region: bytes}, "items": {key: {region: delta}}, "unmeasured": [keys], "exact": bool,
    "anchor": name, "distance": items} (None without costs): the total is exact, the sizes of this configuration's
    last real build from this source, when given (configure.budget looks it up), else the estimate: the real build
    of the nearest configuration whose build is known (nearest_anchor: the defaults, a check configuration,
    extra_anchors) plus what the sum model (additive) says the items that differ change, plus ANCHOR_MARGIN per such
    item. Anchored, the items both share cost what they really cost together (LTO shares code between features, so a
    configuration many items from the defaults is several KB off the plain sum: 17.7 KB of flash for a user's,
    2026-10-14); anchor=False: the plain sum (measure_costs.py measures the pairs against it). The per-item deltas
    stay the sum model's either way"""
    if not costs:
        return None
    total, items, missing = additive(cfg, costs)
    ref = nearest_anchor(cfg, costs, exclude, extra_anchors) if anchor and not exact else None
    if exact:
        total = dict(exact)
    elif ref:
        name, acfg, real, dist = ref
        at, _, _ = additive(acfg, costs)
        grown = _grown(cfg, acfg, costs)
        ctx = context_terms(cfg, acfg, costs, exclude)
        total = {r: real[r] + total[r] - at[r] + ctx[r] + ANCHOR_MARGIN[r] * grown for r in REGIONS}
    return {"total": total, "items": items, "unmeasured": missing, "exact": bool(exact),
            "anchor": ref[0] if ref else None, "distance": ref[3] if ref else None}


# ---- the undo history's ring (information: what RAM and the pool leave)
def undo_ring(total):
    """the undo history's ring a build leaves (app.ld _undo_*): the SRAM after the pool, less its 8 KiB spare
    (docs/MEMORY-MAP.md 2.1); never below 0"""
    return max(0, limits_in_use()["sram"] - SPARE["sram"] - used(total, "sram"))


def sram_parts_text(total):
    """the SRAM's three measured parts as one line (the menu and the CLI show it under the SRAM bar): ram + pool + ram code"""
    return " + ".join(f"{n} {total[k]:,}" for k, n in (("ram", "ram"), ("pool", "pool"), ("ramtext", "ram code"))) + " B"


def fits(total, cfg=None):
    """-> {budget: (used, capacity, over)} for flash and the packed SRAM: over > 0 overflows (SRAM keeps its 8 KiB
    spare); the flash by the configuration's layout (LAYOUT: limits_in_use flash_by_layout). Not a release build's
    sizes.json app_slot: fm1-base's write_sizes puts its normal APP_SLOT there whatever app_end the build had (an SD card
    mode build said 581,564 B; its check said "exceeds the app slot (823228 B)")"""
    out, lim = {}, limits_in_use()
    for b in BUDGETS:
        u, cap = used(total, b), lim[b] - SPARE[b]
        if b == "flash" and cfg and cfg.get("LAYOUT") in lim["flash_by_layout"]:
            cap = lim["flash_by_layout"][cfg["LAYOUT"]] - SPARE["flash"]
        elif b == "flash" and cfg and cfg.get("LAYOUT") in R.LAYOUT_STEPS:
            # a step the build system gives no slot (the release builds only 1 and 3; its other steps' end = the classic
            # end): the design table's room, not the normal slot, or 4 projects showed no more room than 16
            cap = layout_room(cfg["LAYOUT"]) - SPARE["flash"]
        out[b] = (u, cap, u - cap)
    return out


def kit_keys():
    """the sampled kits (they share the PERC samples)"""
    return [k for k in R.ITEMS if k.startswith("KIT_")]


def kit_shared(costs):
    """-> {region: bytes} the sampled kits share (the PERC samples): what all of them together add to a build
    that has none (their own deltas plus costs.json's "pairs" entry for all of them off); None: not measured"""
    kits = kit_keys()
    for name, d in (costs or {}).get("pairs", {}).items():
        conds = pair_conds(name)
        if kits and sorted(k for k, _ in conds) == sorted(kits) and all(v == 0 for _, v in conds):
            own = [item_delta(costs, k, 0) or {} for k in kits]
            return {r: -(d.get(r, 0) + sum(o.get(r, 0) for o in own)) for r in REGIONS}
    return None


def is_last_kit(cfg, key):
    """the item is the only sampled kit still in the build: switching it off drops the shared samples"""
    return key in kit_keys() and built(cfg, key) and not any(built(cfg, k) for k in kit_keys() if k != key)


# the smallest setting of a choice with no "off" whose numbers do not say it (the smallest non-zero value is not the
# smallest build): the 6-bit 909 hats and the 256-point X0X tanh table (costs.json: -4,928 / -3,092 B of flash
# against 1, 2026-10-24)
SMALLEST = {"X909_HAT": 2, "X0X_TANH": 2}


def off_value(key):
    """what switching the item off means: 0 for a switch; for a choice its "off" choice where it has one (BLE_CENTRAL,
    X909_CYM, SNAPSHOTS, CACHE_RAM, ...), else its smallest setting: SMALLEST, else the smallest non-zero value
    (the sizes, DLY_LEN .. SECTIONS; a 0 that is no "off" is not taken: LCD_BAUD's 60 MHz).
    Until 2026-10-24 every choice took the smallest non-zero value: BLE_CENTRAL MINIMAL (3) went to FULL (1), the
    6-bit hats to the 8-bit ones, both bigger"""
    it = R.ITEMS[key]
    if not it.is_choice:
        return 0
    off = [v for v, label in it.choices if label == "off" or label.startswith("off:")]
    if off:
        return off[0]
    return SMALLEST.get(key, min((c[0] for c in it.choices), key=lambda v: v if v else 1 << 30))


def savings_of(cfg, costs):
    """what switching each built item off (or its smallest choice) would save, per region: the sum model with it and
    without it, so the options already off are not saved twice (a parent's delta is measured with its options at
    their defaults: the web editor read +24.5 KB with live editing off, 7.2 KB measured, 2026-10-18)"""
    out = {}
    total, _, _ = additive(cfg, costs)
    for k, it in R.ITEMS.items():
        if not built(cfg, k) or (it.parent and not built(cfg, it.parent)):
            continue
        alt = off_value(k)
        if alt == cfg[k]:
            continue
        if any(v != it.default and item_delta(costs, k, v) is None for v in (cfg[k], alt)):
            continue
        there, _, _ = additive(dict(cfg, **{k: alt}), costs)
        out[k] = {r: total[r] - there[r] for r in REGIONS}
        out[k]["sram"] = used(total, "sram") - used(there, "sram")   # (against the SRAM budget's overflow)
    return out


# ---- fitting: drop items in a fixed order of least loss until the estimate fits
FIT_ORDER = [  # (key, value): least loss first (docs/MEMORY-MAP.md section 3); samples last, PERC never
    ("ENG_SLICE", 0), ("DLY_LEN", 32768), ("PUNCH_N", 16384),
    ("SL_LEN", 2048), ("ENG_LOFI", 0), ("ENG_FORMANT", 0), ("ENG_PHASE", 0), ("ENG_DRAWBAR", 0),
    ("SET_SCRCH", 0), ("SET_STRGS", 0), ("SET_HORNS", 0),
]
FIT_MARGIN = {"flash": 1024, "sram": 704}   # the estimate's error (~0.5 %; SRAM: RAM 448, the user 2026-10-23, + RAM code 256)


def _measured(b):
    """the budget is a real build of this very configuration, not an estimate: exact (configure.py: its last build
    from this source) or anchored at distance 0 (its own check build in costs.json, measured with the deltas)"""
    return bool(b.get("exact")) or b.get("distance") == 0


def over_any(cfg, costs, margin=FIT_MARGIN, budget_fn=budget):
    """-> {region: bytes over, the margin included}; budget_fn: configure.budget on the runner (real builds' sizes).
    The margin covers the estimate's error, so a measured budget (_measured) has none: its figures are the link's
    (2026-10-24: synth, its own build 264 B under the RAM limit, was 184 B "over" and fit() found nothing to drop);
    one item away the whole margin applies again"""
    b = budget_fn(cfg, costs)
    m = {r: 0 for r in BUDGETS} if _measured(b) else margin
    return {r: o + m[r] for r, (_, _, o) in fits(b["total"], cfg).items() if o + m[r] > 0}


def _fit_run(cfg, costs, keep, order, budget_fn):
    """-> (cfg after walking the order, [changes], fits): the fixed order of least loss as far as it gets"""
    cfg, changes = dict(cfg), []
    for k, v in order:
        over = over_any(cfg, costs, budget_fn=budget_fn)
        if not over:
            break
        if k in keep or cfg[k] == v or (R.ITEMS[k].parent and not built(cfg, R.ITEMS[k].parent)):
            continue
        trial = dict(cfg)
        trial[k] = v
        b0, b1 = budget_fn(cfg, costs)["total"], budget_fn(trial, costs)["total"]
        if not any(used(b1, r) < used(b0, r) for r in over):   # (it would not help a budget that overflows)
            continue
        cfg = trial
        changes.append(f"{k}={v}")
    return cfg, changes, not over_any(cfg, costs, budget_fn=budget_fn)


def fit(cfg, costs, keep=(), order=FIT_ORDER, budget_fn=budget):
    """-> (cfg that fits by the estimate, [changes]); keep: keys never touched. None if nothing more to drop"""
    out, changes, ok = _fit_run(cfg, costs, keep, order, budget_fn)
    return (out, changes) if ok else (None, changes)


# ---- proposals: several ways to fit, each with what it changes, what is lost and the result (the web builder's panel;
# the terminal builder can offer the same). Deterministic; every one is made by the same rules as fit()
PROPOSAL_MAX = 4


def _off_value(cfg, key):
    """off_value, or None when the item is there already: nothing to drop"""
    it = R.ITEMS[key]
    alt = off_value(key)
    return None if alt == cfg[key] else alt


def _new_errors(before, after):
    old = validate(before)[0]
    return [e for e in validate(after)[0] if e not in old]


def _drop(cfg, key, value):
    """-> cfg with the item at `value` and what its dependants need (a validate() error the drop causes names items
    that were on: they go too, at most 3, never a synth engine), or None when that is not possible"""
    trial = dict(cfg, **{key: value})
    for _ in range(3):
        errs = _new_errors(cfg, trial)
        if not errs:
            return trial
        more = [k for e in errs for k in e.keys if k != key and trial.get(k) and R.ITEMS[k].group != "Synth engines"
                and not R.ITEMS[k].no_image]
        if not more:
            return None
        for k in sorted(set(more)):
            trial[k] = 0
    return None if _new_errors(cfg, trial) else trial


def _changes(before, after):
    """-> [{key, label, from, to, desc}] in registry order: the items that differ (children of a dropped parent
    are not listed: they follow it)"""
    out = []
    for k, it in R.ITEMS.items():
        if before[k] != after[k]:
            if it.parent and not built(after, it.parent) and before[it.parent] != after[it.parent]:
                continue
            out.append({"key": k, "label": it.label, "from": before[k], "to": after[k],
                        "desc": it.off_warning or it.desc})
    return out


def _shown_over(cfg, costs, budget_fn):
    """a region is over its limit as the gauges show it (the estimate, without the fit margin)"""
    return any(o > 0 for _, _, o in fits(budget_fn(cfg, costs)["total"], cfg).values())


def _outcome(cfg, costs, budget_fn):
    b = budget_fn(cfg, costs)["total"]
    return {"after": {r: {"used": u, "capacity": c, "over": o} for r, (u, c, o) in fits(b, cfg).items()},
            "fits": not _shown_over(cfg, costs, budget_fn)}


def _greedy(cfg, costs, keep, budget_fn):
    """drop built items of the others, the ones that save most in the overflowing regions first, until it fits
    (the savings are those of the configuration it starts from: an estimate, the fit is checked each time)"""
    cfg = dict(cfg)
    sav = {k: v for k, v in savings_of(cfg, costs).items() if built(cfg, k)}
    for _ in range(40):
        over = over_any(cfg, costs, budget_fn=budget_fn)
        if not over:
            break
        best = None
        for k in sorted(sav):
            it = R.ITEMS[k]
            if k in keep or it.no_image or not built(cfg, k):
                continue
            gain = sum(min(sav[k][r], over[r]) for r in over)
            if gain > 0 and (best is None or gain > best[0]):
                best = (gain, k)
        if best is None:
            break
        t = _drop(cfg, best[1], _off_value(cfg, best[1]))
        if t is None:
            keep = set(keep) | {best[1]}
            continue
        cfg = t
    return cfg


def proposals(cfg, costs, budget_fn=budget, limit=PROPOSAL_MAX):
    """-> [{id, title, text, values, changes, after, fits}]: up to `limit` ways to make the configuration fit by the
    estimate, best first; [] when it already fits. changes: what is switched off or lowered ({key, label, from, to,
    desc}, desc: what the user loses); after: {region: {used, capacity, over}}; fits: no gauge is over its limit.
    When nothing fits, the ones that come closest are still returned (fits False).
    The ways: "least" (fit(): the fixed order of least loss), "single" (the one item whose removal alone fits, the
    cheapest two), "engines" / "fx" (keep every
    synth engine / every FX, drop from the others)"""
    _ANCHOR_MEMO[id(costs)] = None
    try:
        return _proposals(cfg, costs, _memo_budget(budget_fn), limit)
    finally:
        _ANCHOR_MEMO.pop(id(costs), None)


def _memo_budget(budget_fn):
    seen = {}

    def memo(cfg, costs, **kw):
        key = tuple(sorted(cfg.items()))
        if key not in seen:
            seen[key] = budget_fn(cfg, costs, **kw)
        return seen[key]
    return memo


def _proposals(cfg, costs, budget_fn, limit):
    if not _shown_over(cfg, costs, budget_fn):
        return []
    over0 = over_any(cfg, costs, budget_fn=budget_fn)
    made = []

    def add(pid, title, text, new):
        if new is None or new == cfg or any(new == m["values"] for m in made):
            return
        made.append({"id": pid, "title": title, "text": text, "values": new, "changes": _changes(cfg, new),
                     **_outcome(new, costs, budget_fn)})

    walked, _, _ = _fit_run(cfg, costs, (), FIT_ORDER, budget_fn)
    add("least", "Least loss (the builder's order)", "The fixed order of least loss: what costs the least to give up goes first.", walked)
    sav = savings_of(cfg, costs)
    singles = []
    for k in sorted(sav):                                   # (only items that save about what is over: then check it)
        it = R.ITEMS[k]
        if it.no_image or any(sav[k][r] < over0[r] * 0.5 for r in over0):
            continue
        t = _drop(cfg, k, _off_value(cfg, k))
        if t is not None and not over_any(t, costs, budget_fn=budget_fn):
            singles.append((sum(sav[k][r] for r in REGIONS), k, t))
    for i, (_, k, t) in enumerate(sorted(singles)[:2]):
        add(f"single-{k}", f"Drop only {R.ITEMS[k].label}", "One item alone makes it fit; nothing else changes.", t)
    engines = {k for k, it in R.ITEMS.items() if it.group == "Synth engines"}
    fx = {k for k, it in R.ITEMS.items() if it.group.startswith("FX")}
    for pid, title, keep in (("engines", "Keep my synth engines, drop the extras", engines), ("fx", "Keep my effects, drop the rest", fx)):
        add(pid, title, "Everything in that group stays; the others give way, the biggest savings first.",
            _greedy(_fit_run(cfg, costs, keep, FIT_ORDER, budget_fn)[0], costs, keep, budget_fn))
    fitting = [m for m in made if m["fits"]]
    if fitting:
        made = fitting + [m for m in made if not m["fits"]]
    else:
        made.sort(key=lambda m: sum(max(0, d["over"]) for d in m["after"].values()))
    return made[:limit]


# ---- the canonical form and the cache key (cloud builds)
def canonical(cfg):
    """-> the sorted KEY=value lines of what the image is built with (effective: the defaults filled in, an option
    of an item left out at its default) plus the retired items at the value every image
    is still built as (R.HASH_AS, as cfg_hash counts them): two configurations that build the same image read alike"""
    full = dict(R.HASH_AS, **effective(cfg))
    return "".join(f"{k}={full[k]}\n" for k in sorted(full))


def cloud_key(cfg, commit):
    """-> sha256 hex of KEY_TAG, the full commit SHA and the canonical form: the name of a cached build"""
    if not isinstance(commit, str) or not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", commit):
        raise ConfigError(f"commit: a full lower-case hex SHA, got {commit!r}")
    return hashlib.sha256(f"{KEY_TAG}\n{commit}\n{canonical(cfg)}".encode()).hexdigest()


# ---- the JSON API (dict in, dict out): the menu, the CLI, the workflow and a web page alike
def describe():
    """-> the registry and the constants a front end needs, JSON-serialisable"""
    return {"key_tag": KEY_TAG, "registry": R.to_json(), "regions": list(REGIONS), "budgets": list(BUDGETS),
            "limits": limits_in_use() if (LIMITS_HOOK and LIMITS_HOOK()) or SETUP_LIMITS else None,   # (none before setup)
            "spare": dict(SPARE), "needs_auto": list(NEEDS_AUTO), "fit_order": [list(x) for x in FIT_ORDER]}


def _request_text(req):
    """a request's configuration as .config text: "text" as it is, or "config" ({KEY: int}) as KEY=value lines"""
    if "text" in req:
        if not isinstance(req["text"], str):
            raise ConfigError("text: a string")
        return req["text"]
    items = req.get("config", {})
    if not isinstance(items, dict):
        raise ConfigError("config: an object of KEY: integer")
    bad = [k for k, v in items.items() if not isinstance(k, str) or isinstance(v, bool) or not isinstance(v, int)]
    if bad:
        raise ConfigError(f"config: integer values only ({', '.join(map(str, bad[:5]))})")
    return "".join(f"{k}={v}\n" for k, v in items.items())


def _issues(lst):
    return [{"text": str(i), "keys": list(getattr(i, "keys", ()))} for i in lst]


def evaluate(req):
    """{"text": .config text | "config": {KEY: int}, "strict": reject unknown keys, "commit": full SHA (for the key),
    "costs": costs.json as a dict (for the budget), "exact": a real build's sizes} -> {"ok", "error", "name",
    "parse_warnings", "errors", "warnings", "notices", "config", "normalized", "canonical", "cfg_hash", "key",
    "budget", "fits", "fit"}; ok: it parses, validates without errors and (with costs) fits by the estimate"""
    if not isinstance(req, dict):
        return {"ok": False, "error": "the request: an object"}
    said = []
    try:
        cfg, name = parse(_request_text(req), warn=said.append, reject_unknown=bool(req.get("strict")))
        key = cloud_key(cfg, req["commit"]) if req.get("commit") is not None else None
    except ConfigError as e:
        return {"ok": False, "error": str(e), "parse_warnings": said}
    err, warn, note = validate(cfg)
    b = budget(cfg, req.get("costs"), exact=req.get("exact"))
    fit_rows = ({r: {"used": u, "capacity": c, "over": o} for r, (u, c, o) in fits(b["total"], cfg).items()}
                if b else None)
    fits_ok = all(x["over"] <= 0 for x in fit_rows.values()) if fit_rows else None
    return {"ok": not err and fits_ok is not False, "error": None, "name": name, "parse_warnings": said,
            "errors": _issues(err), "warnings": _issues(warn), "notices": _issues(note), "config": cfg,
            "normalized": effective(cfg), "canonical": canonical(cfg), "cfg_hash": f"{cfg_hash(cfg):08x}", "key": key,
            "budget": b, "fits": fit_rows, "fit": fits_ok}
