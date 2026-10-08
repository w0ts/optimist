# SPDX-License-Identifier: GPL-3.0-only
"""Firmware builder: .config files, profiles, validation, the budget, build/gen/felucca_config.h.

The plain (non-TUI) side of tools/menuconfig, usable from scripts and tests:

  python3 tools/builder/configure.py --list
  python3 tools/builder/configure.py --profile drum-machine --budget
  python3 tools/builder/configure.py --profile fm-va-studio --set FX_PUNCH=0 --write my.config
  python3 tools/builder/configure.py --config my.config --build      # real build, exact sizes, the package

A .config is "KEY=value" lines (registry.py keys); missing keys take the registry default, so a profile lists
only what differs. "# name: ..." names the configuration (the BUILD SysEx reports it, max 16 characters).
"""
import argparse
import importlib.util
import json
import os
import platform
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import registry as R                                    # noqa: E402  (the registry sits next to this file)
try:
    import backports                                    # noqa: F401,E402  (items of merged backport branches)
except ImportError:
    pass

ROOT = HERE.parent.parent
PROFILES = ROOT / "config" / "profiles"
MY_PROFILES = ROOT / "config" / "my-profiles"           # the user's own (git-ignored)
COSTS = HERE / "costs.json"
LIMITS = {"flash": 581564, "ram": 98304, "pool": 344064, "ramtext": 32512}
SPARE = {"flash": 0, "ram": 0, "pool": 8192, "ramtext": 0}   # build.py keeps 8 KiB of the pool spare
REGIONS = ("flash", "ram", "pool", "ramtext")
GRAIN_SETS = ("SET_PIANO", "SET_VIBES", "SET_FLUTE")       # the sets of GRAIN's presets (eng_grain.c)


class ConfigError(Exception):
    pass


# ---- reading and writing
def defaults():
    return {k: it.default for k, it in R.ITEMS.items()}


def parse(text, base=None, strict=True):
    """.config text -> (values, name); values start from base (default: the registry defaults)"""
    cfg = dict(base if base is not None else defaults())
    name = ""
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
        if k in R.MIGRATE:                              # (an item that became others: registry.py MIGRATE)
            cfg.update(R.MIGRATE[k](v))
            continue
        if k not in R.ITEMS:
            if strict:
                raise ConfigError(f"line {n}: unknown item {k}")
            continue
        cfg[k] = v
    return cfg, name


def load(path, base=None):
    return parse(Path(path).read_text(), base)


def profile_names():
    """the profiles shipped with the firmware (config/profiles)"""
    return sorted(p.stem for p in PROFILES.glob("*.config"))


def my_profile_names():
    """the user's own profiles (config/my-profiles, git-ignored)"""
    return sorted(p.stem for p in MY_PROFILES.glob("*.config"))


def profile_path(name):
    """a shipped profile first, then the user's own of that name"""
    for d in (PROFILES, MY_PROFILES):
        p = d / f"{name}.config"
        if p.exists():
            return p
    return None


def load_profile(name):
    p = profile_path(name)
    if p is None:
        raise ConfigError(f"no profile {name!r} (profiles: {', '.join(profile_names() + my_profile_names())})")
    cfg, nm = load(p)
    return cfg, nm or name


def save_my_profile(cfg, name):
    """-> the path written; a name must be a plain file name and not shadow a shipped profile"""
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9 _.-]{0,39}", name or "") or name.endswith("."):
        raise ConfigError("a profile name: letters, digits, space, _ . - (up to 40)")
    if (PROFILES / f"{name}.config").exists():
        raise ConfigError(f"{name!r} is a shipped profile: pick another name")
    MY_PROFILES.mkdir(parents=True, exist_ok=True)
    p = MY_PROFILES / f"{name}.config"
    p.write_text(dump(cfg, name))
    return p


PUBLISH_RE = re.compile(r"#\s*publish:\s*(\S+)\s*$")


def is_published(path):
    """a shipped profile's "# publish: yes" line: CI builds it (.github/workflows/build.yml, config --published)"""
    for ln in Path(path).read_text().splitlines():
        m = PUBLISH_RE.fullmatch(ln.strip())
        if m:
            return m.group(1).lower() == "yes"
    return False


