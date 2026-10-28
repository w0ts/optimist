# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Henri David
"""The release build: Optimist as an fm1-base app (the submodule fm1base/, its app directory fm1base/optimist).

  ready()                 the submodule is there (else FM1BaseError: what to run)
  build(cfgfile, bdir)    fm1base/tools/build.py optimist with OPTIMIST = this checkout and OPT_CONFIG = the builder's
                          .config: -> (returncode, output); its package and sizes.json are copied into bdir as
                          Optimist's own build leaves them (felucca.fwsc, optimist-<version>.fwsc, sizes.json)
  install_tool()          fm1base/tools/fm1_install.py (the update tool of the fm1-base side)

Optimist's own firmware build stays available as `--classic` (tools/optimist.py, make classic). One fm1-base build at
a time (its build directory is the app's own: fm1base/optimist/build, git-ignored): builds here are serialized."""
import os
import re
import shutil
import subprocess
import threading
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
DIR = ROOT / "fm1base"
APP = DIR / "optimist"
OUT = APP / "build"
BUILD_PY = DIR / "tools" / "build.py"
LOCKS = {}                       # app directory -> its lock: one fm1-base build at a time in each
_LOCKS_GUARD = threading.Lock()
APP_END = 0x93000                # fm1-base's normal app area end (tools/build.py APP_END)
FETCH = ("git submodule update --init --recursive (the fork, git@github.com:w0ts/fm1-base.git, is not public yet: "
         "its access is needed; docs/BUILDING.md, \"The release build\")")


class FM1BaseError(Exception):
    pass


def ready():
    """-> the fm1-base build script; FM1BaseError when the submodule is not checked out (or its bsp is not)"""
    if not BUILD_PY.is_file():
        raise FM1BaseError(f"{DIR} is empty: run {FETCH}")
    if not (DIR / "bsp" / "chip").is_dir():
        raise FM1BaseError(f"{DIR / 'bsp'} is empty (fm1-base's nested submodule): run {FETCH}")
    return BUILD_PY


def install_tool():
    ready()
    return DIR / "tools" / "fm1_install.py"


def toolchain_env():
    """JIELI_TOOLCHAIN (fm1-base's build wants the Linux toolchain's directory) and AC79_SDK (Optimist's SDK files)"""
    import toolchain as TC
    env = {}
    tc = os.environ.get("JIELI_TOOLCHAIN") or str(Path.home() / ".jieli" / "toolchain")
    if not (Path(tc) / "pi32v2" / "bin" / "clang").exists():
        raise FM1BaseError(f"JIELI_TOOLCHAIN {tc}: no pi32v2/bin/clang (the JieLi Linux toolchain: fm1base/tools/get_toolchain.sh)")
    env["JIELI_TOOLCHAIN"] = tc
    env["AC79_SDK"] = str(TC.sdk_dir())
    return env


def command(python, release=None, app=None):
    return [python, str(ready()), str(app or APP), "--sdk", toolchain_env()["AC79_SDK"],
            *(["--release", release] if release else [])]


def slot_app(bdir):
    """-> (app directory, its build directory) for a build that is to run beside others (measure_costs.py --jobs): a copy
    of the app directory next to it (fm1base/optimist-<bdir's name>, removed by the caller), made again at every build; the
    default build directory (ROOT/build) and a missing bdir build in the app itself"""
    if bdir is None or Path(bdir).resolve() == (ROOT / "build").resolve():
        return APP, OUT
    app = DIR / f"optimist-{Path(bdir).name}"
    shutil.copytree(APP, app, dirs_exist_ok=True, ignore=shutil.ignore_patterns("build", "__pycache__", "tests"))
    return app, app / "build"


def env_for(cfgfile, slot=None):
    env = {k: v for k, v in os.environ.items() if not k.startswith("FELUCCA_")}
    env.update(toolchain_env())
    env.update(OPTIMIST=str(ROOT), OPT_CONFIG=str(Path(cfgfile).resolve()), OPT_TREE_COPY="1")
    if slot:
        env["FM1B_JOBS"] = "2"       # (builds side by side: each one's compile pool small)
    env.pop("OPT_PROFILE", None)
    env.pop("FM1B_APP_END", None)
    end = app_end(cfgfile)
    if end:
        env["FM1B_APP_END"] = end
    return env


SD_APP_END = "0xCE000"           # fm1-base's bigger app slot: Optimist's SD card mode (LAYOUT 1) needs it (gen_optimist.py slot_check)


def app_end(cfgfile):
    """the app slot the configuration's flash layout needs: SD_APP_END for LAYOUT 1 (the SD card mode: fewer flash
    projects, by design), else None (fm1-base's normal slot, 0x93000)"""
    import configure
    cfg, _ = configure.parse(Path(cfgfile).read_text())
    return SD_APP_END if configure.flags(cfg)[0].get("FELUCCA_LAYOUT", 0) == 1 else None


