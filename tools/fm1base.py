# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Henri David
"""The release build: Optimist as an fm1-base app (the submodule fm1base/, its app directory fm1base/optimist).

  ready()                 the submodule is there (else FM1BaseError: what to run)
  build(cfgfile, bdir)    fm1base/tools/build.py optimist with OPTIMIST = this checkout and OPT_CONFIG = the builder's
                          .config: -> (returncode, output); its package and sizes.json are copied into bdir as
                          Optimist's own build leaves them (felucca.fwsc, optimist-<version>.fwsc, sizes.json)
  install_tool()          fm1base/tools/fm1_install.py (the update tool of the fm1-base side)
  site(pkg, version, out) the release's own site (its install page with m-bootloader, the manual, the browser emulator,
                          the package): fm1base/tools/make_site.py on the app's directory with Optimist's site inputs

Optimist's own firmware build stays available as `--classic` (tools/optimist.py, make classic). One fm1-base build at
a time (its build directory is the app's own: fm1base/optimist/build, git-ignored): builds here are serialized."""
import os
import re
import shutil
import subprocess
import sys
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
    """fm1-base's build environment for the toolchain: its JIELI_TOOLCHAIN (the name fm1-base's build.py reads) is
    pi32v2-llvm's drop-in (tools/toolchain.py llvm_dir), run natively (JIELI_DOCKER=0: fm1-base runs tools in a container
    on every host but Linux x86-64 unless told not to), and AC79_SDK (Optimist's SDK files)"""
    import toolchain as TC
    try:
        TC.resolve()                    # (there, and runs on this host: not a Linux copy on a Mac)
    except TC.ToolchainError as e:
        raise FM1BaseError(str(e))
    tc = str(TC.llvm_dir())
    return {"JIELI_TOOLCHAIN": tc, "JIELI_DOCKER": "0", "AC79_SDK": str(TC.sdk_dir())}


def command(python, release=None, app=None):
    import toolchain as TC
    return [python, str(ready()), str(app or APP), "--sdk", str(TC.sdk_dir()),
            *(["--release", release] if release else [])]


def slot_app(bdir):
    """-> (app directory, its build directory) for a build that is to run beside others (measure_costs.py --jobs): a copy
    of the app directory next to it (fm1base/optimist-<bdir's name>, removed by build once its outputs are collected); the
    default build directory (ROOT/build) and a missing bdir build in the app itself"""
    if bdir is None or Path(bdir).resolve() == (ROOT / "build").resolve():
        return APP, OUT
    app = DIR / f"optimist-{Path(bdir).name}"
    shutil.copytree(APP, app, dirs_exist_ok=True, ignore=shutil.ignore_patterns("build", "__pycache__", "tests"))
    return app, app / "build"


def measure_toolchain():
    """-> the directory to give fm1-base's build as JIELI_TOOLCHAIN for a measurement build: the real toolchain's entries
    linked, its pi32v2/bin/ld the wrapper tools/fm1base_ld.py (room for any build: fm1-base has no measurement mode).
    Made once (ROOT/build/measure-toolchain), shared by parallel builds: under SHIM_LOCK (measure_costs runs them as
    threads), built in a directory of its own and renamed into place (2026-10-29: costs-all's 288 jobs at once removed
    each other's half-made shim, FileNotFoundError on its links)"""
    real = Path(toolchain_env()["JIELI_TOOLCHAIN"]).resolve()
    shim = ROOT / "build" / "measure-toolchain"
    with SHIM_LOCK:
        if (shim / "pi32v2" / "bin" / "ld").is_file() and (shim / "real").resolve() == real:
            return shim
        tmp = shim.with_name(f"{shim.name}.{os.getpid()}")
        shutil.rmtree(tmp, ignore_errors=True)
        _make_shim(tmp, real)
        shutil.rmtree(shim, ignore_errors=True)
        os.rename(tmp, shim)
    return shim


SHIM_LOCK = threading.Lock()
LD_PY = HERE / "fm1base_ld.py"


def _make_shim(shim, real):
    """measure_toolchain's directory at shim: real's entries linked, its ld the wrapper"""
    for src in real.iterdir():
        if src.name != "pi32v2":
            (shim / src.name).parent.mkdir(parents=True, exist_ok=True)
            (shim / src.name).symlink_to(src)
    (shim / "pi32v2" / "bin").mkdir(parents=True)
    for sub in (real / "pi32v2").iterdir():
        if sub.name != "bin":
            (shim / "pi32v2" / sub.name).symlink_to(sub)
    for tool in (real / "pi32v2" / "bin").iterdir():
        if tool.name != "ld":
            (shim / "pi32v2" / "bin" / tool.name).symlink_to(tool)
    ld = shim / "pi32v2" / "bin" / "ld"
    ld.write_text(f"#!/bin/sh\nFM1B_REAL_LD={real / 'pi32v2' / 'bin' / 'ld'} exec {sys.executable} {HERE / 'fm1base_ld.py'} \"$@\"\n")
    ld.chmod(0o755)
    (shim / "real").symlink_to(real)