def published_profiles():
    """the shipped profiles marked "# publish: yes" (my profiles are git-ignored: CI never sees them)"""
    return [n for n in profile_names() if is_published(PROFILES / f"{n}.config")]


def set_published(name, on):
    """-> the path; the flag goes right after "# name:" (or first), the rest of the file is kept as written"""
    p = PROFILES / f"{name}.config"
    if not p.exists():
        where = " (mine: share it first, --share)" if (MY_PROFILES / f"{name}.config").exists() else ""
        raise ConfigError(f"no shipped profile {name!r}{where} (shipped: {', '.join(profile_names())})")
    flag = f"# publish: {'yes' if on else 'no'}"
    lines = p.read_text().splitlines()
    at = next((i for i, ln in enumerate(lines) if PUBLISH_RE.fullmatch(ln.strip())), None)
    if at is not None:
        lines = lines[:at] + [flag] + lines[at + 1:]
    else:
        after = next((i + 1 for i, ln in enumerate(lines) if re.match(r"#\s*name:", ln.strip())), 0)
        lines = lines[:after] + [flag] + lines[after:]
    p.write_text("\n".join(lines) + "\n")
    return p


def share_profile(name):
    """one of my profiles (config/my-profiles, git-ignored) -> config/profiles (shipped: commit it); -> the path"""
    src, dest = MY_PROFILES / f"{name}.config", PROFILES / f"{name}.config"
    if not src.exists():
        raise ConfigError(f"no profile of mine {name!r} (mine: {', '.join(my_profile_names()) or 'none'})")
    if dest.exists():
        raise ConfigError(f"{name!r} is already a shipped profile: rename yours first")
    PROFILES.mkdir(parents=True, exist_ok=True)
    os.replace(src, dest)
    return dest


DEFAULT_PROFILE = "user-default"        # make's and tools/optimist.py's default: never deleted


def delete_profile(name):
    """-> the path removed: one of mine first, else a shipped one (git has it back until the deletion is committed)"""
    if name == DEFAULT_PROFILE:
        raise ConfigError(f"{name!r} is the default profile (make, tools/optimist.py): change it, do not delete it")
    for d in (MY_PROFILES, PROFILES):
        p = d / f"{name}.config"
        if p.exists():
            p.unlink()
            return p
    raise ConfigError(f"no profile {name!r} (profiles: {', '.join(profile_names() + my_profile_names())})")


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


def apply_env(cfg, env):
    """the old build switches (FELUCCA_X=0/1 in the environment) still work: they override the .config"""
    cfg = dict(cfg)
    for k, it in R.ITEMS.items():
        if it.flag and it.flag in env and re.fullmatch(r"-?\d+u?", env[it.flag]):
            cfg[k] = int(env[it.flag].rstrip("u"))
    if env.get("FELUCCA_LCD_BAUD", "") in ("0", "1", "2", "3", "4"):
        cfg["LCD_BAUD"] = int(env["FELUCCA_LCD_BAUD"])
    if env.get("FELUCCA_SIZE") in ("0", "1"):
        cfg["SIZE"] = int(env["FELUCCA_SIZE"])
    if env.get("FELUCCA_SLICE") in ("0", "1"):
        cfg["ENG_SLICE"] = int(env["FELUCCA_SLICE"])
    if "FELUCCA_USB_AUDIO" in env or "FELUCCA_CDC" in env:
        ua = env.get("FELUCCA_USB_AUDIO") == "1"
        cdc = env.get("FELUCCA_CDC", "0" if ua else "1") == "1"
        cfg["USB_MODE"] = 2 if ua else 1 if cdc else 0
    for s in (x.strip().upper() for x in env.get("FELUCCA_SAMPLES_SKIP", "").split(",") if x.strip()):
        if f"SET_{s}" in cfg:
            cfg[f"SET_{s}"] = 0
    return cfg


