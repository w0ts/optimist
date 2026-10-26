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
import re
import sys

import registry as R

KEY_TAG = "optimist-cloud/1"
NEEDS_AUTO = ()                 # (setup: the items the firmware switches off without the automation store)


def setup(needs_auto=None):
    """the data the core cannot read itself: needs_auto, the keys of configure._needs_auto (firmware headers)"""
    global NEEDS_AUTO
    if needs_auto is not None:
        NEEDS_AUTO = tuple(needs_auto)


def _stderr(msg):
    print(f"warning: {msg}", file=sys.stderr)


# the budgets: the app slot and the packed SRAM (app.ld: RAM code, .data/.bss and the pool in one region since
# 2026-10-25, docs/MEMORY-MAP.md 2.3-2.4); build.py keeps 8 KiB of SRAM spare below the stacks
LIMITS = {"flash": 581564, "sram": 474880}
SPARE = {"flash": 0, "sram": 8192}
FLASH_BY_LAYOUT = {1: 765696}    # the code's room in the other flash layouts once the far probe passed (LAYOUT + FAR_OK):
                                 # tools/flash_layout.py code_limit, which the web builder does not ship; a probe build
                                 # (LAYOUT without FAR_OK) keeps the normal slot; tests/flash_layout_test.py checks they agree
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
        cfg[k] = v
        named.add(k)
    for k, it in R.ITEMS.items():                      # (a fixed item: what every build has, whatever the text says)
        if it.fixed:
            cfg[k] = it.default
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
            if it.group == g and (full or cfg[k] != it.default):
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
    if cfg.get("LAYOUT"):                        # the fake SD mode: the user samples' and snapshots' flash is the app's
        clash = [k for k in ("ENG_SAMPLE", "DRUM_USR", "SNAPSHOTS") if k in R.ITEMS and built(cfg, k)
                 and not (k == "ENG_SAMPLE" and cfg.get("SD_RING"))]   # (SAMPLE's CARD set: from the card)
        if built(cfg, "NATIVE_BANKS") and built(cfg, "ENG_CZ"):
            clash.append("NATIVE_BANKS")
        if clash or not cfg.get("PATTERNS"):
            err.append(Issue("the fake SD mode layout keeps no user samples, snapshots or CZ collection and needs "
                             "PATTERNS: " + ", ".join(clash + ([] if cfg.get("PATTERNS") else ["PATTERNS off"])),
                             ["LAYOUT"] + clash + ([] if cfg.get("PATTERNS") else ["PATTERNS"])))
    if cfg.get("BT_PROJ") and not (cfg.get("BLE") and cfg.get("ED_MIRROR") and cfg.get("PATTERNS") and cfg.get("BACKUP")):
        err.append(Issue("projects over Bluetooth need Bluetooth MIDI (BLE), the Mirror (ED_MIRROR, its Bluetooth link), the "
                         "projects (PATTERNS) and the backup (BACKUP)", ["BT_PROJ", "BLE", "ED_MIRROR", "PATTERNS", "BACKUP"]))
    if cfg.get("SD_RING") and not (cfg.get("SDCARD") and cfg.get("LAYOUT") == 1 and cfg.get("FAR_OK") and
                                   cfg.get("ARENA") and built(cfg, "ENG_SAMPLE")):
        err.append(Issue("samples from the card (SD_RING) need the card under full FatFs (SDCARD, LAYOUT 1, FAR_OK), "
                         "SAMPLE (its CARD set) and the arena (ARENA: the heads and the rings)",
                         ["SD_RING", "SDCARD", "LAYOUT", "FAR_OK", "ENG_SAMPLE", "ARENA"]))
    if cfg.get("FAR_OK") and not cfg.get("LAYOUT"):    # the big slot exists only in the fake SD mode layout
        err.append(Issue("the far probe's answer (FAR_OK) opens the fake SD mode's whole slot: it needs that layout "
                         "(LAYOUT 1)", ["FAR_OK", "LAYOUT"]))
    if cfg.get("OVERLAY") and not cfg.get("LAYOUT"):   # each slot engine's code linked once per slot: the big flash
        err.append(Issue("the RAMTEXT overlays put each engine's fast code in flash once per part: they need the fake "
                         "SD mode layout (LAYOUT 1)", ["OVERLAY", "LAYOUT"]))
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
    sets = [k for k in R.ITEMS if k.startswith("SET_")]
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
    if mem["types"] and cfg.get("DLY_ARENA", 0) and cfg["DLY_ARENA"] < mem["send"]:
        err.append(Issue(f"the effects memory holds the delay's line: at least {mem['send']} KB (or auto, or a shorter "
                         "delay)", ["DLY_ARENA", "DLY_LEN"]))
    else:
        for k, t in mem["types"].items():
            if not t["own"]:                            # (borrowing a send's line only: silent while the sends are on
                                                        # the master, as by default; the FM-1, 2026-10-25: XDLY did
                                                        # nothing, "it should be addressed at the builder stage")
                err.append(Issue(f"{FXM_NAMES[k]} cannot play while the delay is on the master: its line is "
                                 f"{t['kb']} KB and the effects memory has {mem['own']} KB beside the delay's "
                                 f"{mem['send']} KB: DLY_ARENA at least {mem['send'] + t['kb']} KB (or auto), a shorter "
                                 "DLY_LEN" + (" or XDLY_LEN" if k == "INS_XDLY" else ""),
                                 ["DLY_ARENA", k] + (["XDLY_LEN"] if k == "INS_XDLY" else []) + ["DLY_LEN"]))
        if mem["types"]:
            note.append(Issue(f"{R.ITEMS['DLY_ARENA'].label}: {fx_memory_text(mem)}", ["DLY_ARENA"]))
    if cfg.get("ARENA", 0):
        blocks, room = arena_blocks(cfg), cfg["ARENA"] * 1024
        big = [n for n, b in blocks.items() if b > room]
        if big:
            err.append(Issue(f"the arena ({cfg['ARENA']} KB) cannot hold {', '.join(big)} "
                             f"({max(blocks.values()) // 1024} KB): a larger ARENA, or off", ["ARENA"]))
        else:
            fixed = room - arena_mem(cfg)["pool"]
            what = ("the parts' engine states, the effects' lines, the SLICER's recordings, WAVE's table, the X0X "
                    "machines" if sd_mode(cfg) else "the parts' engine states")
            note.append(Issue(f"{R.ITEMS['ARENA'].label}: {cfg['ARENA']} KB in place of {fixed // 1024} KB of fixed "
                              f"memories ({what}); a project using more is refused and offered what to drop", ["ARENA"]))
    for k, it in R.ITEMS.items():
        if it.off_warning and not cfg[k] and (not it.parent or built(cfg, it.parent)):
            warn.append(Issue(f"{it.label} off: {it.off_warning}", [k]))
        if built(cfg, k):
            if it.experimental:
                warn.append(Issue(f"{it.label}: EXPERIMENTAL ({it.tested})", [k]))
            if it.notice:
                note.append(Issue(f"{it.label}: {it.notice}", [k]))
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
        out[it.flag] = v
    out["FELUCCA_USB_AUDIO"] = cfg["USB_AUDIO"]
    out["FELUCCA_CDC"] = cfg["USB_CONSOLE"]
    out["FELUCCA_SLICE"] = cfg["ENG_SLICE"]
    if not built(cfg, "DRUM_SAMPLED") or not any(cfg[k] for k in R.ITEMS if k.startswith("KIT_")):
        out["FELUCCA_DRUM_SAMPLED"] = 0
    skip = [R.ITEMS[k].env for k in R.ITEMS if k.startswith("SET_") and not cfg[k]]
    if not out.get("FELUCCA_DRUM_SAMPLED", 1):
        skip.append("PERC")
    env = {"FELUCCA_SAMPLES_SKIP": ",".join(skip), "FELUCCA_SLICE": str(cfg["ENG_SLICE"])}
    return out, env


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
    target = {R.ITEMS[k].flag for k in R.ITEMS if R.ITEMS[k].target_only}
    for k in sorted(f):
        suffix = "u" if k in ("FELUCCA_DLY_LEN", "FELUCCA_PUNCH_N", "FELUCCA_SL_LEN", "LCD_BAUD") else ""
        body = [f"#ifndef {k}", f"#define {k} {f[k]}{suffix}", "#endif"]
        L += (["#ifdef __PI32V2__"] + body + ["#endif"]) if k in target else body
    return "\n".join(L) + "\n"


