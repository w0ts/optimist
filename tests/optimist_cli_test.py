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
import emu
import optimist
import toolchain as TC

fails = 0


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
optimist.PASSTHROUGH.update(emu=lambda ns: seen.append(ns.rest) or 0, config=lambda ns: seen.append(ns.rest) or 0)
optimist.main(["emu", "optimist", "--cpu", "96", "--bg"])
optimist.main(["config", "--profile", "x", "--list"])
optimist.PASSTHROUGH.update(saved)
check("emu and config: their arguments go to them untouched",
      seen == [["optimist", "--cpu", "96", "--bg"], ["--profile", "x", "--list"]])
rc, _ = quiet(ap.parse_args, ["build", "--profile", "a", "--config", "b"])
check("build: --profile and --config together is a usage error", rc == 2)
cfg, name = optimist.load_config(ap.parse_args(["build"]))
check(f"build without a configuration builds {optimist.DEFAULT_PROFILE}", name and cfg)
rc, out = quiet(optimist.main, ["build", "--config", str(ROOT / "no-such.config")])
check("a missing .config: exit 2 and its name", rc == 2 and "no-such.config" in out)
rc, out = quiet(optimist.main, ["config", "--profile", "drum-machine", "--budget"])
check("config passes through to configure.py (--budget)", rc == 0 and "estimate" in out)
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


def git_in(repo, *args):
    subprocess.run(["git", "-C", str(repo), "-c", "user.name=t", "-c", "user.email=t@t", *args],
                   check=True, capture_output=True)


if emu.shutil.which("git"):
    with tempfile.TemporaryDirectory() as d:
        up, saved_clone = Path(d) / "up", emu.CLONE
        up.mkdir()
        git_in(up, "init", "-q", "-b", "feat/upstream-merge")
        git_in(up, "commit", "-q", "--allow-empty", "-m", "one")
        emu.CLONE = Path(d) / "emulator" / "fm1-emulator"
        env = {"EMU_REPO": str(up)}
        rc_clone, _ = quiet(emu.ensure_clone, False, env)
        rc_same, _ = quiet(emu.ensure_clone, False, env)
        git_in(up, "commit", "-q", "--allow-empty", "-m", "two")
        rc_off, _ = quiet(emu.ensure_clone, False, dict(env, EMU_OFFLINE="1"))
        rc_moved, out_moved = quiet(emu.ensure_clone, False, env)
        rc_gone, out_gone = quiet(emu.ensure_clone, False, {"EMU_REPO": str(Path(d) / "nowhere")})
        emu.CLONE = saved_clone
    check("emu: clone, then every run fetches; rebuild only when the branch moved; EMU_OFFLINE; offline goes on",
          rc_clone is True and rc_same is False and rc_off is False and rc_moved is True and
          "(1): updating" in out_moved and rc_gone is False and "offline?" in out_gone)

print("optimist CLI: " + ("all passed" if not fails else f"{fails} FAILED"))
sys.exit(1 if fails else 0)
