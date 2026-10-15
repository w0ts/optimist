# SPDX-License-Identifier: GPL-3.0-only
"""Firmware builder: .config files, profiles, validation, the budget, build/gen/felucca_config.h.

The plain (non-TUI) side of tools/menuconfig, usable from scripts and tests:

  python3 tools/builder/configure.py --list
  python3 tools/builder/configure.py --profile drum-machine --budget
  python3 tools/builder/configure.py --profile fm-va-studio --set FX_PUNCH=0 --write my.config
  python3 tools/builder/configure.py --config my.config --build      # real build, exact sizes, the package

A .config is "KEY=value" lines (registry.py keys); missing keys take the registry default, so a profile lists
only what differs. "# name: ..." names the configuration (the BUILD SysEx reports it, max 16 characters).

The rules (parse, validate, flags, the budget, fit, the canonical form and key) live in core.py, pure and shared
with CI and the web page; this module adds what reads the disk or git (profiles, costs.json, the firmware headers,
real builds' sizes), the build and the CLI, and re-exports the core's names as before.
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

# the rules (pure, shared with the web page), re-exported so configure.X works as before ("X as X": an explicit
# re-export). Every importer has tools/builder on sys.path already (it found this module there)
import core as K
from core import (
    ANCHOR_MARGIN as ANCHOR_MARGIN, AUTO_AUX as AUTO_AUX, AUTO_ENC_MAX as AUTO_ENC_MAX, AUTO_ITEMS as AUTO_ITEMS,
    AUTO_LIST as AUTO_LIST, AUTO_MAX as AUTO_MAX, AUTO_NTRK as AUTO_NTRK, AUTO_STORE as AUTO_STORE,
    FIT_MARGIN as FIT_MARGIN, FIT_ORDER as FIT_ORDER, GRAIN_SETS as GRAIN_SETS, LIMITS as LIMITS, NEEDS as NEEDS,
    PART_MEM as PART_MEM, PART_MEM_NONE as PART_MEM_NONE, REGIONS as REGIONS, REV_ROOM_LEN as REV_ROOM_LEN,
    SL24_XS as SL24_XS, SPARE as SPARE, ConfigError as ConfigError, Issue as Issue, additive as additive,
    anchor_cfg as anchor_cfg, anchor_record as anchor_record, auto_built as auto_built, auto_store as auto_store,
    built as built, built_parent as built_parent, canonical as canonical, cfg_bits as cfg_bits, cfg_diff as cfg_diff,
    cfg_hash as cfg_hash, cloud_key as cloud_key, conflicts as conflicts, counted as counted, defaults as defaults,
    distance as distance, dump as dump, effective as effective, fits as fits, flags as flags, fnv32 as fnv32,
    group_delta as group_delta, header as header, is_last_kit as is_last_kit, item_delta as item_delta,
    item_delta_alone as item_delta_alone, kit_keys as kit_keys, kit_shared as kit_shared, model_terms as model_terms,
    pair_conds as pair_conds, pair_delta as pair_delta, parse as parse, part_mem as part_mem,
    reserve_errors as reserve_errors, reserve_flash as reserve_flash, reserve_undo as reserve_undo,
    rev_lines as rev_lines, savings_of as savings_of, undo_ring as undo_ring, undo_short as undo_short,
    validate as validate, evaluate as evaluate, describe as describe)

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import registry as R                                    # noqa: E402  (the registry sits next to this file)
try:
    import backports                                    # noqa: F401,E402  (items of merged backport branches)
except ImportError:
    pass

ROOT = HERE.parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import shared                                           # noqa: E402  (git worktrees: BUILDING.md, "Worktrees")
CONFIG, CONFIG_SHARED = shared.config_dir(ROOT)         # a worktree: the main checkout's profiles, not its copy
PROFILES = CONFIG / "profiles"
MY_PROFILES = CONFIG / "my-profiles"                    # the user's own (git-ignored)


def _noted():
    if CONFIG_SHARED:
        shared.note("profiles", CONFIG)         # (OPTIMIST_LOCAL_PROFILES=1 uses this worktree's own)
COSTS = HERE / "costs.json"


BLE_DROP_HELP = ("with BLE on: the item to remove to make room for it when the build would overflow, a sample set "
                 "(FLUTE, the default when not given here, PIANO, ...) or another big item's key; nothing is "
                 "removed when BLE fits as it is")


def load(path, base=None):
    return parse(Path(path).read_text(), base)


def profile_names():
    """the profiles shipped with the firmware (config/profiles)"""
    _noted()
    return sorted(p.stem for p in PROFILES.glob("*.config"))


def my_profile_names():
    """the user's own profiles (config/my-profiles, git-ignored)"""
    _noted()
    return sorted(p.stem for p in MY_PROFILES.glob("*.config"))


def profile_path(name):
    """a shipped profile first, then the user's own of that name"""
    _noted()
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
    _noted()
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