def item_delta(costs, key, value):
    """bytes this item at this value adds to the default build, per region (None: not measured). An item switched
    off or down (a value under its default) never adds: a region it measured as growing counts 0 (the user, 2026-10-19,
    "clamp at 0": FX_DIST, MIDI_NOTEOUT, MISSING_WARN off made the default build 300-700 B larger in flash, the RAM code
    placed again, and the figure grew as a feature went). Not the other way: an item on that saves a region is often
    real (TAB_IN_POOL's RAM to the pool, X909_CYM=2's smaller cymbals)"""
    d = costs.get("deltas", {}).get(key, {}).get(str(value))
    if d is None or not value < R.ITEMS[key].default:
        return d
    return {r: min(0, b) for r, b in d.items()}


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


REV_ROOM_LEN = (1559 + 1931 + 2389 + 2791 + 12 + 2, 779 + 967 + 1193 + 1399 + 6 + 2)   # fx.c REV_ROOM_LEN (full, half rate)


def rev_lines(cfg):
    """-> {region: bytes} of the reverb's shared line buffer (fx.c and reverb_alt.c rev_line): the size of the
    largest algorithm built (ROOM 8684 samples, 4346 at half rate; PLATE / FDN8 / VTINY RV_N: 8192, 4096 at half
    rate, FDN8 twice that in the pool; SPRING 4096), in the pool with REV_POOL, else in main RAM. A maximum over
    several items and a region that REV_POOL picks: no per-item delta adds up to it, so the estimate computes it
    (model_terms) and the measured deltas and pairs leave it out (measure_costs.py)"""
    f, _ = flags(cfg)

    def on(key):                                        # (flags: an option of an item left out has its default)
        return int(f.get(R.ITEMS[key].flag, 0) or 0)
    half, pool = on("REV_HALF"), on("REV_POOL")
    own = max(REV_ROOM_LEN[half] if on("REV_ROOM") else 0, 4096 * on("SPRING"))
    n = own
    if on("REV_PLATE") or on("REV_FDN8") or on("REV_AIRWIN"):
        n = max((4096 if half else 8192) << (1 if on("REV_FDN8") and pool else 0), own)
    if not on("FX_REVERB") or not n:
        n = 1                                           # (the C array's 1 when the reverb is not built)
    return {"pool" if pool else "ram": 2 * n}