# ---- meaning
def built(cfg, key):
    """the item is in the build: on (a choice: non-zero) and its parent on"""
    it = R.ITEMS[key]
    if it.parent and not built(cfg, it.parent):
        return False
    return cfg[key] != 0


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
    fm6_modes = ("FM6_MARK1", "FM6_MODERN", "FM6_OPL")
    if built(cfg, "ENG_FM6"):
        if not any(cfg[k] for k in fm6_modes):
            err.append(Issue("FM6 needs at least one ENGINE mode: switch on MARK I, MODERN or OPL (or FM6 off)",
                             ("ENG_FM6",) + fm6_modes))
        if cfg.get("FM6_MKI_FLASH") and not cfg["FM6_MARK1"]:
            warn.append(Issue("MARK I tables in flash without ENGINE mode MARK I: nothing to move (no effect)",
                              ("FM6_MKI_FLASH", "FM6_MARK1")))
        if not cfg["FM6_KEYS"] and not cfg["FM6_SYSEX"]:
            warn.append(Issue("FM6 without the operator editor and without DX7 SysEx: preset-only (no voice editing)",
                              ("FM6_KEYS", "FM6_SYSEX")))
    sx_users = [k for k in ("MICRO", "FILLS", "PLOCK") if k in R.ITEMS and cfg.get(k)]   # (SLOOP 2.4's sequencer)
    if sx_users and "SL24_XSTEP" in R.ITEMS and not cfg.get("SL24_XSTEP"):
        err.append(Issue("micro timing, fills and parameter locks need their storage: switch on SL24_XSTEP",
                         ["SL24_XSTEP"] + sx_users))
    revs = [k for k in R.REV_ALGOS if k in R.ITEMS]
    if built(cfg, "FX_REVERB") and not any(cfg[k] for k in revs):
        err.append(Issue("the reverb bus needs an algorithm: tick ROOM, PLATE, FDN8, SPRING or VTINY "
                         "(or the reverb bus off)",
                         ["FX_REVERB"] + revs))
    kits = [k for k in R.ITEMS if k.startswith("KIT_")]
    if not built(cfg, "DRUM_SYNTH") and not any(built(cfg, k) for k in kits):
        err.append(Issue("the drum track needs a drum source: the drum synth or a sampled kit",
                         ["DRUM_SYNTH", "DRUM_SAMPLED"] + kits))
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
    if built(cfg, "FX_DUCK") and not cfg["DRUM_SYNTH"] and not built(cfg, "DRUM_SAMPLED"):
        warn.append(Issue("DUCK follows the kick", ("FX_DUCK", "DRUM_SYNTH", "DRUM_SAMPLED")))
    for k, it in R.ITEMS.items():
        if it.off_warning and not cfg[k] and (not it.parent or built(cfg, it.parent)):
            warn.append(Issue(f"{it.label} off: {it.off_warning}", [k]))
        if built(cfg, k):
            if it.experimental:
                warn.append(Issue(f"{it.label}: EXPERIMENTAL (emulator-tested only)", [k]))
            if it.notice:
                note.append(Issue(f"{it.label}: {it.notice}", [k]))
    if cfg["USB_MODE"] == 2:
        warn.append(Issue("USB audio: EXPERIMENTAL (the CDC console goes; +12 KB pool)", ["USB_MODE"]))
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
        out[it.flag] = v
    out["FELUCCA_USB_AUDIO"] = 1 if cfg["USB_MODE"] == 2 else 0
    out["FELUCCA_CDC"] = 1 if cfg["USB_MODE"] == 1 else 0
    out["FELUCCA_SLICE"] = cfg["ENG_SLICE"]
    if not built(cfg, "DRUM_SAMPLED") or not any(cfg[k] for k in R.ITEMS if k.startswith("KIT_")):
        out["FELUCCA_DRUM_SAMPLED"] = 0
    skip = [R.ITEMS[k].env for k in R.ITEMS if k.startswith("SET_") and not cfg[k]]
    if not out.get("FELUCCA_DRUM_SAMPLED", 1):
        skip.append("PERC")
    env = {"FELUCCA_SAMPLES_SKIP": ",".join(skip), "FELUCCA_SLICE": str(cfg["ENG_SLICE"]),
           "FELUCCA_SIZE": str(cfg["SIZE"])}
    return out, env