def env_for(cfgfile, slot=None, measure=False):
    env = {k: v for k, v in os.environ.items() if not k.startswith("FELUCCA_")}
    env.update(toolchain_env())
    env.update(OPTIMIST=str(ROOT), OPT_CONFIG=str(Path(cfgfile).resolve()), OPT_TREE_COPY="1")
    if measure:
        env["JIELI_TOOLCHAIN"] = str(measure_toolchain())
    if slot:
        env["FM1B_JOBS"] = "2"       # (builds side by side: each one's compile pool small)
    env.pop("OPT_PROFILE", None)
    env.pop("FM1B_APP_END", None)
    end = app_end(cfgfile)
    if end:
        env["FM1B_APP_END"] = end
    return env


def _app_value(name):
    """-> the integer the app's generator (fm1base/optimist/gen_optimist.py, the one place Optimist's fm1-base app says
    it) assigns to name at its top level: SD_APP_END, the SD card mode's app area end"""
    src = APP / "gen_optimist.py"
    if not src.is_file():
        raise FM1BaseError(f"{src} is not there: run {FETCH}")
    m = re.search(rf"^{name} = (0x[0-9A-Fa-f]+)", src.read_text(), re.M)
    if not m:
        raise FM1BaseError(f"{src}: no {name}")
    return int(m.group(1), 16)


def sd_app_end():
    """fm1-base's bigger app area end, the SD card mode's (LAYOUT 1): gen_optimist.py SD_APP_END (its slot_check
    refuses a LAYOUT 1 build without it)"""
    return _app_value("SD_APP_END")


def release_end(layout):
    """fm1-base's app end for this MEMORY step (Optimist's one table, tools/flash_layout.py STEPS release_end; gen_optimist.py
    checks the build's FM1B_APP_END against the same table), None for normal (the app's fm1.toml app_end)"""
    import flash_layout
    if not layout:
        return None
    if layout not in flash_layout.STEPS:
        raise FM1BaseError(f"LAYOUT {layout}: no MEMORY step ({sorted(flash_layout.STEPS)})")
    return flash_layout.STEPS[layout].release_end


def app_end(cfgfile):
    """the app slot the configuration's flash layout needs: FM1B_APP_END=release_end(layout) for a step past normal
    (LAYOUT 1, the SD card mode, 0xCE000; the other steps at their FM6 voices, as the classic build), else None (the
    app's fm1.toml app_end, the normal slot)"""
    import configure
    cfg, _ = configure.parse(Path(cfgfile).read_text())
    end = release_end(configure.flags(cfg)[0].get("FELUCCA_LAYOUT", 0))
    return f"0x{end:X}" if end else None