def limits(base_sizes):
    """-> the slots the release's budget counts against, from a real build's sizes.json (the default build's): {"flash": the
    app slot (sizes.json app_slot), "flash_by_layout": {"1": the SD card mode's, FM1B_APP_END = SD_APP_END: the slot grows by
    the app area's growth}, "sram": the SRAM region (fm1base/device/app.ld)}"""
    import re
    flash = int(base_sizes["app_slot"])
    ld = (DIR / "device" / "app.ld").read_text()
    m = re.search(r"SRAM\s*\(rwx\)\s*:\s*ORIGIN = 0x[0-9A-Fa-f]+, LENGTH = (0x[0-9A-Fa-f]+)", ld)
    if not m:
        raise FM1BaseError(f"{DIR / 'device' / 'app.ld'}: no SRAM region")
    return {"flash": flash, "flash_by_layout": {"1": flash + int(SD_APP_END, 0) - APP_END}, "sram": int(m.group(1), 0)}


def collect(bdir, release, name_stem, cfgfile, out=OUT):
    """fm1-base's outputs (in out) -> bdir, under Optimist's names -> the package's path or None"""
    bdir, OUT = Path(bdir), Path(out)
    sizes = OUT / "sizes.json"
    bdir.mkdir(parents=True, exist_ok=True)
    if sizes.is_file():
        shutil.copy(sizes, bdir / "sizes.json")
    pkg = OUT / (f"optimist-{release}.fwsc" if release else "optimist.fwsc")
    if not pkg.is_file():
        return None
    for old in bdir.glob("optimist-*.fwsc"):
        old.unlink()
    shutil.copy(pkg, bdir / "felucca.fwsc")
    shutil.copy(pkg, bdir / f"{name_stem}.fwsc")
    for old in bdir.glob("optimist-*-ui.zip"):
        old.unlink()
    shutil.copy(ui_sidecar(bdir / f"{name_stem}.fwsc", cfgfile), bdir / "felucca-ui.zip")
    for f in ("optimist.bin",):
        if (OUT / f).is_file():
            shutil.copy(OUT / f, bdir / "felucca.bin")
    return bdir / f"{name_stem}.fwsc"


def ui_sidecar(pkg, cfgfile):
    """PACKAGE-ui.zip beside the package: the web editor as this tree has it (web/editor.html and its fonts and notices:
    the app answers its SysEx, fm1base/optimist/ui/opt_editor.h) and SOURCE.txt (commit, whether the tree had changes, the
    configuration's name and hash: tools/flash.py reads it): what Optimist's own build leaves too (tools/build.py)"""
    import configure
    pkg = Path(pkg)
    web = ROOT / "web"

    def git(*a):
        r = subprocess.run(["git", "-C", str(ROOT), *a], capture_output=True, text=True)
        return r.stdout.strip() if r.returncode == 0 else ""
    commit, dirty = git("rev-parse", "HEAD") or "unknown", git("status", "--porcelain", "--", "web", "firmware", "fm1base")
    cfg, name = configure.parse(Path(cfgfile).read_text())
    source = (f"Web editor for {pkg.name} (the release: Optimist on fm1-base {git('-C', str(DIR), 'rev-parse', '--short', 'HEAD') or '?'})\n"
              f"from {ROOT} at {commit}{' (with uncommitted changes)' if dirty else ''}, web/editor.html (GPL-3.0-only)\n"
              f"build configuration: {name or 'default'} hash {configure.cfg_hash(cfg):08x}\n")
    notices = ("LICENSE", "LICENSING.md") + tuple(sorted("LICENSES/" + p.name for p in (ROOT / "LICENSES").glob("*.txt")))
    out = pkg.with_name(pkg.stem + "-ui.zip")
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        def add(n, data):
            zi = zipfile.ZipInfo(n, date_time=(2026, 1, 1, 0, 0, 0))
            zi.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(zi, data)
        add("index.html", (web / "editor.html").read_bytes())
        for f in ("fukiai.ttf", "FUKIAI-LICENSE.txt", "TERMINUS-LICENSE.txt", "JETBRAINSMONO-LICENSE.txt"):
            if (web / f).exists():
                add(f, (web / f).read_bytes())
        for f in notices:
            add(f, (ROOT / f).read_bytes())
        add("SOURCE.txt", source)
    return out


def build(cfgfile, bdir, python, run, release=None, name_stem="optimist", parallel=False):
    """one release build of the .config at cfgfile -> (returncode, output); run(cmd, env) -> (rc, output) streams it.
    A build that is over the slot still leaves its sizes.json (a measurement), and no package. parallel: in a copy of
    the app directory of its own (slot_app), so builds may run side by side (measure_costs.py --jobs N)"""
    app = out_dir = None
    with _LOCKS_GUARD:
        lock = LOCKS.setdefault(str(bdir), threading.Lock()) if parallel else LOCKS.setdefault("", threading.Lock())
    with lock:
        app, out_dir = slot_app(bdir) if parallel else (APP, OUT)
        cmd, env = command(python, release, app), env_for(cfgfile, parallel)
        for stale in (out_dir / "sizes.json", out_dir / "optimist.fwsc"):
            stale.unlink(missing_ok=True)
        rc, out = run(cmd, env)
        if rc == 0:
            collect(bdir, release, name_stem, cfgfile, out_dir)
        elif (out_dir / "sizes.json").is_file():
            Path(bdir).mkdir(parents=True, exist_ok=True)
            shutil.copy(out_dir / "sizes.json", Path(bdir) / "sizes.json")
    return rc, out