AUTO_ITEMS = ("MOTION", "CHANCE", "SL24_XSTEP", "MICRO", "FILLS", "PLOCK", "COND")   # (backports24seq.h FELUCCA_AUTO)
AUTO_MAX, AUTO_NTRK = 128, 4                                                 # (seq/auto.h)
AUTO_LIST = 1 + 3 * AUTO_MAX                                                 # auto_list_t: n, AUTO_MAX x 3 bytes
AUTO_STORE = 4 + 4 + AUTO_NTRK * AUTO_LIST                                   # auto_store_t (seq/auto.c): psum, on + rsv, 4 lists
AUTO_AUX = 4                                                                 # storage/auto_proj.c AUTO_AUX
AUTO_ENC_MAX = 3 + AUTO_NTRK * (1 + 3 * AUTO_MAX)                            # auto.h AUTO_ENC_MAX (> the stepx form's)
SL24_XS = 4 * 176          # storage/project.c sl24_xs: NTRK stepx_t (seq/stepx.h, 176 B) in the pool, with SL24_IMPORT or SL24_EXPORT


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
    SECTIONS 4) and the extras record's read buffer (sx_rbuf) in the pool; with SL24_IMPORT or SL24_EXPORT, 2.4's step
    extras on their way in or out (project.c sl24_xs, pool: found 2026-10-12, 704 B under the estimate). Shared by those
    items: the estimate counts it once (model_terms) and the measured deltas and pairs leave it out (measure_costs.py)"""
    f, _ = flags(cfg)
    if not any(f.get(R.ITEMS[k].flag, 0) for k in AUTO_ITEMS):
        return {"ram": 0, "pool": 0}
    slots = 0 if f.get("FELUCCA_SECTIONS", 16) > 4 else 4           # (SEC_LOGGED: the sections live in the log)
    rbuf = (4 + AUTO_ENC_MAX + 3) // 4 * 4 if not slots else (AUTO_ENC_MAX + 3) // 4 * 4   # (stepx_log.c / snapshots.c)
    export = SL24_XS if f.get(R.ITEMS["SL24_EXPORT"].flag, 0) or f.get(R.ITEMS["SL24_IMPORT"].flag, 0) else 0
    return {"ram": AUTO_STORE + AUTO_LIST, "pool": (AUTO_AUX + slots) * AUTO_STORE + rbuf + export}


# engines.c part_mem: NPART engine states in the pool, a union of the engines built that keep one there; bytes of
# the array with each member alone (sizes.json "symbols", measure_costs.py holds these against every build it makes)
# (2026-10-26: JUNO, DRAWBAR, CZ and SLICE's states moved in from their static arrays: their measured item costs in
# costs.json still count those arrays as RAM until a re-measure)
PART_MEM = {"ENG_FM6": 11712, "ENG_PHYS": 38664, "ENG_GRAIN": 21072, "ENG_ACID": 1440, "ENG_PLAITS": 1920,
            "ENG_JUNO": 1344, "ENG_DRAWBAR": 2496, "ENG_CZ": 3120, "ENG_SLICE": 3072}   # (2026-10-25/26: arn_mem)
PART_MEM_NONE = 0                                   # (none built: not in the image)


def part_mem(cfg):
    """-> {region: bytes} of the synth parts' engine states (engines.c part_mem, the pool): the largest member
    built. A maximum: no per-item delta adds up to it (GRAIN off frees 9.3 KB, but not with PHYS on), so the estimate
    computes it (model_terms) and the measured deltas and pairs leave it out (measure_costs.py)"""
    sizes = [n for k, n in PART_MEM.items() if built(cfg, k)]
    return {"pool": max(sizes) if sizes else PART_MEM_NONE}


NTRK = 4
# the effects memory (fx/insfx/fxi_mem.h, fxi.c, 2026-10-23): one pool memory in 1 KB units for the delay send's line and
# every line of the limited inserts; the line of each (KB, by item; XDLY's: two lines of XDLY_LEN samples) and its
# instance's state on the target (bytes: measure_costs.py holds the tables against the firmware's symbols)
FXM_UNIT = 1024        # fxi_mem.h FXA_U: 512 samples, bytes
FXM_OWNERS = 9         # fxi_mem.h FXI_NK: T1 T2 T3 DR, the master, the USB input, the returns A B C
FXM_TYPES = ("INS_CHORUS", "INS_DELAY", "INS_JCHORUS", "INS_FLANGER", "INS_XDLY")   # (fxi_kern.c's order, j)
FXM_NAMES = {"INS_CHORUS": "CHRS", "INS_DELAY": "ECHO", "INS_JCHORUS": "JUNO CHORUS", "INS_FLANGER": "FLNG",
             "INS_XDLY": "XDLY"}
FXM_STATE = {"INS_CHORUS": ("ifx_chrs", 24), "INS_DELAY": ("ifx_echo", 20), "INS_JCHORUS": ("ifx_jcho", 36),
             "INS_FLANGER": ("ifx_flng", 24), "INS_XDLY": ("ifx_xdly", 40)}
FXM_SYMBOLS = ("fxa_ins", "fxa_bm") + tuple(v[0] for v in FXM_STATE.values())   # (tools/build.py SIZED_SYMBOLS)
FXM_CHO_KB = 4         # fx.c CHO_LEN: the chorus send's line, 2048 samples
XD_STATE = FXM_STATE["INS_XDLY"][1]


def dly_send_kb(cfg, f=None):
    """the delay send's line in KB (fx.c dly_buf), 0 without the delay; f: the configuration's flags, when at hand"""
    f = f if f is not None else flags(cfg)[0]
    return int(f.get("FELUCCA_DLY_LEN", 0) or 0) * 2 // 1024 if f.get("FELUCCA_FX_DELAY") else 0


def fx_memory(cfg):
    """-> the effects memory (fx/insfx/fxi_mem.h) of this configuration: {"send", "cho": the send lines' KB, "own": the
    inserts' own KB (DLY_ARENA less the delay's line; auto: one line of each type built), "types": {item: {"kb": its
    line, "max": the most at once (an owner each at most, fxi_mem.h FXI_TN), "own": in the own room alone, "dly", "cho":
    what each send's line adds while it is in no master slot}}} (types: the ones built)"""
    f, _ = flags(cfg)

    def on(key):
        return int(f.get(R.ITEMS[key].flag, 0) or 0)
    send, cho = dly_send_kb(cfg, f), FXM_CHO_KB if on("FX_CHORUS") else 0
    kb = {"INS_CHORUS": 2, "INS_DELAY": 8, "INS_JCHORUS": 2, "INS_FLANGER": 1, "INS_XDLY": on("XDLY_LEN") // 256}
    types = [k for k in FXM_TYPES if on(k)]
    own = (max(0, on("DLY_ARENA") - send) if on("DLY_ARENA") else sum(kb[k] for k in types)) if types else 0
    out = {}
    for k in types:
        z = kb[k]
        out[k] = {"kb": z, "max": min(FXM_OWNERS, own // z + send // z + cho // z),
                  "own": min(FXM_OWNERS, own // z), "dly": send // z, "cho": cho // z}
    return {"send": send, "cho": cho, "own": own, "types": out}


def fx_memory_text(mem):
    """the "up to N at once" line of the effects memory (validate's notice on DLY_ARENA, the menu's info)"""
    parts = []
    for k, t in mem["types"].items():
        more = [f"+{min(t['dly'], FXM_OWNERS - t['own'])} while the delay is in no master slot" if t["dly"] and
                t["own"] < FXM_OWNERS else "",
                f"+{min(t['cho'], FXM_OWNERS - t['own'])} while the chorus is in none" if t["cho"] and
                t["own"] < FXM_OWNERS else ""]
        more = ", ".join(m for m in more if m)
        parts.append(f"{FXM_NAMES[k]} up to {t['own']}" + (f" ({more})" if more else ""))
    return (f"{mem['own']} KB of its own beside the delay's {mem['send']} KB line: at once (each type alone) "
            + "; ".join(parts))


def dly_arena(cfg):
    """-> {"units": the own room's KB (fxa_ins), "pool": bytes} of the effects memory (fx/insfx/fxi_mem.h, fxi.c): the
    own room, the instance tables (ifx_chrs .. ifx_xdly: an owner each at most, no more than the memory holds) and the
    units' bitmap (fxi.c fxa_bm). Set by the five types, XDLY_LEN, DLY_LEN, FX_DELAY, FX_CHORUS and DLY_ARENA together:
    no per-item delta adds up to it, so the estimate computes it (model_terms) and the measured deltas leave it out
    (measure_costs.py)"""
    mem = fx_memory(cfg)
    tables = {FXM_STATE[k][0]: t["max"] * FXM_STATE[k][1] for k, t in mem["types"].items()}
    bm = 4 * ((mem["send"] + mem["own"] + mem["cho"] + 31) // 32) if mem["types"] else 0
    return {"units": mem["own"], "tables": tables, "bitmap": bm,
            "pool": mem["own"] * FXM_UNIT + sum(tables.values()) + bm}


NPART = 3                  # core.h: the synth parts (part_mem's NPART states)


REV_AP = (556 + 441, 278 + 221)   # fx.c REV_A0 + REV_A1, the ROOM's diffusers rev_ap (full, half rate), samples
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


def sd_mode(cfg):
    """the SD card mode (registry.h FELUCCA_SD_MODE: the fake SD mode's layout, LAYOUT 1, until its own flag): the
    builds that get the arena's extras (the effects' lines, the other buffers, the load's list page)"""
    return cfg.get("LAYOUT", 0) == 1


def arena_mem(cfg):
    """-> {region: bytes} of the arena (ARENA, KB: core/arena.c; in the SD card mode fx/fx_arena.c, core/arena_bufs.c
    too) against the fixed memories it replaces: its own pool less the parts' engine states (part_mem); in the SD card
    mode (sd_mode) less the delay and chorus sends' lines, the reverb's line (rev_lines: in main RAM without REV_POOL)
    and the ROOM's diffusers after it, the inserts' own room (fx_memory), the SLICER's recordings (a track each), WAVE's
    table and the X0X machines' states (arena_bufs); {} with ARENA off. The code and bookkeeping it adds is its measured
    delta (costs.json)"""
    f, _ = flags(cfg)
    kb = int(f.get(R.ITEMS["ARENA"].flag, 0) or 0)
    if not kb:
        return {}
    if not sd_mode(cfg):
        return {"pool": kb * 1024 - part_mem(cfg)["pool"]}
    rev, mem, bufs = rev_lines(cfg), fx_memory(cfg), arena_bufs(cfg)
    lines = (mem["send"] + mem["cho"] + mem["own"]) * 1024 + rev.get("pool", 0) + rev_ap(cfg)
    other = sum(v * (NTRK if k.startswith("a track's") else 1) for k, v in bufs.items())
    return {"pool": kb * 1024 - part_mem(cfg)["pool"] - lines - other, "ram": -rev.get("ram", 0)}


def rev_ap(cfg):
    """-> bytes of the ROOM's diffusers (fx.c rev_ap, always in the pool; in the reverb's block with ARENA), as the
    pool holds them: 4-aligned (the default profile with ROOM, the SLICER and WAVE, 2026-10-25: 1,996 B of pool for
    the 1,994 B array)"""
    f, _ = flags(cfg)
    if not built(cfg, "FX_REVERB") or not built(cfg, "REV_ROOM"):
        return 0
    return (2 * REV_AP[int(f.get(R.ITEMS["REV_HALF"].flag, 0) or 0)] + 3) & ~3


def arena_blocks(cfg):
    """-> {name: bytes} the blocks the arena places (a part's engine state, each effect's line, the other buffers;
    validate: each must fit)"""
    if not sd_mode(cfg):
        return {"a part's engine state": part_mem(cfg)["pool"] // NPART}
    rev, mem = rev_lines(cfg), fx_memory(cfg)
    out = {"a part's engine state": part_mem(cfg)["pool"] // NPART, "the delay's line": mem["send"] * 1024,
           "the chorus send's line": mem["cho"] * 1024,
           "the reverb's line": sum(rev.values()) + rev_ap(cfg) if built(cfg, "FX_REVERB") else 0,
           "the inserts' own room": mem["own"] * 1024, **arena_bufs(cfg)}
    return {k: v for k, v in out.items() if v}


def model_terms(cfg):
    """-> {region: bytes} the computed part of the estimate, against the default build: what is not a sum of
    per-item deltas (the reverb's line buffer, rev_lines; the automation store, auto_store; the engines' part
    states, part_mem; the delay arena, dly_arena; the arena for both, arena_mem)"""
    d = defaults()
    arena = ({"pool": dly_arena(cfg)["pool"]}, {"pool": dly_arena(d)["pool"]})
    terms = [(rev_lines(cfg), rev_lines(d)), (auto_store(cfg), auto_store(d)), (part_mem(cfg), part_mem(d)), arena,
             (arena_mem(cfg), arena_mem(d))]
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


def nearest_anchor(cfg, costs, exclude=(), extra=()):
    """-> (name, cfg, real sizes, distance) the anchor (anchors) the fewest items away; at the same distance the
    later one (a real build from this source over costs.json's)"""
    best = None
    for n, acfg, real in anchors(costs, exclude, extra):
        d = distance(cfg, acfg)
        if best is None or d <= best[3]:
            best = (n, acfg, real, d)
    return best


def _grown(cfg, acfg, costs):
    """the items whose change from the anchor's configuration to cfg adds flash (the margin counts these only: an
    item switched OFF never raises the estimate, 2026-10-16: a margin per changed item made switching a 40 B feature off
    show 216 B MORE flash, and a user saw the figure grow as he removed features); a growth within ALIGN_NOISE is alignment,
    not code (2026-10-19: three items measured +4..+8 B off and drew the whole 256 B margin)"""
    ic, ia = additive(cfg, costs)[1], additive(acfg, costs)[1]
    ec, ea = effective(cfg), effective(acfg)
    return sum(1 for k in ec if ec[k] != ea[k] and ic.get(k, {}).get("flash", 0) - ia.get(k, {}).get("flash", 0) > ALIGN_NOISE)


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
        total = {r: real[r] + total[r] - at[r] + ANCHOR_MARGIN[r] * grown for r in REGIONS}
    return {"total": total, "items": items, "unmeasured": missing, "exact": bool(exact),
            "anchor": ref[0] if ref else None, "distance": ref[3] if ref else None}


# ---- the undo history's ring (information: what RAM and the pool leave)
def undo_ring(total):
    """the undo history's ring a build leaves (app.ld _undo_*): the SRAM after the pool, less its 8 KiB spare
    (docs/MEMORY-MAP.md 2.1); never below 0"""
    return max(0, LIMITS["sram"] - SPARE["sram"] - used(total, "sram"))


def fits(total, cfg=None):
    """-> {budget: (used, capacity, over)} for flash and the packed SRAM: over > 0 overflows (SRAM keeps its 8 KiB
    spare); the flash by the configuration's layout (LAYOUT: tools/flash_layout.py)"""
    out = {}
    for b in BUDGETS:
        u, cap = used(total, b), LIMITS[b] - SPARE[b]
        if b == "flash" and cfg and cfg.get("LAYOUT") in FLASH_BY_LAYOUT and cfg.get("FAR_OK"):
            cap = FLASH_BY_LAYOUT[cfg["LAYOUT"]] - SPARE["flash"]
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
    X909_CYM, SNAPSHOTS, CACHE_RAM, DUAL, ...), else its smallest setting: SMALLEST, else the smallest non-zero value
    (the sizes, DLY_LEN .. SECTIONS; a 0 that is no "off" is not taken: DLY_ARENA's auto, LCD_BAUD's 60 MHz).
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
    ("LCD_DIRTY", 0),   # (after the samples: for main RAM, 656 B, the graph strip redrawn whole; the defaults were 52 B over 2026-10-13)
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
            "limits": dict(LIMITS),
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