def apply_env(cfg, env):
    """the old build switches (FELUCCA_X=0/1 in the environment) still work: they override the .config"""
    cfg = dict(cfg)
    for k, it in R.ITEMS.items():
        if it.flag and it.flag in env and re.fullmatch(r"-?\d+u?", env[it.flag]):
            cfg[k] = int(env[it.flag].rstrip("u"))
    if env.get("FELUCCA_LCD_BAUD", "") in ("0", "1", "2", "3", "4"):
        cfg["LCD_BAUD"] = int(env["FELUCCA_LCD_BAUD"])
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


# ---- the budget (measured deltas, tools/builder/costs.json)
def load_costs(path=COSTS):
    try:
        return json.loads(Path(path).read_text())
    except (OSError, ValueError):
        return None


def _needs_auto():
    """-> (keys) the items the firmware switches off without the automation store (core.h: "#if FELUCCA_X &&
    !FELUCCA_AUTO" then "#undef FELUCCA_X": DRUM_ACCENT's lane is stored as its events), read from the headers so a
    new one is covered without an edit here"""
    by_flag = {it.flag: k for k, it in R.ITEMS.items() if it.flag}
    out = []
    for h in sorted((ROOT / "firmware" / "src" / "core").glob("*.h")):
        for f in re.findall(r"^#if (FELUCCA_\w+) && !FELUCCA_AUTO\s*\n#undef \1\b", h.read_text(), re.M):
            if f in by_flag:
                out.append(by_flag[f])
    return tuple(out)


NEEDS_AUTO = _needs_auto()
K.setup(needs_auto=NEEDS_AUTO)


EXACT = ROOT / "build" / "exact-sizes.json"      # the sizes of real builds, by configuration and source


_SOURCE_STATE = [0.0, None]                      # (when, value): one budget asks for it twice


def source_state():
    """what the sizes of a build depend on besides the configuration: the firmware, its assets, build.py and
    the generators (tools/gen*.py), as committed or staged, plus any uncommitted change there (kept 2 s)"""
    import time
    if _SOURCE_STATE[1] is None or time.monotonic() - _SOURCE_STATE[0] > 2:
        _SOURCE_STATE[:] = [time.monotonic(), _source_state()]
    return _SOURCE_STATE[1]


def _source_state():
    def git(*args):
        r = subprocess.run(["git", "-C", str(ROOT), *args], capture_output=True, text=True)
        return r.stdout if r.returncode == 0 else ""
    paths = ["firmware", "assets", "tools/build.py", ":(glob)tools/gen*.py"]
    tree = git("ls-files", "-s", "--", *paths)
    dirty = git("diff", "HEAD", "--", *paths) + git("ls-files", "--others", "--exclude-standard", "--", *paths)
    return f"{fnv32(tree):08x}-{fnv32(dirty) if dirty else 0:08x}"


def exact_key(cfg):
    return f"{cfg_hash(cfg):08x}-{source_state()}"


def exact_builds():
    """-> {exact_key: {region: bytes, "cfg", "built_hash"}} the real builds remembered (remember_sizes)"""
    try:
        known = json.loads(EXACT.read_text())
    except (OSError, ValueError):
        return {}
    return known if isinstance(known, dict) else {}


def exact_sizes(cfg):
    """the sizes of the last real build of this configuration from the same source, else None"""
    e = exact_builds().get(exact_key(cfg))
    return {r: e[r] for r in REGIONS} if isinstance(e, dict) and all(r in e for r in REGIONS) else None


def remember_sizes(cfg, sizes):
    known = exact_builds()
    known[exact_key(cfg)] = dict({r: sizes[r] for r in REGIONS}, **anchor_record(cfg))   # (an anchor: budget)
    EXACT.parent.mkdir(parents=True, exist_ok=True)
    EXACT.write_text(json.dumps(dict(list(known.items())[-64:]), indent=1) + "\n")   # (the last 64 builds)


def exact_anchors():
    """-> [(name, cfg, real sizes)] the real builds made from this very source (build/exact-sizes.json): anchors of
    the estimate besides costs.json's (core.anchors)"""
    src, out = source_state(), []
    for key, e in exact_builds().items():
        acfg = anchor_cfg(e) if key.endswith(src) else None
        if acfg:
            out.append((f"build {key[:8]}", acfg, {r: e[r] for r in REGIONS}))
    return out


def anchors(costs, use_exact=True, exclude=()):
    """core.anchors plus, with use_exact, the real builds from this source"""
    return K.anchors(costs, exclude, exact_anchors() if use_exact else ())


def nearest_anchor(cfg, costs, use_exact=True, exclude=()):
    return K.nearest_anchor(cfg, costs, exclude, exact_anchors() if use_exact else ())