def fnv32(text):
    h = 0x811C9DC5
    for b in text.encode():
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def cfg_hash(cfg):
    return fnv32("".join(f"{k}={cfg[k]}\n" for k in sorted(cfg))) & 0x7FFFFFFF


def cfg_bits(cfg):
    n = max(it.bit for it in R.ITEMS.values()) + 1
    bits = [0] * ((n + 6) // 7)
    for k, it in R.ITEMS.items():
        if built(cfg, k):
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


# ---- the budget (measured deltas, tools/builder/costs.json)
def load_costs(path=COSTS):
    try:
        return json.loads(Path(path).read_text())
    except (OSError, ValueError):
        return None


def item_delta(costs, key, value):
    """bytes this item at this value adds to the default build, per region (None: not measured)"""
    d = costs.get("deltas", {}).get(key, {}).get(str(value))
    return d


def pair_conds(name):
    """a costs.json "pairs" key ("MOTION=1,SECTIONS=4") -> [(key, value)]"""
    return [(k, int(v)) for k, v in (c.split("=") for c in name.split(","))]


def built_parent(cfg, key):
    """the item's parent (if any) is in the build"""
    parent = R.ITEMS[key].parent
    return not parent or built(cfg, parent)


def pair_delta(cfg, costs):
    """what items set together cost beyond their own deltas (costs.json "pairs": measured with all of them set,
    less each one's delta), per region"""
    out = {r: 0 for r in REGIONS}
    for name, d in (costs or {}).get("pairs", {}).items():
        conds = pair_conds(name)
        if all(k in cfg and cfg[k] == v and built_parent(cfg, k) for k, v in conds):   # (an option of an item that is off is not in the build)
            for r in REGIONS:
                out[r] += d.get(r, 0)
    return out


EXACT = ROOT / "build" / "exact-sizes.json"      # the sizes of real builds, by configuration and source


def source_state():
    """what the sizes of a build depend on besides the configuration: the firmware, its assets, build.py and
    the generators (tools/gen*.py), as committed or staged, plus any uncommitted change there"""
    def git(*args):
        r = subprocess.run(["git", "-C", str(ROOT), *args], capture_output=True, text=True)
        return r.stdout if r.returncode == 0 else ""
    paths = ["firmware", "assets", "tools/build.py", ":(glob)tools/gen*.py"]
    tree = git("ls-files", "-s", "--", *paths)
    dirty = git("diff", "HEAD", "--", *paths) + git("ls-files", "--others", "--exclude-standard", "--", *paths)
    return f"{fnv32(tree):08x}-{fnv32(dirty) if dirty else 0:08x}"


def exact_key(cfg):
    return f"{cfg_hash(cfg):08x}-{source_state()}"


def exact_sizes(cfg):
    """the sizes of the last real build of this configuration from the same source, else None"""
    try:
        return json.loads(EXACT.read_text()).get(exact_key(cfg))
    except (OSError, ValueError):
        return None


def remember_sizes(cfg, sizes):
    try:
        known = json.loads(EXACT.read_text())
    except (OSError, ValueError):
        known = {}
    known[exact_key(cfg)] = {r: sizes[r] for r in REGIONS}
    EXACT.parent.mkdir(parents=True, exist_ok=True)
    EXACT.write_text(json.dumps(dict(list(known.items())[-64:]), indent=1) + "\n")   # (the last 64 builds)


def budget(cfg, costs=None):
    """-> {"total": {region: bytes}, "items": {key: {region: delta}}, "unmeasured": [keys], "exact": bool}:
    the total is the last real build's when this configuration was built from this source, else the estimate
    (the per-item deltas stay estimates either way)"""
    costs = costs or load_costs()
    if not costs:
        return None
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
        items[k] = d
        for r in REGIONS:
            total[r] += d.get(r, 0)
    for r, n in pair_delta(cfg, costs).items():
        total[r] += n
    exact = exact_sizes(cfg)
    if exact:
        total = dict(exact)
    return {"total": total, "items": items, "unmeasured": missing, "exact": bool(exact)}


def fits(total):
    """-> {region: (used, capacity, over)}: over > 0 overflows (the pool keeps its 8 KiB spare)"""
    return {r: (total[r], LIMITS[r] - SPARE[r], total[r] - (LIMITS[r] - SPARE[r])) for r in REGIONS}


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


def savings_of(cfg, costs):
    """what switching each built item off (or its smallest choice) would save, per region"""
    out = {}
    for k, it in R.ITEMS.items():
        if not built(cfg, k) or (it.parent and not built(cfg, it.parent)):
            continue
        alt = 0 if not it.is_choice else min((c[0] for c in it.choices), key=lambda v: v if v else 1 << 30)
        if alt == cfg[k]:
            continue
        here = item_delta(costs, k, cfg[k]) if cfg[k] != it.default else {r: 0 for r in REGIONS}
        there = item_delta(costs, k, alt) if alt != it.default else {r: 0 for r in REGIONS}
        if here is None or there is None:
            continue
        p_here, p_there = pair_delta(cfg, costs), pair_delta(dict(cfg, **{k: alt}), costs)
        out[k] = {r: here.get(r, 0) - there.get(r, 0) + p_here[r] - p_there[r] for r in REGIONS}
    return out


# ---- fitting: drop items in a fixed order of least loss until the estimate fits
FIT_ORDER = [  # (key, value): least loss first (docs/MEMORY-BUDGET.md section 3); samples last, PERC never
    ("ENG_SLICE", 0), ("SPLASH", 0), ("ICONS", 0), ("DLY_LEN", 32768), ("OVERVIEW", 0), ("PUNCH_N", 16384),
    ("SL_LEN", 2048), ("ENG_LOFI", 0), ("ENG_FORMANT", 0), ("ENG_PHASE", 0), ("ENG_DRAWBAR", 0),
    ("FM6_OPL", 0), ("SET_SCRCH", 0), ("SET_STRGS", 0), ("SET_HORNS", 0),
]
FIT_MARGIN = {"flash": 1024, "ram": 512, "pool": 0, "ramtext": 256}   # the estimate's error (~0.5 %)


def over_any(cfg, costs, margin=FIT_MARGIN):
    b = budget(cfg, costs)
    return {r: o + margin[r] for r, (_, _, o) in fits(b["total"]).items() if o + margin[r] > 0}


def fit(cfg, costs=None, keep=(), order=FIT_ORDER):
    """-> (cfg that fits by the estimate, [changes]); keep: keys never touched. None if nothing more to drop"""
    costs = costs or load_costs()
    cfg, changes = dict(cfg), []
    for k, v in order:
        over = over_any(cfg, costs)
        if not over:
            break
        if k in keep or cfg[k] == v or (R.ITEMS[k].parent and not built(cfg, R.ITEMS[k].parent)):
            continue
        trial = dict(cfg)
        trial[k] = v
        b0, b1 = budget(cfg, costs)["total"], budget(trial, costs)["total"]
        if not any(b1[r] < b0[r] for r in over):        # (it would not help a region that overflows)
            continue
        cfg = trial
        changes.append(f"{k}={v}")
    return (cfg, changes) if not over_any(cfg, costs) else (None, changes)


# ---- building
def write_header(cfg, name, path=ROOT / "build" / "gen" / "felucca_config.h"):
    path.parent.mkdir(parents=True, exist_ok=True)
    text = header(cfg, name)
    if not path.exists() or path.read_text() != text:
        tmp = path.with_suffix(".tmp")                  # (whole or not at all: the Docker mount has shown a
        with open(tmp, "w") as f:                       # half-written header to the compiler)
            f.write(text)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)
    return path