def _pkg_tool():
    """fm1-base's package tool (fm1base/tools/fm1pkg_make.py): app_slot(end), the app.bin room fm1-base's build checks
    an image against (tools/build.py app_slot, "exceeds the app slot")"""
    import importlib.util
    path = DIR / "tools" / "fm1pkg_make.py"
    if not path.is_file():
        raise FM1BaseError(f"{path} is not there: run {FETCH}")
    spec = importlib.util.spec_from_file_location("fm1b_pkg_make", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def normal_app_end():
    """the app area's end of a normal build: the app's fm1.toml app_end, else fm1-base's own (fm1pkg_make.py FLASH_SIZE,
    tools/build.py APP_END)"""
    import tomllib
    end = tomllib.loads((APP / "fm1.toml").read_text()).get("app_end")
    return int(end) if end is not None else _pkg_tool().FLASH_SIZE


def limits(base_sizes=None):
    """-> the slots the release's budget counts against, as fm1-base's build computes them: {"flash": the normal
    layout's app slot (fm1pkg_make.py app_slot of fm1.toml's app_end), "flash_by_layout": {layout: app_slot of its
    release_end()} (every MEMORY step but normal), "sram": the SRAM region (fm1base/device/app_packed.ld)}. base_sizes: a real normal build's
    sizes.json (its app_slot, what that build checked against): FM1BaseError when the two disagree"""
    import flash_layout
    tool = _pkg_tool()
    flash = tool.app_slot(normal_app_end())
    if base_sizes is not None and int(base_sizes.get("app_slot", flash)) != flash:
        raise FM1BaseError(f"the build's app slot {base_sizes['app_slot']} B is not fm1pkg_make.app_slot(fm1.toml app_end) = {flash} B")
    ld = (DIR / "device" / "app_packed.ld").read_text()
    m = re.search(r"SRAM\s*\(rwx\)\s*:\s*ORIGIN = 0x[0-9A-Fa-f]+, LENGTH = (0x[0-9A-Fa-f]+)", ld)
    if not m:
        raise FM1BaseError(f"{DIR / 'device' / 'app_packed.ld'}: no SRAM region")
    sram = int(m.group(1), 0)
    import tomllib
    conf = tomllib.loads((APP / "fm1.toml").read_text())
    if "main_stack" in conf or "irq_stack" in conf:   # (fm1-base's build moves the SRAM's end: build.py stack_script)
        sram = 0x7C000 - int(conf.get("main_stack", 24320)) - int(conf.get("irq_stack", 7936)) - 512
    return {"flash": flash, "flash_by_layout": {str(k): tool.app_slot(release_end(k)) for k in sorted(flash_layout.STEPS) if k},
            "sram": sram}


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
    write_install_meta(bdir / f"{name_stem}.fwsc", cfgfile)
    return bdir / f"{name_stem}.fwsc"


def write_install_meta(pkg, cfgfile):
    """PACKAGE.install.json beside the package: the builder's BOOTLOADER (tools/install_meta.py), as Optimist's own
    build writes it"""
    import configure
    import install_meta
    for old in Path(pkg).parent.glob("optimist-*.install.json"):
        old.unlink()
    cfg, name = configure.parse(Path(cfgfile).read_text())
    return install_meta.write(pkg, cfg.get("BOOTLOADER", 1), name or "default", f"{configure.cfg_hash(cfg):08x}")


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
    import srcgit
    commit, dirty = srcgit.commit(ROOT) or "unknown", srcgit.modified(ROOT, "web", "firmware", "fm1base")
    base_commit = os.environ.get("OPTIMIST_FM1BASE_COMMIT", "").strip()[:7] or (   # (the submodule's own commit: only when
        git("-C", str(DIR), "rev-parse", "--short", "HEAD") if srcgit.is_toplevel(DIR) else "")   # DIR is a repository)
    cfg, name = configure.parse(Path(cfgfile).read_text())
    source = (f"Web editor for {pkg.name} (the release: Optimist on fm1-base {base_commit or '?'})\n"
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


# the release build's first output line names the compiler (2026-10-29: a log that did not say, an ELF read to tell): printed,
# then this process becomes the build (the same process: a cancel still ends it; Windows has no exec, a child there)
ANNOUNCE = ("import os, subprocess, sys; print(sys.argv[1], flush=True)\n"
            "if os.name == 'nt': sys.exit(subprocess.call(sys.argv[2:]))\n"
            "os.execv(sys.argv[2], sys.argv[2:])")


def compiler_line(env):
    """'compiler pi32v2-llvm (<its directory>)': the toolchain the build gets (a measurement shim's real one)"""
    tc = Path(env["JIELI_TOOLCHAIN"])
    tc = (tc / "real") if (tc / "real").exists() else tc
    return f"compiler pi32v2-llvm ({tc.resolve()})"


def sys_top(cfgfile):
    """the .config builds SYSTEM > TOP (builder item SYS_TOP)"""
    import configure
    cfg, _ = configure.parse(Path(cfgfile).read_text())
    return str(cfg.get("SYS_TOP", 0)) not in ("0", "")


def top_table(out_dir, env):
    """SYSTEM > TOP's table from the app's ELF (tools/ramtop.py) into its generated headers; the symbols through llvm-nm,
    else the toolchain's nm"""
    import ramtop
    nm = None
    try:
        ramtop.R.find_nm(None)
    except SystemExit:
        nm = str(Path(env["JIELI_TOOLCHAIN"]) / "pi32v2" / "bin" / "nm")
    elf = Path(out_dir) / "optimist.elf"
    ramtop.write(Path(out_dir) / "optgen" / ramtop.HEADER, ramtop.symbols_of(elf, nm), elf.name)


def build(cfgfile, bdir, python, run, release=None, name_stem="optimist", parallel=False, measure=False):
    """one release build of the .config at cfgfile -> (returncode, output); run(cmd, env) -> (rc, output) streams it.
    A build that is over the slot still leaves its sizes.json (a measurement), and no package. measure: with the linker wrapper (measure_toolchain: the link succeeds whatever the size).
    parallel: in a copy of
    the app directory of its own (slot_app), so builds may run side by side (measure_costs.py --jobs N)"""
    app = out_dir = None
    with _LOCKS_GUARD:
        lock = LOCKS.setdefault(str(bdir), threading.Lock()) if parallel else LOCKS.setdefault("", threading.Lock())
    with lock:
        app, out_dir = slot_app(bdir) if parallel else (APP, OUT)
        cmd, env = command(python, release, app), env_for(cfgfile, parallel, measure)
        cmd = [python, "-c", ANNOUNCE, compiler_line(env), *cmd]    # (its first output line: the compiler)
        for stale in (out_dir / "sizes.json", out_dir / "optimist.fwsc"):
            stale.unlink(missing_ok=True)
        rc, out = run(cmd, env)
        if rc == 0 and not measure and sys_top(cfgfile):
            # SYSTEM > TOP's table (op_top.c TOP_RAM, the app's generated headers: build/optgen): the first link's SRAM by
            # module, then the build again with it (the table's size fixed: the same RAM)
            top_table(out_dir, env)
            (out_dir / "optimist.fwsc").unlink(missing_ok=True)
            rc, out2 = run(cmd, env)
            out += out2
        if rc == 0:
            collect(bdir, release, name_stem, cfgfile, out_dir)
        elif (out_dir / "sizes.json").is_file():
            Path(bdir).mkdir(parents=True, exist_ok=True)
            shutil.copy(out_dir / "sizes.json", Path(bdir) / "sizes.json")
        if app != APP:                                   # (slot_app's copy, its outputs collected into bdir: removed, so no
            shutil.rmtree(app, ignore_errors=True)       # fm1base/optimist-<name> trees stay behind after make costs-all)
    return rc, out


def emu_build(cfgfile=None, python=sys.executable):
    """the release's browser emulator (fm1base/tools/build.py optimist --web: Emscripten) -> its directory (APP/build/emu);
    cfgfile: a builder .config, else the app's own profile copy. No toolchain: the emulator is the host build"""
    ready()
    env = {k: v for k, v in os.environ.items() if not k.startswith("FELUCCA_")}
    env["OPTIMIST"] = str(ROOT)
    env.pop("OPT_CONFIG", None)
    if cfgfile:
        env["OPT_CONFIG"] = str(Path(cfgfile).resolve())
    r = subprocess.run([python, str(BUILD_PY), str(APP), "--web"], env=env, capture_output=True, text=True)
    if r.returncode:
        raise FM1BaseError(f"the emulator did not build:\n{r.stdout[-2000:]}{r.stderr[-2000:]}")
    return APP / "build" / "emu"


def site(pkg, version, out, cfgfile=None, emu=True, python=sys.executable):
    """the release's site into out: fm1-base's tools/make_site.py on a staging copy of the app's directory (fm1base/
    optimist-site, removed after) that has Optimist's inputs: the front page (APP/site/landing.md), the manual built from
    docs/guide (tools/site_manual.py), the name, tagline and repo of fm1.toml; its emulator is APP/build/emu (built here
    unless emu is False: a directory with app.wasm is there already). pkg: the release build's package (identity
    FM-1_79 and the version's six digits: tools/build.py --release)"""
    import site_manual
    ready()
    if emu:
        emu_build(cfgfile, python)
    if not (APP / "build" / "emu" / "app.wasm").is_file():
        raise FM1BaseError(f"{APP / 'build' / 'emu'}: no app.wasm (the emulator: {BUILD_PY} {APP} --web)")
    stage = DIR / "optimist-site"
    shutil.rmtree(stage, ignore_errors=True)
    try:
        shutil.copytree(APP, stage, ignore=shutil.ignore_patterns("build", "__pycache__", "tests", "profiles"))
        (stage / "docs").mkdir(exist_ok=True)
        (stage / "docs" / "MANUAL.md").write_text(site_manual.build(), encoding="utf-8")
        (stage / "build").mkdir()
        shutil.copytree(APP / "build" / "emu", stage / "build" / "emu")
        if (APP / "web" / "words").is_dir():
            (stage / "web").mkdir(exist_ok=True)
            shutil.copytree(APP / "web" / "words", stage / "web" / "words", dirs_exist_ok=True)
        r = subprocess.run([python, str(DIR / "tools" / "make_site.py"), str(stage), str(pkg), version, str(out)],
                           capture_output=True, text=True)
        if r.returncode:
            raise FM1BaseError(f"make_site.py failed:\n{r.stdout}{r.stderr}")
        return r.stdout.strip()
    finally:
        shutil.rmtree(stage, ignore_errors=True)
