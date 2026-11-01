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


def toolchain_env(compiler="jieli"):
    """JIELI_TOOLCHAIN (fm1-base's build wants the Linux toolchain's directory) and AC79_SDK (Optimist's SDK files).
    compiler "llvm": pi32v2-llvm's drop-in (tools/toolchain.py llvm_dir, JieLi's layout) instead; where fm1-base runs the
    tools in a container, the toolchain image (JIELI_DOCKER_IMAGE: the drop-in's wrapper needs its python3)"""
    import toolchain as TC
    env = {}
    if compiler == "llvm":
        tc = str(TC.llvm_dir())
        if not TC.has_clang(tc):
            raise FM1BaseError(f"no pi32v2-llvm in {tc}: python tools/optimist.py setup (or the builder's COMPILER item: JieLi)")
        if not TC.is_native_host() or os.environ.get("JIELI_DOCKER") == "1":
            env["JIELI_DOCKER_IMAGE"] = TC.image_name()
    else:
        tc = os.environ.get("JIELI_TOOLCHAIN") or str(Path.home() / ".jieli" / "toolchain")
        if not (Path(tc) / "pi32v2" / "bin" / "clang").exists():
            raise FM1BaseError(f"JIELI_TOOLCHAIN {tc}: no pi32v2/bin/clang (the JieLi Linux toolchain: fm1base/tools/get_toolchain.sh)")
    env["JIELI_TOOLCHAIN"] = tc
    env["AC79_SDK"] = str(TC.sdk_dir())
    return env


def compiler_of(cfgfile):
    """the .config's compiler (its COMPILER item): "jieli" or "llvm" (tools/toolchain.py compiler_of)"""
    import configure
    import toolchain as TC
    cfg, _ = configure.parse(Path(cfgfile).read_text())
    return TC.compiler_of(configure.flags(cfg)[0])