def budget(cfg, costs=None, use_exact=True, anchor=True, exclude=()):
    """core.budget with costs.json read when not given and, unless use_exact is False, the last real build of this
    configuration from this source (exact_sizes) or, without one, the real builds from this source as anchors"""
    costs = costs or load_costs()
    if not costs:
        return None
    exact = exact_sizes(cfg) if use_exact else None
    extra = exact_anchors() if use_exact and anchor and not exact else ()
    return K.budget(cfg, costs, anchor=anchor, exclude=exclude, exact=exact, extra_anchors=extra)


def over_any(cfg, costs, margin=FIT_MARGIN):
    return K.over_any(cfg, costs, margin, budget_fn=budget)


def fit(cfg, costs=None, keep=(), order=FIT_ORDER):
    """core.fit with this budget (real builds' sizes) and costs.json read when not given"""
    return K.fit(cfg, costs or load_costs(), keep, order, budget_fn=budget)


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
    sys.path.insert(0, str(HERE.parent))
    import deps                                         # (the venv: BUILDER_VENV, ./tools/builder/venv or the main checkout's)
    venv = deps.venv_dir()
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


def _run(cmd, env, echo, on_line=None):
    """-> (returncode, output); echo: print each line as it comes (the CLI), else quiet (the menu); on_line: called
    with each line as it comes (the menu's progress bar reads the stage lines)"""
    if not echo and not on_line:
        p = subprocess.run(cmd, cwd=ROOT, env=env, capture_output=True, text=True, encoding="utf-8",
                           errors="replace")
        return p.returncode, p.stdout + p.stderr

    import progress as PR                               # (tools/progress.py; here, not at the top, so a copy of this
                                                        # file that only lists and loads profiles needs nothing else)

    def each(ln):
        if echo:
            print(ln, flush=True)
        if on_line:
            on_line(ln)
    return PR.stream(cmd, on_line=each, cwd=ROOT, env=env)


def build(cfg, name, measure=False, log=None, extra=(), echo=False, on_line=None):
    """a real build of this configuration (tools/build.py) -> (ok, sizes dict, output text); extra: more
    build.py arguments (--release X.Y); on_line: called with each output line while it builds"""
    err, _, _ = validate(cfg)
    if err:
        msg = "configuration errors:\n  " + "\n  ".join(err)
        if echo:                                        # (the CLI: say why; it used to exit 1 with no word)
            print(msg, file=sys.stderr)
        return False, None, msg
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
        rc, out = _run(cmd, env, echo, on_line)
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
    for f in ("LICENSE", "LICENSING.md", "LICENSES/Apache-2.0.txt"):   # next to the package (JieLi SDK files, Apache-2.0)
        dst = outdir / "LICENSES" / Path(f).name if f.startswith("LICENSES/") else outdir / f
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes((ROOT / f).read_bytes())
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
        if m and m.group(1) in R.MERGED:
            into, why = R.MERGED[m.group(1)]
            print(f"warning: --set {s}: {m.group(1)} was merged into {into} ({why})", file=sys.stderr)
            cfg[into] = cfg[into] | int(int(m.group(2)) != 0)
            continue
        if m and m.group(1) in R.RETIRED:
            print(f"warning: --set {s}: {m.group(1)} was dropped and the setting is ignored ({R.RETIRED[m.group(1)]})",
                  file=sys.stderr)
            continue
        if not m or m.group(1) not in R.ITEMS:
            raise ConfigError(f"--set {s}: KEY=number with a registry key")
        cfg[m.group(1)] = int(m.group(2))
    drop = getattr(a, "ble_drop", None)
    if drop is not None:                                # (BLE replaces samples: room.py)
        import room
        if not built(cfg, room.TRIGGER):
            raise ConfigError("--ble-drop: BLE is not on (--set BLE=1)")
        cfg, made = room.make_room(cfg, drop=room.key_of(drop), how="--ble-drop ITEM")
        print("ble-drop: " + (made.note or "BLE fits as it is: nothing removed"))
    return cfg, a.name or name


def fmt_budget(cfg, costs):
    b = budget(cfg, costs)
    if not b:
        return "budget: no tools/builder/costs.json (run tools/builder/measure_costs.py)"
    rows = []
    for r, (used, cap, over) in fits(b["total"], cfg).items():
        rows.append(f"  {r:8s} {used:9,d} / {cap:9,d}  {'OVER by ' + format(over, ',') if over > 0 else 'free ' + format(-over, ',')}")
    if built(cfg, "UNDO_HISTORY"):
        ring, short = undo_ring(b["total"]), undo_short(cfg, b["total"])
        rows.append(f"  undo ring {ring:7,d} B" + (f"  BELOW the {reserve_undo(cfg):,} B reserve by {short:,}" if short else ""))
    if b["unmeasured"]:
        rows.append(f"  (not measured: {', '.join(b['unmeasured'])})")
    head = ("exact (the last real build of this configuration from this source):" if b.get("exact")
            else f"estimate (from the real build of {b['anchor']}, {b['distance']} items away, and the measured deltas):")
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
    ap.add_argument("--ble-drop", metavar="ITEM", help=BLE_DROP_HELP)
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