def build_python():
    """a Python with Pillow for tools/build.py (the generators need it): this one, else the builder's venv"""
    if importlib.util.find_spec("PIL") is not None:
        return sys.executable
    venv = Path(os.environ.get("BUILDER_VENV", HERE / "venv"))
    for py in (venv / "bin" / "python", venv / "Scripts" / "python.exe"):
        try:
            if py.exists():
                return str(py)
        except OSError:                                 # (a Docker mount that refuses the venv's symlink)
            pass
    return sys.executable                               # (build.py then says how to get Pillow)


def in_container():
    """the toolchain runs in a container: any host but Linux x86-64, JIELI_DOCKER=1, or inside the image"""
    native = sys.platform.startswith("linux") and platform.machine() in ("x86_64", "AMD64")
    return (not native or os.environ.get("JIELI_DOCKER") == "1" or os.environ.get("OPTIMIST_IN_CONTAINER") == "1"
            or os.environ.get("JIELI_BACKEND") in ("docker", "image"))


def _run(cmd, env, echo):
    """-> (returncode, output); echo: print each line as it comes (the CLI), else quiet (the menu)"""
    if not echo:
        p = subprocess.run(cmd, cwd=ROOT, env=env, capture_output=True, text=True, encoding="utf-8",
                           errors="replace")
        return p.returncode, p.stdout + p.stderr
    lines = []
    with subprocess.Popen(cmd, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                          encoding="utf-8", errors="replace",
                          bufsize=1) as p:
        for ln in p.stdout:
            sys.stdout.write(ln)
            lines.append(ln)
    return p.returncode, "".join(lines)