def command(python, release=None, app=None):
    import toolchain as TC
    return [python, str(ready()), str(app or APP), "--sdk", str(TC.sdk_dir()),
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


def measure_toolchain(compiler="jieli"):
    """-> the directory to give fm1-base's build as JIELI_TOOLCHAIN for a measurement build: the real toolchain's entries
    linked, its pi32v2/bin/ld the wrapper tools/fm1base_ld.py (room for any build: fm1-base has no measurement mode).
    Made once per compiler (ROOT/build/measure-toolchain, -llvm), shared by parallel builds: under SHIM_LOCK (measure_costs
    runs them as threads), built in a directory of its own and renamed into place (2026-10-29: costs-all's 288 jobs at
    once removed each other's half-made shim, FileNotFoundError on its links).
    Where fm1-base runs the tools in a container (in_container: macOS, JIELI_DOCKER=1) only this directory is mounted
    (at /opt/jieli), so a link to the host's toolchain dangles there (2026-10-31, the Mac: "stat /opt/jieli/pi32v2/bin/clang:
    no such file"): the shim is then whole in itself (_make_container_shim, -docker)"""
    real = Path(toolchain_env(compiler)["JIELI_TOOLCHAIN"]).resolve()
    boxed = in_container()
    shim = ROOT / "build" / ("measure-toolchain" + ("-llvm" if compiler == "llvm" else "") + ("-docker" if boxed else ""))
    with SHIM_LOCK:
        if (shim / "pi32v2" / "bin" / "ld").is_file() and (shim / "real").resolve() == real and (
                not boxed or _shim_ld_current(shim)):
            return shim
        tmp = shim.with_name(f"{shim.name}.{os.getpid()}")
        shutil.rmtree(tmp, ignore_errors=True)
        (_make_container_shim if boxed else _make_shim)(tmp, real)
        shutil.rmtree(shim, ignore_errors=True)
        os.rename(tmp, shim)
    return shim


SHIM_LOCK = threading.Lock()
LD_PY = HERE / "fm1base_ld.py"


def in_container():
    """fm1-base's build runs the toolchain in a container (its tools/build.py use_docker: anywhere but Linux x86-64, or
    JIELI_DOCKER=1), with only JIELI_TOOLCHAIN's directory mounted, at /opt/jieli"""
    import toolchain as TC
    return os.environ.get("JIELI_DOCKER", "0" if TC.is_native_host() else "1") == "1"


def _shim_ld_current(shim):
    """the container shim's copy of fm1base_ld.py is this checkout's"""
    copy = shim / "measure" / LD_PY.name
    return copy.is_file() and copy.read_bytes() == LD_PY.read_bytes()


def _make_container_shim(shim, real):
    """measure_toolchain's directory at shim for a container: real's tree as hard links (copies across file systems),
    its relative links kept as they are; the real ld at measure/bin/ld (as deep as pi32v2/bin/ld: the drop-in's jlwrap.py
    finds its toolchain three levels up from the name it is run by), a copy of fm1base_ld.py in measure/, and
    pi32v2/bin/ld the wrapper, which finds both from its own path and runs the container's python3 (the toolchain image)"""
    shim.mkdir(parents=True)
    for src in real.iterdir():
        if src.name != "pi32v2":
            _mirror(src, shim / src.name, real)
    (shim / "pi32v2").mkdir()
    for sub in (real / "pi32v2").iterdir():
        if sub.name != "bin":
            _mirror(sub, shim / "pi32v2" / sub.name, real)
    (shim / "pi32v2" / "bin").mkdir()
    for tool in (real / "pi32v2" / "bin").iterdir():
        if tool.name != "ld":
            _mirror(tool, shim / "pi32v2" / "bin" / tool.name, real)
    (shim / "measure" / "bin").mkdir(parents=True)
    _mirror(real / "pi32v2" / "bin" / "ld", shim / "measure" / "bin" / "ld", real)
    shutil.copy2(LD_PY, shim / "measure" / LD_PY.name)
    ld = shim / "pi32v2" / "bin" / "ld"
    ld.write_text('#!/bin/sh\nd=$(dirname "$0")/../..\n'
                  f'FM1B_REAL_LD="$d/measure/bin/ld" exec python3 "$d/measure/{LD_PY.name}" "$@"\n')
    ld.chmod(0o755)
    (shim / "real").symlink_to(real)


def _mirror(src, dst, top):
    """src (inside top) at dst: a relative link that stays inside top as the same link, any other link as what it
    points at, a directory entry by entry, a file hard-linked (copied when dst is on another file system)"""
    if src.is_symlink():
        target = os.readlink(src)
        inside = os.path.normpath(src.parent / target)
        if not os.path.isabs(target) and (inside == str(top) or inside.startswith(str(top) + os.sep)):
            dst.symlink_to(target)
            return
        src = src.resolve(strict=True)
    if src.is_dir():
        dst.mkdir()
        for child in src.iterdir():
            _mirror(child, dst / child.name, top)
        return
    try:
        os.link(src, dst)
    except OSError:
        shutil.copy2(src, dst)


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
    env.update(toolchain_env(compiler_of(cfgfile)))
    env.update(OPTIMIST=str(ROOT), OPT_CONFIG=str(Path(cfgfile).resolve()), OPT_TREE_COPY="1")
    if measure:
        env["JIELI_TOOLCHAIN"] = str(measure_toolchain(compiler_of(cfgfile)))
        if in_container():           # (the wrapper's python3: the toolchain image's, JieLi's too, never plain debian)
            import toolchain as TC
            env["JIELI_DOCKER_IMAGE"] = TC.image_name()
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


# the release's app end of each MEMORY step but normal: gen_optimist.py's names (tools/flash_layout.py STEPS release_end;
# tests/fm1base_test.py checks the two agree)
RELEASE_END_NAMES = {1: "SD_APP_END", 3: "S0_APP_END"}


def release_end(layout):
    """fm1-base's app end for this step (gen_optimist.py), None for normal (the app's fm1.toml app_end)"""
    if not layout:
        return None
    if layout not in RELEASE_END_NAMES:
        raise FM1BaseError(f"LAYOUT {layout}: no release app end (gen_optimist.py has {', '.join(RELEASE_END_NAMES.values())})")
    return _app_value(RELEASE_END_NAMES[layout])


def app_end(cfgfile):
    """the app slot the configuration's flash layout needs: FM1B_APP_END=release_end(layout) for a step past normal
    (LAYOUT 1, the SD card mode: fewer flash projects, by design; LAYOUT 3: gen_optimist.py refuses it until fm1-base's
    loader takes the steps), else None (the app's fm1.toml app_end, the normal slot)"""
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
    release_end()} (1 the SD card mode's, 3 no user samples', which the release does not build yet), "sram": the SRAM region (fm1base/device/app.ld)}. base_sizes: a real normal build's
    sizes.json (its app_slot, what that build checked against): FM1BaseError when the two disagree"""
    tool = _pkg_tool()
    flash = tool.app_slot(normal_app_end())
    if base_sizes is not None and int(base_sizes.get("app_slot", flash)) != flash:
        raise FM1BaseError(f"the build's app slot {base_sizes['app_slot']} B is not fm1pkg_make.app_slot(fm1.toml app_end) = {flash} B")
    ld = (DIR / "device" / "app.ld").read_text()
    m = re.search(r"SRAM\s*\(rwx\)\s*:\s*ORIGIN = 0x[0-9A-Fa-f]+, LENGTH = (0x[0-9A-Fa-f]+)", ld)
    if not m:
        raise FM1BaseError(f"{DIR / 'device' / 'app.ld'}: no SRAM region")
    sram = int(m.group(1), 0)
    import tomllib
    conf = tomllib.loads((APP / "fm1.toml").read_text())
    if "main_stack" in conf or "irq_stack" in conf:   # (fm1-base's build moves the SRAM's end: build.py stack_script)
        sram = 0x7C000 - int(conf.get("main_stack", 24320)) - int(conf.get("irq_stack", 7936)) - 512
    return {"flash": flash, "flash_by_layout": {str(k): tool.app_slot(release_end(k)) for k in sorted(RELEASE_END_NAMES)},
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


def compiler_line(cfgfile, env):
    """'compiler pi32v2-llvm (<its directory>)' / 'compiler JieLi (<directory>)': the .config's COMPILER and the toolchain
    the build gets (a measurement shim's real one)"""
    tc = Path(env["JIELI_TOOLCHAIN"])
    tc = (tc / "real") if (tc / "real").exists() else tc
    name = "pi32v2-llvm" if compiler_of(cfgfile) == "llvm" else "JieLi"
    return f"compiler {name} ({tc.resolve()})"


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
        cmd = [python, "-c", ANNOUNCE, compiler_line(cfgfile, env), *cmd]    # (its first output line: the compiler)
        for stale in (out_dir / "sizes.json", out_dir / "optimist.fwsc"):
            stale.unlink(missing_ok=True)
        rc, out = run(cmd, env)
        if rc == 0:
            collect(bdir, release, name_stem, cfgfile, out_dir)
        elif (out_dir / "sizes.json").is_file():
            Path(bdir).mkdir(parents=True, exist_ok=True)
            shutil.copy(out_dir / "sizes.json", Path(bdir) / "sizes.json")
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
