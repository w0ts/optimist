#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The cross-platform entry point (tools/optimist.py) without a build: the command line, the toolchain's
backends and their command lines, the SDK lookup, the emulator launcher's firmware matching. Run by
tests/run_tests.sh and `python tools/optimist.py test --python`."""
import contextlib
import io
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "web"))
import emu
import optimist
import toolchain as TC

fails = 0
REAL_STREAMS = (sys.stdout, sys.stderr)


def check(what, ok):
    global fails
    print(f"{what:78s} {'ok' if ok else 'FAIL'}")
    fails += not ok


def quiet(fn, *a):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        try:
            rc = fn(*a)
        except SystemExit as e:
            rc = e.code
    return rc, out.getvalue() + err.getvalue()


def raises(fn, *a):
    try:
        fn(*a)
    except TC.ToolchainError as e:
        return str(e)
    return None


# the command line
ap = optimist.parser()
a = ap.parse_args(["build", "--profile", "drum-machine", "--set", "FX_PUNCH=0", "--release", "1.2"])
check("build: profile, --set and --release parse", a.profile == "drum-machine" and a.set == ["FX_PUNCH=0"] and
      a.release == "1.2" and a.fn is optimist.cmd_build)
a = ap.parse_args(["package", "--config", "x.config", "--out", "out", "--summary", "s.json", "--in-docker"])
check("package: --config, --out, --summary, --in-docker", a.config == "x.config" and a.out == "out" and
      a.summary == "s.json" and a.in_docker)
a = ap.parse_args(["test", "--python", "--in-docker"])
check("test: --python, --in-docker", a.python and a.in_docker and a.fn is optimist.cmd_test)
a = ap.parse_args(["setup", "--check", "--no-emu"])
check("setup: --check (fetch nothing), --no-emu", a.check and a.no_emu and not a.yes and a.fn is optimist.cmd_setup)
seen = []
saved = dict(optimist.PASSTHROUGH)
optimist.PASSTHROUGH.update(emu=lambda ns: seen.append(ns.rest) or 0, config=lambda ns: seen.append(ns.rest) or 0,
                            cpu=lambda ns: seen.append(ns.rest) or 0)
optimist.main(["emu", "optimist", "--cpu", "96", "--bg"])
optimist.main(["config", "--profile", "x", "--list"])
optimist.main(["cpu", "30", "--port", "/dev/x", "--csv", "c.csv"])
optimist.PASSTHROUGH.update(saved)
check("emu, config and cpu: their arguments go to them untouched",
      seen == [["optimist", "--cpu", "96", "--bg"], ["--profile", "x", "--list"], ["30", "--port", "/dev/x", "--csv", "c.csv"]])
rc, _ = quiet(ap.parse_args, ["build", "--profile", "a", "--config", "b"])
check("build: --profile and --config together is a usage error", rc == 2)
cfg, name = optimist.load_config(ap.parse_args(["build"]))
check(f"build without a configuration builds {optimist.DEFAULT_PROFILE}", name and cfg)
rc, out = quiet(optimist.main, ["build", "--config", str(ROOT / "no-such.config")])
check("a missing .config: exit 2 and its name", rc == 2 and "no-such.config" in out)
rc, out = quiet(optimist.main, ["config", "--profile", "drum-machine", "--budget"])
check("config passes through to configure.py (--budget)",   # (exact: a real build of it from this source exists)
      rc == 0 and ("estimate (" in out or "exact (the last real build" in out))
p = subprocess.run([sys.executable, str(ROOT / "tools" / "optimist.py"), "--help"], capture_output=True, text=True)
check("optimist.py --help runs as a script", p.returncode == 0 and "setup" in p.stdout and "emu" in p.stdout)

# the toolchain backends
check("JIELI_BACKEND must name a backend", "use one of" in (raises(TC.resolve, {"JIELI_BACKEND": "qemu"}) or ""))
with tempfile.TemporaryDirectory() as d:
    check("JIELI_TOOLCHAIN without pi32v2/bin/clang is refused",
          "no pi32v2/bin/clang" in (raises(TC.resolve, {"JIELI_TOOLCHAIN": d}) or ""))
    (Path(d) / "pi32v2" / "bin").mkdir(parents=True)
    (Path(d) / "pi32v2" / "bin" / "clang").write_text("")
    check("has_clang finds a toolchain directory", TC.has_clang(d) and not TC.has_clang(None))
    if TC.is_native_host():
        b = TC.resolve({"JIELI_TOOLCHAIN": d})
        check("Linux x86-64 + JIELI_TOOLCHAIN: native", b.kind == "native")
    sdk = Path(d) / "sdk"
    check("SDK: AC79_SDK wins, files missing are named", TC.sdk_dir({"AC79_SDK": str(sdk)}) == sdk and
          TC.sdk_missing(sdk) == list(TC.SDK_SHA256))
def _mk_sdk(base):
    f = Path(base) / "cpu" / "wl82" / "tools" / "uboot.boot"
    f.parent.mkdir(parents=True)
    f.write_text("")
    return Path(base)
_root0, _home0 = TC.ROOT, os.environ.get("HOME")
_td = tempfile.mkdtemp()
fake_root, fake_home = Path(_td) / "fakeroot", Path(_td) / "fakehome"
fake_root.mkdir(); fake_home.mkdir()
try:
    TC.ROOT, os.environ["HOME"] = fake_root, str(fake_home)
    check("SDK: nothing found -> the repo's sdk/", TC.sdk_dir({}) == fake_root / "sdk")
    legacy = _mk_sdk(fake_home / "fw-AC79_AIoT_SDK")
    check("SDK: the legacy home folder is still found", TC.sdk_dir({}) == legacy)
    repo_sdk = _mk_sdk(fake_root / "sdk")
    check("SDK: ROOT/sdk beats the legacy home folder", TC.sdk_dir({}) == repo_sdk)
    check("SDK: AC79_SDK beats ROOT/sdk", TC.sdk_dir({"AC79_SDK": str(sdk)}) == sdk)
finally:
    TC.ROOT = _root0
    if _home0 is None: os.environ.pop("HOME", None)
    else: os.environ["HOME"] = _home0
src = Path("/src/sloop")
nat = TC.Backend("native", "/tc").command("pi32v2/bin/clang", ["-c", "a.c"], src)
check("native: the tool under sh with core files off", nat[:3] == ["sh", "-c", TC.NO_CORE] and
      Path(nat[3]) == Path("/tc/pi32v2/bin/clang") and nat[4:] == ["-c", "a.c"])
dk = TC.Backend("docker", "/tc", "debian:bookworm-slim").command("common/bin/objdump", ["-d", "x.elf"], src)
check("docker: linux/amd64, tree at /work, toolchain mounted read-only",
      dk[:6] == ["docker", "run", "--rm", "--platform", "linux/amd64", "--ulimit"] and f"{src}:/work" in dk and
      "/tc:/opt/jieli:ro" in dk and dk[-3:] == ["/opt/jieli/common/bin/objdump", "-d", "x.elf"])
im = TC.Backend("image", image="optimist-toolchain:x").command("pi32v2/bin/ld", ["-T", "a.ld"], src)
check("image: no toolchain mount, the image's /opt/jieli",
      not any(x.endswith(":/opt/jieli:ro") for x in im) and "optimist-toolchain:x" in im and
      im[-3:] == ["/opt/jieli/pi32v2/bin/ld", "-T", "a.ld"])
TC._WSL_PATHS[(str(src), "Ubuntu")] = "/mnt/c/src/sloop"
ws = TC.Backend("wsl", "$HOME/.jieli/toolchain", distro="Ubuntu").command("pi32v2/bin/clang", ["-c", "a.c"], src)
check("wsl: the distribution, the tree's WSL path, the tool under sh",
      ws[:6] == ["wsl", "-d", "Ubuntu", "--cd", "/mnt/c/src/sloop", "--exec"] and ws[-3:] == ["pi32v2/bin/clang",
                                                                                            "-c", "a.c"])
if os.name != "nt" and emu.shutil.which("sh"):
    # the wsl backend's shell part, run by this host's sh in place of WSL's: the toolchain path is an argument,
    # a leading ~/ or $HOME/ becomes the home, anything else ($, quotes) stays as it is
    with tempfile.TemporaryDirectory() as d:
        home = Path(d) / "home"
        tool = home / ".jieli" / "toolchain" / "pi32v2" / "bin"
        tool.mkdir(parents=True)
        (tool / "clang").write_text('#!/bin/sh\necho "clang $*"\n')
        (tool / "clang").chmod(0o755)
        odd = Path(d) / 'a "$(x)" b'
        (odd / "pi32v2" / "bin").mkdir(parents=True)
        (odd / "pi32v2" / "bin" / "clang").write_text('#!/bin/sh\necho "odd $*"\n')
        (odd / "pi32v2" / "bin" / "clang").chmod(0o755)
        outs = []
        TC._WSL_PATHS[(str(src), "")] = "/mnt/c/src/sloop"
        for tc_path in ("$HOME/.jieli/toolchain", "~/.jieli/toolchain", str(odd)):
            ws_cmd = TC.Backend("wsl", tc_path).command("pi32v2/bin/clang", ["-c", "a b.c"], src)
            sh_part = ws_cmd[ws_cmd.index("sh"):]          # what WSL would run
            r = subprocess.run(sh_part, capture_output=True, text=True, env=dict(os.environ, HOME=str(home)))
            outs.append(r.stdout.strip())
    check("wsl: the toolchain path is an argument ($HOME/ and ~/ expanded, a quoted $(x) left alone)",
          outs == ["clang -c a b.c", "clang -c a b.c", "odd -c a b.c"])
TC.time.sleep, slept = (lambda s: None), TC.time.sleep
nf = "clang: error: unable to open output file 'build/x.o': 'No such file or directory'"
check("a tool in a container is run again after a mount lag or a crash, up to TOOL_TRIES",
      TC.retry_tool(dk_b := TC.Backend("docker", "/tc", "debian"), 1, nf, 0) and
      TC.retry_tool(dk_b, 139, "Segmentation fault (core dumped)", TC.TOOL_TRIES - 2) and
      not TC.retry_tool(dk_b, 1, nf, TC.TOOL_TRIES - 1))
check("never for a success, a real compile error, or a native tool",
      not TC.retry_tool(dk_b, 0, nf, 0) and not TC.retry_tool(dk_b, 1, "a.c:3: error: expected ';'", 0) and
      (os.environ.get("OPTIMIST_IN_CONTAINER") == "1" or not TC.retry_tool(TC.Backend("native", "/tc"), 1, nf, 0)))
TC.time.sleep = slept
check("the image name follows the pinned toolchain version, JIELI_TOOLCHAIN_IMAGE overrides",
      TC.image_name({}) == f"optimist-toolchain:{TC.TOOLCHAIN_VERSION}" and
      TC.image_name({"JIELI_TOOLCHAIN_IMAGE": "x:1"}) == "x:1")
dockerfile = (ROOT / "tools" / "docker" / "Dockerfile").read_text()
check("the Dockerfile pins the same toolchain and SDK files as tools/toolchain.py",
      f"TOOLCHAIN_VERSION={TC.TOOLCHAIN_VERSION}" in dockerfile and TC.TOOLCHAIN_SHA256 in dockerfile and
      f"SDK_TAG={TC.SDK_TAG}" in dockerfile and all(h in dockerfile for h in TC.SDK_SHA256.values()))

# the emulator launcher
with tempfile.TemporaryDirectory() as d:
    imgs, bld = Path(d) / "firmwares", Path(d) / "build"
    (imgs / "sub").mkdir(parents=True)
    bld.mkdir()
    for i, n in enumerate(("firmwares/stock-v15.fwsc", "firmwares/optimist-drum.fwsc", "firmwares/sub/optimist-fm.fwsc",
                           "build/felucca.fwsc", "build/optimist-0.1-dev-abc1234.fwsc")):
        (Path(d) / n).write_bytes(b"x")
        os.utime(Path(d) / n, (time.time() - 100 + i, time.time() - 100 + i))
    listed = emu.find_firmware(imgs, bld)
    found = [p for p, o in listed if o == "firmwares/"]
    check("emu: .fwsc in firmwares/ and one folder down, newest first",
          [p.name for p in found] == ["optimist-fm.fwsc", "optimist-drum.fwsc", "stock-v15.fwsc"])
    check("emu: build/ lists the named packages only (not felucca.fwsc), first",
          [(p.name, o) for p, o in listed[:1]] == [("optimist-0.1-dev-abc1234.fwsc", "build/")] and len(listed) == 4)
    entries = [(p, "firmwares/") for p in found]
    check("emu: an exact name, with or without .fwsc", emu.match(entries, "stock-v15").name == "stock-v15.fwsc")
    check("emu: a unique part of a name", emu.match(entries, "drum").name == "optimist-drum.fwsc")
    try:
        emu.match(entries, "optimist")
        ambiguous = False
    except emu.EmuError as e:
        ambiguous = "matches 2 files" in str(e)
    check("emu: an ambiguous part names the matches", ambiguous)
    check("emu: a path is taken as it is", emu.match(entries, str(imgs / "stock-v15.fwsc")) == imgs / "stock-v15.fwsc")
refused = 0
for bad in ("0", "1001", "fast"):
    try:
        emu.check_cpu(bad)
    except emu.EmuError:
        refused += 1
check("emu: --cpu 1..1000 (0, 1001, words refused), empty = 96, 'own' = the firmware's own clock",
      emu.check_cpu("120") == 120 and emu.check_cpu("") == 96 and emu.check_cpu(None) == 96 and
      emu.check_cpu("own") is None and refused == 3)
check("emu: the clone and the logs in emulator/ (no hidden folder)",
      emu.CLONE == ROOT / "emulator" / "fm1-emulator" and emu.images_dir({}) == ROOT / "firmwares")
check("no hidden folder for the builder's venv", optimist.deps.venv_dir().name == "venv" or "BUILDER_VENV" in os.environ)
check("emu: the fork by default, main for upstream",
      emu.emu_source({}) == (emu.FORK_URL, "feat/upstream-merge") and
      emu.emu_source({"EMU_REPO": emu.UPSTREAM_URL}) == (emu.UPSTREAM_URL, "main"))
check("emu: the executable's name on this host", emu.exe_name() == ("fm1-ui.exe" if os.name == "nt" else "fm1-ui"))
with tempfile.TemporaryDirectory() as d:
    ours, theirs = Path(d) / "ours", Path(d) / "theirs"
    for crate in (ours, theirs):
        (crate / "target" / "release").mkdir(parents=True)
        (crate / "src").mkdir()
    (ours / "src" / "flash_state.rs").write_text("")
    exe = ours / "target" / "release" / emu.exe_name()
    state = ["--state", str(emu.STATE) + os.sep]
    check("emu: the flash is kept in emulator/state/ (visible), --fresh starts without it",
          emu.STATE == ROOT / "emulator" / "state" and emu.state_args(exe, False) == state and
          emu.state_args(exe, True) == state + ["--fresh"])
    check("emu: an emulator without flash states gets no state options (upstream's would refuse them)",
          emu.state_args(theirs / "target" / "release" / emu.exe_name(), True) == [])
check("emu: --fresh on the command line (make emu FRESH=1)",
      emu.parser().parse_args(["optimist", "--fresh"]).fresh and not emu.parser().parse_args(["optimist"]).fresh)


def git_in(repo, *args):
    subprocess.run(["git", "-C", str(repo), "-c", "user.name=t", "-c", "user.email=t@t", *args],
                   check=True, capture_output=True)


if emu.shutil.which("git"):
    with tempfile.TemporaryDirectory() as d:
        up, clone = Path(d) / "up", Path(d) / "emulator" / "fm1-emulator"
        up.mkdir()
        git_in(up, "init", "-q", "-b", "feat/upstream-merge")
        git_in(up, "commit", "-q", "--allow-empty", "-m", "one")
        env = {"EMU_REPO": str(up)}
        rc_clone, _ = quiet(emu.ensure_clone, False, env, clone)
        rc_same, _ = quiet(emu.ensure_clone, False, env, clone)
        git_in(up, "commit", "-q", "--allow-empty", "-m", "two")
        rc_off, _ = quiet(emu.ensure_clone, False, dict(env, EMU_OFFLINE="1"), clone)
        rc_moved, out_moved = quiet(emu.ensure_clone, False, env, clone)
        rc_gone, out_gone = quiet(emu.ensure_clone, False, {"EMU_REPO": str(Path(d) / "nowhere")}, clone)
    check("emu: clone, then every run fetches; rebuild only when the branch moved; EMU_OFFLINE; offline goes on",
          rc_clone is True and rc_same is False and rc_off is False and rc_moved is True and
          "(1): updating" in out_moved and rc_gone is False and "offline?" in out_gone)

import tarfile
deps = optimist.deps
with tempfile.TemporaryDirectory() as d:
    bad, good = Path(d) / "bad.tar", Path(d) / "good.tar"
    (Path(d) / "f.txt").write_text("x")
    with tarfile.open(bad, "w") as t:
        t.add(Path(d) / "f.txt", arcname="../escape.txt")
    with tarfile.open(good, "w") as t:
        t.add(Path(d) / "f.txt", arcname="tc/f.txt")
    results = []
    modes = (True, False) if hasattr(tarfile, "data_filter") else (False,)
    for with_filter in modes:                   # this Python's filter, then the check for Pythons without it
        out = Path(d) / f"out{with_filter}"
        deps.extract(good, out, with_filter)
        try:
            deps.extract(bad, out, with_filter)
            refused = False
        except tarfile.TarError:
            refused = True
        results.append((out / "tc" / "f.txt").is_file() and refused and not (Path(d) / "escape.txt").exists())
check("setup: the toolchain archive is extracted inside its folder only (with and without tarfile's filter)",
      results == [True] * len(modes))

# release notices: the JieLi SDK files (Apache-2.0) travel with every package, so every output carries the licences
import shutil
import zipfile
import build as BUILD
NOTICES = ("LICENSE", "LICENSING.md", "LICENSES/Apache-2.0.txt")
check("notices: LICENSES/Apache-2.0.txt is the Apache License 2.0",
      "Apache License\n                           Version 2.0, January 2004" in (ROOT / "LICENSES/Apache-2.0.txt").read_text())
check("notices: LICENSING.md lists the three JieLi SDK files with their Apache-2.0 licence",
      all(n in (ROOT / "LICENSING.md").read_text() for n in ("uboot.boot", "cfg_tool.bin", "eq_cfg_hw.bin", "AC79NN_SDK_V1.2.1_2023-12-13")))
with tempfile.TemporaryDirectory() as d:
    ui = BUILD.ui_sidecar(Path(d) / "x.fwsc")
    names = zipfile.ZipFile(ui).namelist()
    check("notices: the -ui.zip carries LICENSE, LICENSING.md and LICENSES/Apache-2.0.txt", all(n in names for n in NOTICES))
    import importlib
    ms = importlib.import_module("make_site")
    fwsc = next((c for c in (ROOT / "build" / "felucca.fwsc", *sorted((ROOT / "build").glob("optimist-*.fwsc"))) if c.exists()), None)
    if fwsc is None:
        print("notices: no package in build/: the site check is skipped")
    else:
        os.environ["OPTIMIST_SOURCE_REF"] = "v9.9-test"
        ms.main(fwsc, "9.9-test", Path(d) / "site")
        site = Path(d) / "site"
        page = (site / "webapp/installer/index.html").read_text(encoding="utf-8")
        check("notices: the site root carries LICENSE, LICENSING.md and LICENSES/Apache-2.0.txt", all((site / n).is_file() for n in NOTICES))
        check("notices: the flasher's Source link is w0ts/optimist at the tag, not Felucca",
              'href="https://github.com/w0ts/optimist/tree/v9.9-test"' in page and "hugelton/Felucca" not in page)
        check("notices: the flasher's footer no longer says the sample pack is not CC0", "not CC0" not in page)

# git worktrees: the SDK files, the venv, the emulator clone resolve to the main checkout (tools/shared.py)
import hashlib
import threading
import shared

if emu.shutil.which("git"):
    _env0 = {k: os.environ.get(k) for k in ("HOME", "USERPROFILE", "BUILDER_VENV", "AC79_SDK", shared.NOTED_ENV)}
    with tempfile.TemporaryDirectory() as d:
        d = Path(d).resolve()
        main_repo, wt, plain, home = d / "main", d / "wt", d / "plain", d / "home"
        for p in (main_repo, plain, home):
            p.mkdir()
        git_in(main_repo, "init", "-q", "-b", "main")
        git_in(main_repo, "commit", "-q", "--allow-empty", "-m", "one")
        git_in(main_repo, "worktree", "add", "-q", str(wt), "-b", "feat/x")
        os.environ["HOME"] = os.environ["USERPROFILE"] = str(home)
        for k in ("BUILDER_VENV", "AC79_SDK", shared.NOTED_ENV):
            os.environ.pop(k, None)
        try:
            check("worktree: the main checkout is found from a linked worktree, not from itself or a plain folder",
                  shared.main_checkout(wt) == main_repo and shared.main_checkout(main_repo) is None and
                  shared.main_checkout(plain) is None)
            check("worktree: without git, the .git file finds it too",
                  shared._common_dir_from_file(wt) == main_repo / ".git" and shared._common_dir_from_file(main_repo) is None)
            # SDK
            check("worktree SDK: nothing anywhere -> the main checkout's sdk/ (where setup fetches it)",
                  TC.sdk_dir({}, wt) == main_repo / "sdk" and TC.default_sdk_dir(wt) == main_repo / "sdk" and
                  TC.sdk_dir({}, main_repo) == main_repo / "sdk")
            own = _mk_sdk(wt / "sdk")
            check("worktree SDK: its own sdk/ is used when the main checkout has none", TC.sdk_dir({}, wt) == own)
            main_sdk = _mk_sdk(main_repo / "sdk")
            out_sdk, err_sdk = io.StringIO(), io.StringIO()
            with contextlib.redirect_stdout(out_sdk), contextlib.redirect_stderr(err_sdk):
                got = TC.sdk_dir({}, wt)
                TC.sdk_dir({}, wt)
            check("worktree SDK: the main checkout's wins, one line on stderr (once), nothing on stdout",
                  got == main_sdk and out_sdk.getvalue() == "" and
                  err_sdk.getvalue().count("sdk: using the main checkout's (") == 1 and
                  err_sdk.getvalue().count("\n") == 1)
            check("worktree SDK: AC79_SDK beats the main checkout's", TC.sdk_dir({"AC79_SDK": str(own)}, wt) == own)
            # venv
            bin_dir = "Scripts" if os.name == "nt" else "bin"
            exe = "python.exe" if os.name == "nt" else "python"
            check("worktree venv: none anywhere -> the main checkout's (made there)",
                  deps_venv := (deps.venv_dir(wt) == main_repo / "tools" / "builder" / "venv"))
            (wt / "tools" / "builder" / "venv" / bin_dir).mkdir(parents=True)
            (wt / "tools" / "builder" / "venv" / bin_dir / exe).write_text("")
            check("worktree venv: its own is used when the main checkout has none",
                  deps.venv_dir(wt) == wt / "tools" / "builder" / "venv")
            (main_repo / "tools" / "builder" / "venv" / bin_dir).mkdir(parents=True)
            (main_repo / "tools" / "builder" / "venv" / bin_dir / exe).write_text("")
            check("worktree venv: the main checkout's wins", deps.venv_dir(wt) == main_repo / "tools" / "builder" / "venv" and
                  deps.venv_python(deps.venv_dir(wt)) == main_repo / "tools" / "builder" / "venv" / bin_dir / exe)
            os.environ["BUILDER_VENV"] = str(d / "elsewhere")
            check("worktree venv: BUILDER_VENV beats the main checkout's", deps.venv_dir(wt) == d / "elsewhere")
            os.environ.pop("BUILDER_VENV")
            # emulator
            check("worktree emulator: nothing anywhere -> the main checkout's emulator/fm1-emulator",
                  emu.clone_dir(wt) == main_repo / "emulator" / "fm1-emulator")
            (wt / "emulator" / "fm1-emulator" / ".git").mkdir(parents=True)
            check("worktree emulator: its own clone is used when the main checkout has none",
                  emu.clone_dir(wt) == wt / "emulator" / "fm1-emulator")
            (main_repo / "emulator" / "fm1-emulator" / ".git").mkdir(parents=True)
            check("worktree emulator: the main checkout's wins; the flash state stays in the worktree",
                  emu.clone_dir(wt) == main_repo / "emulator" / "fm1-emulator" and emu.STATE == ROOT / "emulator" / "state")
            # the hiphop sample cache
            (main_repo / "build" / "hiphop-src").mkdir(parents=True)
            check("worktree: the sample download cache is the main checkout's",
                  shared.resolve("build/hiphop-src", Path.is_dir, wt)[0] == main_repo / "build" / "hiphop-src")

            # the profiles: the main checkout's config/ (list, load, save), unless OPTIMIST_LOCAL_PROFILES=1
            check("worktree profiles: the main checkout has no config/profiles -> the worktree's own",
                  shared.config_dir(wt, {}) == (wt / "config", False))
            (main_repo / "config" / "profiles").mkdir(parents=True)
            (main_repo / "config" / "profiles" / "user-default.config").write_text("# name: Main\nICONS=0\n")
            (wt / "config" / "profiles").mkdir(parents=True)
            (wt / "config" / "profiles" / "user-default.config").write_text("# name: Wt\nICONS=1\n")
            check("worktree profiles: config/ is the main checkout's", shared.config_dir(wt, {}) == (main_repo / "config", True))
            check("worktree profiles: the main checkout itself is not 'shared'",
                  shared.config_dir(main_repo, {}) == (main_repo / "config", False))
            check("worktree profiles: OPTIMIST_LOCAL_PROFILES=1 keeps the worktree's own",
                  shared.config_dir(wt, {shared.LOCAL_PROFILES_ENV: "1"}) == (wt / "config", False) and
                  shared.config_dir(wt, {shared.LOCAL_PROFILES_ENV: "0"}) == (main_repo / "config", True))
            # configure.py in a copy of the tools inside the worktree: list, load, save go to the main checkout
            shutil.copytree(ROOT / "tools" / "builder", wt / "tools" / "builder",
                            ignore=shutil.ignore_patterns("venv", "__pycache__"), dirs_exist_ok=True)
            shutil.copy(ROOT / "tools" / "shared.py", wt / "tools" / "shared.py")
            probe = ("import sys; sys.path.insert(0, 'tools/builder'); import configure as C; "
                     "cfg, nm = C.load_profile('user-default'); p = C.save_my_profile(cfg, 'mine'); "
                     "print(nm, C.profile_names(), C.my_profile_names(), p)")
            envs = {k: v for k, v in os.environ.items() if k not in (shared.NOTED_ENV, shared.LOCAL_PROFILES_ENV)}
            run = subprocess.run([sys.executable, "-c", probe], cwd=wt, env=envs, capture_output=True, text=True)
            check("worktree profiles: configure loads, lists and SAVES in the main checkout, saying so on stderr",
                  run.returncode == 0 and run.stdout.startswith("Main ") and "['mine']" in run.stdout and
                  (main_repo / "config" / "my-profiles" / "mine.config").is_file() and
                  not (wt / "config" / "my-profiles").exists() and "profiles: using the main checkout's" in run.stderr)
            run = subprocess.run([sys.executable, "-c", probe], cwd=wt, capture_output=True, text=True,
                                 env={**envs, shared.LOCAL_PROFILES_ENV: "1"})
            check("worktree profiles: OPTIMIST_LOCAL_PROFILES=1 loads and saves the worktree's own, silently",
                  run.returncode == 0 and run.stdout.startswith("Wt ") and
                  (wt / "config" / "my-profiles" / "mine.config").is_file() and run.stderr == "")

            # an install into the main checkout's folder, once, under the lock, from several worktrees at once
            sdk_new = d / "main" / "sdk2"
            blobs = {rel: ("data " + rel).encode() for rel in TC.SDK_SHA256}
            sha_saved, dl_saved = dict(TC.SDK_SHA256), deps.download
            calls = []

            def fake_download(url, dest):
                calls.append(url)
                time.sleep(0.05)
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(blobs[url.rsplit("/tools/", 1)[1]])

            TC.SDK_SHA256.update({rel: hashlib.sha256(b).hexdigest() for rel, b in blobs.items()})
            deps.download = fake_download
            try:
                errs = []

                def worker():
                    try:
                        quiet(deps.fetch_sdk, sdk_new)
                    except Exception as e:      # noqa: BLE001  (reported by the check below)
                        errs.append(e)
                threads = [threading.Thread(target=worker) for _ in range(5)]
                for t in threads:
                    t.start()
                for t in threads:
                    t.join()
                sys.stdout, sys.stderr = REAL_STREAMS   # (five threads each redirected them: the last restore left a stale one,
                                                        # and everything after, the final verdict too, went nowhere)
                check("install: five at once fetch each SDK file once, into the main checkout, and free the lock",
                      not errs and len(calls) == len(blobs) and not TC.sdk_missing(sdk_new) and
                      not (sdk_new / shared.LOCK_NAME).exists() and not (wt / "sdk2").exists())
                n = len(calls)
                quiet(deps.fetch_sdk, sdk_new)
                check("install: a complete, right SDK is not fetched again", len(calls) == n)
                (sdk_new / "cpu" / "wl82" / "tools" / "uboot.boot").write_bytes(b"stale")
                quiet(deps.fetch_sdk, sdk_new)
                check("install: a file that is not the pinned version's is fetched again",
                      len(calls) == n + 1 and not TC.sdk_missing(sdk_new))
            finally:
                deps.download = dl_saved
                TC.SDK_SHA256.clear()
                TC.SDK_SHA256.update(sha_saved)

            # the lock
            lk_dir = d / "locked"
            entered = threading.Event()
            release = threading.Event()

            def holder():
                with shared.install_lock(lk_dir, "test"):
                    entered.set()
                    release.wait(10)
            th = threading.Thread(target=holder)
            th.start()
            entered.wait(10)
            try:
                with contextlib.redirect_stderr(io.StringIO()):
                    shared.install_lock(lk_dir, "test", timeout=0.3, poll=0.05).__enter__()
                blocked = False
            except TimeoutError:
                blocked = True
            release.set()
            th.join()
            check("lock: a second install into the same folder waits (and gives up with a message)",
                  blocked and not (lk_dir / shared.LOCK_NAME).exists())
            with shared.install_lock(lk_dir, "test"):
                pass
            check("lock: free again after the first one is done", not (lk_dir / shared.LOCK_NAME).exists())
            dead = subprocess.Popen([sys.executable, "-c", "pass"])
            dead.wait()
            lock_file = lk_dir / shared.LOCK_NAME
            lock_file.write_text(f"{dead.pid} crashed\n")
            old = time.time() - (shared.STALE_SECONDS + 60)
            os.utime(lock_file, (old, old))
            with shared.install_lock(lk_dir, "test", timeout=5, poll=0.05):
                took = True
            check("lock: one a crashed install left (older than an hour) is taken over",
                  took and not lock_file.exists())
        finally:
            for k, v in _env0.items():
                if v is None:
                    os.environ.pop(k, None)
                else:
                    os.environ[k] = v

# flash: the arguments, the package choice, the refusals, the confirmation (fm1_install mocked: no device is touched)
import flash as FL
a = ap.parse_args(["flash", "x.fwsc", "--port", "FM-1", "--yes"])
check("flash: PACKAGE, --port, --yes parse", a.package == "x.fwsc" and a.port == "FM-1" and a.yes and
      a.fn is optimist.cmd_flash)
a = ap.parse_args(["flash"])
check("flash: no package given: the last build, asks first", a.package is None and not a.yes)


class FakeTool:
    """stands for fm1_install.py: records every call, answers --info and the install"""
    def __init__(self, info=(0, "Optimist_705  [running]  port: FM-1"), write=(0, "done")):
        self.calls, self.info, self.write = [], info, write

    def __call__(self, args, capture=True):
        self.calls.append(list(args))
        return self.info if "--info" in args else self.write

    def wrote(self):
        return [c for c in self.calls if "--info" not in c]


def flash_run(argv, tool, answer="n", packages=None, product="FM-1_705"):
    """optimist flash with fm1_install replaced; input() answers; -> (rc, output)"""
    import builtins
    save = (FL.run_tool, FL.FI.load_package, FL.last_package, builtins.input)
    prompts = []
    FL.run_tool = tool
    FL.last_package = lambda build=None: packages

    def load(path, force):
        assert force is False, "flash must never pass --force"
        if product is None:
            raise FL.FI.InstallError("badpkg", f"{path}: no Felucca update loader in this package")
        return product, b""
    FL.FI.load_package = load
    builtins.input = lambda p="": prompts.append(p) or answer
    try:
        rc, out = quiet(optimist.main, ["flash", *argv])
    finally:
        FL.run_tool, FL.FI.load_package, FL.last_package, builtins.input = save
    return rc, out, prompts


with tempfile.TemporaryDirectory() as td:
    bd = Path(td)
    old, new = bd / "optimist-0.9-a.fwsc", bd / "optimist-1.0-b.fwsc"
    for f, t in ((old, 1000), (new, 2000), (bd / "felucca.fwsc", 3000)):
        f.write_bytes(b"x")
        os.utime(f, (t, t))
    check("flash: the default package is the newest build/optimist-*.fwsc (never the internal felucca.fwsc)",
          FL.last_package(bd) == new)
    check("flash: no optimist-*.fwsc in build/: none", FL.last_package(bd / "nope") is None)
    t = FakeTool()
    rc, out, pr = flash_run([], t, packages=None)
    check("flash: no build: refused, nothing asked, no device touched", rc == 1 and "no build to flash" in out and
          not t.calls and not pr)
    t = FakeTool()
    rc, out, pr = flash_run([str(bd / "missing.fwsc")], t)
    check("flash: a package that does not exist is refused (exit 2), no device touched", rc == 2 and not t.calls)
    t = FakeTool()
    rc, out, pr = flash_run([str(new)], t, product=None)
    check("flash: a stock or foreign package (no Felucca loader) is refused before the device is read",
          rc == 2 and "no Felucca update loader" in out and not t.calls)
    t = FakeTool()
    rc, out, pr = flash_run([str(new)], t, product="FM-1_906")
    check("flash: another firmware's identity (SLOOP FM-1_906) is refused, no device touched",
          rc == 2 and "not an Optimist build" in out and not t.calls)
    t = FakeTool(info=(3, "error: FM-1 not found"))
    rc, out, pr = flash_run([str(new)], t, answer="y")
    check("flash: no FM-1 port found: refused (exit 3), nothing written, nothing asked",
          rc == 3 and "no FM-1 found" in out and not t.wrote() and not pr)
    t = FakeTool()
    rc, out, pr = flash_run([], t, answer="n", packages=new)
    check("flash: shows the running identity, the package and the recovery, then asks",
          "Optimist_705  [running]" in out and "optimist-1.0-b.fwsc" in out and "FM-1_705" in out and
          "Rescue, going back" in out and len(pr) == 1 and "Flash" in pr[0])
    check("flash: answer n: cancelled, exit 1, nothing written", rc == 1 and "cancelled" in out and not t.wrote())
    t = FakeTool()
    rc, out, pr = flash_run([], t, answer="", packages=new)
    check("flash: an empty answer is a no", rc == 1 and not t.wrote())
    t = FakeTool()
    rc, out, pr = flash_run([], t, answer="y", packages=new)
    check("flash: answer y: the last build is written with --yes (we asked), never --force",
          rc == 0 and t.wrote() == [[str(new), "--yes"]] and not any("--force" in c for c in t.calls))
    t = FakeTool()
    rc, out, pr = flash_run([str(old), "--yes", "--port", "FM-1"], t, packages=new)
    check("flash: an explicit package and --yes: no question, --port passed to the info and the write",
          rc == 0 and not pr and t.calls == [["--info", "--port", "FM-1"], [str(old), "--yes", "--port", "FM-1"]])
    t = FakeTool(write=(4, "error: connection lost"))
    rc, out, pr = flash_run([], t, answer="y", packages=new)
    check("flash: a failed write returns fm1_install's exit code", rc == 4)
    save_mp, save_lp = FL.midi_python, FL.FI.load_package
    FL.midi_python, FL.FI.load_package = (lambda: None), (lambda path, force: ("FM-1_705", b""))
    try:
        rc, out = quiet(optimist.main, ["flash", str(new), "--yes"])
    finally:
        FL.midi_python, FL.FI.load_package = save_mp, save_lp
    check("flash: no mido / python-rtmidi: says how to get them (make setup), exit 1",
          rc == 1 and "make setup" in out)
import re
_req = {re.split("[<>=]", ln)[0] for ln in (ROOT / "tools" / "requirements-flash.txt").read_text().splitlines()
        if ln and ln[0] != "#"}
check("flash: requirements-flash.txt names mido and python-rtmidi (deps.install_flash puts them in the venv)",
      _req == {"mido", "python-rtmidi"})

print("optimist CLI: " + ("all passed" if not fails else f"{fails} FAILED"))
sys.exit(1 if fails else 0)