def build(cfg, name, measure=False, log=None, extra=(), echo=False):
    """a real build of this configuration (tools/build.py) -> (ok, sizes dict, output text); extra: more
    build.py arguments (--release X.Y)"""
    err, _, _ = validate(cfg)
    if err:
        return False, None, "configuration errors:\n  " + "\n  ".join(err)
    cfgfile = ROOT / "build" / "builder.config"
    cfgfile.parent.mkdir(parents=True, exist_ok=True)
    cfgfile.write_text(dump(cfg, name))
    env = {k: v for k, v in os.environ.items() if not k.startswith("FELUCCA_")}
    cmd = [build_python(), str(ROOT / "tools" / "build.py"), "--config", str(cfgfile),
           *(["--measure"] if measure else []), *extra]
    out, rc = "", 1
    (ROOT / "build" / "sizes.json").unlink(missing_ok=True)   # (a failed build must not report the last one's)
    for attempt in range(3):                            # (Docker: clang crashes now and then, and the mount has
                                                        # shown stale files to the tools; a retry works)
        rc, out = _run(cmd, env, echo)
        flaky = ("core dumped" in out or "Segmentation" in out or "No such file" in out or "Bus error" in out or
                 ("felucca_config.h" in out and "error:" in out))
        if rc == 0 or not flaky or attempt == 2 or not in_container():
            break
        if echo:
            print(f"build: the toolchain failed in a way a retry fixes; again ({attempt + 2} of 3)")
        __import__("time").sleep(2 + 3 * attempt)        # (let the mount settle before the next try)
    if log:
        Path(log).write_text(out)
    sizes = None
    try:
        sizes = json.loads((ROOT / "build" / "sizes.json").read_text())
    except (OSError, ValueError):
        pass
    if sizes and all(r in sizes for r in REGIONS):
        remember_sizes(cfg, sizes)                       # (the menu shows these, exact, from now on)
    return rc == 0, sizes, out


def version():
    """the VERSION file's number plus -dev-<commit> (-modified: uncommitted firmware/tools/web changes),
    as tools/build.py names a development build"""
    v = (ROOT / "VERSION").read_text().strip()
    def git(*args):
        try:                            # (no git: a tree from a zip, or Windows without git)
            r = subprocess.run(["git", "-C", str(ROOT), *args], capture_output=True, text=True)
        except OSError:
            return ""
        return r.stdout.strip() if r.returncode == 0 else ""
    commit = git("rev-parse", "--short", "HEAD") or "local"
    changed = git("status", "--porcelain", "--", "firmware", "tools", "web")
    return f"{v}-dev-{commit}{'-modified' if changed else ''}"


def package(cfg, name, outdir, stem=None, echo=False, summary=None):
    """a real build -> outdir/optimist-<version>-<name>.fwsc + -ui.zip (never a bench or measurement build: the
    builder's environment has no FELUCCA_* variable); -> 0 ok. summary: a JSON file with the result (ok, the
    files, sizes, the configuration's hash), for scripts and CI"""
    ok, sizes, out = build(cfg, name, measure=False, echo=echo)
    pkg, ui = ROOT / "build" / "felucca.fwsc", ROOT / "build" / "felucca-ui.zip"
    result = {"ok": False, "name": name, "hash": f"{cfg_hash(cfg):08x}", "sizes": sizes, "fwsc": None, "ui": None}
    if not ok or not pkg.exists() or not ui.exists():
        if not echo:
            print(out[-2000:])
        print("package: the build failed or does not fit: nothing copied")
        _summary(summary, result)
        return 1
    slug = stem or f"optimist-{version()}-" + re.sub(r"[^a-z0-9]+", "-", (name or "custom").lower()).strip("-")
    outdir.mkdir(parents=True, exist_ok=True)
    (outdir / f"{slug}.fwsc").write_bytes(pkg.read_bytes())
    (outdir / f"{slug}-ui.zip").write_bytes(ui.read_bytes())
    print(f"package: {outdir / (slug + '.fwsc')} (+ -ui.zip): " +
          ", ".join(f"{r} {sizes[r]:,}" for r in REGIONS))
    result.update(ok=True, fwsc=str(outdir / f"{slug}.fwsc"), ui=str(outdir / f"{slug}-ui.zip"))
    _summary(summary, result)
    return 0


def _summary(path, result):
    if path:
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        Path(path).write_text(json.dumps(result, indent=1) + "\n")


def resolve_cli(a):
    if a.config:
        cfg, name = load(a.config)
    elif a.profile:
        cfg, name = load_profile(a.profile)
    else:
        cfg, name = defaults(), "default"
    for s in a.set or []:
        m = re.fullmatch(r"([A-Z0-9_]+)=(-?\d+)", s)
        if not m or m.group(1) not in R.ITEMS:
            raise ConfigError(f"--set {s}: KEY=number with a registry key")
        cfg[m.group(1)] = int(m.group(2))
    return cfg, a.name or name


def fmt_budget(cfg, costs):
    b = budget(cfg, costs)
    if not b:
        return "budget: no tools/builder/costs.json (run tools/builder/measure_costs.py)"
    rows = []
    for r, (used, cap, over) in fits(b["total"]).items():
        rows.append(f"  {r:8s} {used:9,d} / {cap:9,d}  {'OVER by ' + format(over, ',') if over > 0 else 'free ' + format(-over, ',')}")
    if b["unmeasured"]:
        rows.append(f"  (not measured: {', '.join(b['unmeasured'])})")
    head = ("exact (the last real build of this configuration from this source):" if b.get("exact")
            else "estimate (measured deltas, +-~0.5 %):")
    return head + "\n" + "\n".join(rows)


def fmt_profiles():
    pub = set(published_profiles())
    rows = [f"  {'published' if n in pub else 'shipped  '}  {n:24s} {load_profile(n)[1]}" for n in profile_names()]
    rows += [f"  mine       {n:24s} {load_profile(n)[1]}" for n in my_profile_names() if n not in profile_names()]
    return ("profiles (published: CI builds them; mine: config/my-profiles, git-ignored)\n" + "\n".join(rows))


def profile_admin(a):
    """--delete, --share, --publish, --unpublish (in that order: --share x --publish x shares, then publishes)"""
    try:
        if a.delete:
            print(f"deleted {delete_profile(a.delete)}")
        if a.share:
            print(f"shared {share_profile(a.share)} (commit it)")
        for name, on in ((a.publish, True), (a.unpublish, False)):
            if name:
                set_published(name, on)
                print(f"{name}: publish {'yes' if on else 'no'}")
    except (OSError, ConfigError) as e:
        print(f"configure: {e}", file=sys.stderr)
        return 2
    print("published: " + (", ".join(published_profiles()) or "none"))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--profile", help=f"a profile: config/profiles ({', '.join(profile_names())}) or your own in config/my-profiles")
    ap.add_argument("--config", help="a .config file")
    ap.add_argument("--set", action="append", metavar="KEY=V", help="change one item (repeatable)")
    ap.add_argument("--name", help="the configuration's name (BUILD SysEx, package)")
    ap.add_argument("--list", action="store_true", help="the registry, with this configuration's values")
    ap.add_argument("--budget", action="store_true", help="the estimated flash / RAM / pool / RAMTEXT")
    ap.add_argument("--write", metavar="FILE", help="write the .config")
    ap.add_argument("--header", metavar="FILE", help="write the felucca_config.h")
    ap.add_argument("--json", action="store_true", help="the registry as JSON")
    ap.add_argument("--fit", action="store_true", help="drop items (least loss first) until the estimate fits")
    ap.add_argument("--keep", nargs="*", default=[], help="with --fit: items never dropped")
    ap.add_argument("--build", action="store_true", help="build it (exact sizes; a package when it fits)")
    ap.add_argument("--measure", action="store_true", help="with --build: a measurement build (links past the slot)")
    ap.add_argument("--package", metavar="DIR", help="build it and copy optimist-<name>-<date>.fwsc and its -ui.zip to DIR")
    ap.add_argument("--profiles", action="store_true", help="every profile: shipped (published or not) and mine")
    ap.add_argument("--published", action="store_true", help="the shipped profiles CI builds (JSON list)")
    ap.add_argument("--publish", metavar="NAME", help="mark a shipped profile \"# publish: yes\" (CI builds it)")
    ap.add_argument("--unpublish", metavar="NAME", help="mark a shipped profile \"# publish: no\"")
    ap.add_argument("--share", metavar="NAME", help="move one of my profiles to config/profiles (to commit)")
    ap.add_argument("--delete", metavar="NAME", help="delete a profile (mine first, else the shipped one)")
    a = ap.parse_args(argv)
    if a.json:
        print(json.dumps(R.to_json(), indent=1))
        return 0
    if a.published:
        print(json.dumps(published_profiles()))
        return 0
    if a.profiles:
        print(fmt_profiles())
        return 0
    if a.publish or a.unpublish or a.share or a.delete:
        return profile_admin(a)
    try:
        cfg, name = resolve_cli(a)
    except ConfigError as e:
        print(f"configure: {e}", file=sys.stderr)
        return 2
    if a.fit:
        fitted, changes = fit(cfg, keep=a.keep)
        print("fit: " + (", ".join(changes) or "fits as it is") + ("" if fitted else "  -- still does not fit"))
        if not fitted:
            return 1
        cfg = fitted
    err, warn, note = validate(cfg)
    if a.list:
        for g in R.GROUPS:
            print(f"[{g}]")
            for it in R.top_level(g):
                for k in [it.key] + it.children:
                    x = R.ITEMS[k]
                    v = cfg[k]
                    mark = ("[x]" if v else "[ ]") if not x.is_choice else f"<{v}>"
                    print(f"  {'   ' if x.parent else ''}{mark} {k:14s} {x.label}")
    for e in err:
        print(f"ERROR   {e}")
    for w in warn:
        print(f"warning {w}")
    for n in note:
        print(f"NOTICE  {n}")
    if a.budget or a.list:
        print(fmt_budget(cfg, load_costs()))
    if a.write:
        Path(a.write).write_text(dump(cfg, name))
        print(f"wrote {a.write}")
    if a.header:
        Path(a.header).write_text(header(cfg, name))
    if a.package:
        return package(cfg, name, Path(a.package).expanduser())
    if a.build:
        ok, sizes, out = build(cfg, name, measure=a.measure)
        print(out[-3000:])
        if sizes:
            print("exact:", {k: v for k, v in sizes.items() if k != "limits"})
        return 0 if ok else 1
    return 1 if err else 0


if __name__ == "__main__":
    sys.exit(main())
