#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The compiler: pi32v2-llvm's drop-in, run natively (tools/toolchain.py host_key, llvm_asset, resolve; tools/fm1base.py
toolchain_env, compiler_line, measure_toolchain). The drop-in for this host is found or the error says how to get it; a
drop-in that does not run here is refused; the release build gets its directory, run natively; the builder's COMPILER
item is retired and an old .config that names it still loads. No toolchain: fake directories and probes.
  python3 tests/compiler_test.py"""
import stat
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "builder"))
import configure as C  # noqa: E402
import fm1base  # noqa: E402
import registry as R  # noqa: E402
import toolchain as TC  # noqa: E402

fails = 0


def check(what, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + what)
    fails += not ok


def fake_dropin(d, runs=True):
    """a drop-in whose clang wrapper starts (exit 0) or does not (exit 1, as a Linux archive's on a Mac)"""
    clang = d / "pi32v2" / "bin" / "clang"
    clang.parent.mkdir(parents=True)
    clang.write_text(f"#!/bin/sh\nexit {0 if runs else 1}\n")
    clang.chmod(clang.stat().st_mode | stat.S_IXUSR)
    return d


def refused(fn, *args):
    try:
        fn(*args)
    except TC.ToolchainError as e:
        return str(e)
    return None


# ---- the drop-ins there are, per host
check("drop-ins: Linux x86-64 and macOS arm64, an archive and a SHA-256 each",
      set(TC.LLVM_ASSETS) == {("linux", "x86_64"), ("darwin", "arm64")}
      and all(name.endswith(".tar.xz") and len(sha) == 64 for name, sha, _mb in TC.LLVM_ASSETS.values()))
check("drop-ins: one archive per host, named for it (the macOS one says so)",
      len({name for name, _, _ in TC.LLVM_ASSETS.values()}) == 2
      and "macos-arm64" in TC.LLVM_ASSETS[("darwin", "arm64")][0] and "macos" not in TC.LLVM_ASSETS[("linux", "x86_64")][0])
check("llvm_asset: none for a host with no drop-in (Windows, Linux arm64)",
      TC.llvm_asset(("windows", "x86_64")) is None and TC.llvm_asset(("linux", "arm64")) is None
      and TC.llvm_asset(("darwin", "arm64")) == TC.LLVM_ASSETS[("darwin", "arm64")])
real_machine = TC.platform.machine
try:
    for raw, want in (("AMD64", "x86_64"), ("x86_64", "x86_64"), ("aarch64", "arm64"), ("arm64", "arm64")):
        TC.platform.machine = lambda raw=raw: raw
        check(f"host_key: machine {raw} is {want}", TC.host_key()[1] == want)
finally:
    TC.platform.machine = real_machine

# ---- resolve: native, or why not
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    e = refused(TC.resolve, {"PI32V2_LLVM_TOOLCHAIN": str(td / "none")})
    check("no drop-in: refused, says setup", e is not None and "setup" in e and str(td / "none") in e)
    d = fake_dropin(td / "dropin")
    b = TC.resolve({"PI32V2_LLVM_TOOLCHAIN": str(d)})
    check("a drop-in that runs: the native backend, from its directory", Path(b.path) == d.resolve())
    check("... it runs a tool through sh with core files off, the tool's own path, the arguments after",
          b.command("pi32v2/bin/clang", ["-c", "a.c"]) == ["sh", "-c", TC.NO_CORE, str(d.resolve() / "pi32v2/bin/clang"),
                                                          "-c", "a.c"])
    check("... and describes itself (compiler, version, native)", "pi32v2-llvm" in b.describe() and "native" in b.describe())
    bad = fake_dropin(td / "other-host", runs=False)
    e = refused(TC.resolve, {"PI32V2_LLVM_TOOLCHAIN": str(bad)})
    check("a drop-in that does not run here (another system, or python3 older than 3.11): refused, says why",
          e is not None and "does not run on this host" in e and "3.11" in e)
    real_key = TC.host_key
    try:
        TC.host_key = lambda: ("windows", "x86_64")
        e = refused(TC.resolve, {"PI32V2_LLVM_TOOLCHAIN": str(td / "none")})
        check("a host with no drop-in: says so, and how to build one (PI32V2_LLVM_TOOLCHAIN)",
              e is not None and "windows" in e and "PI32V2_LLVM_TOOLCHAIN" in e)
    finally:
        TC.host_key = real_key
    check("the old ways are gone: no Docker, image or WSL backend, no JieLi",
          not any(hasattr(TC, n) for n in ("docker_running", "image_present", "wsl_toolchain", "COMPILERS", "compiler_of",
                                           "IMAGE_DEFAULT", "TOOLCHAIN_URL", "JIELI_HOME", "BACKENDS", "retry_tool",
                                           "sync_view")))

    # ---- the release build (fm1-base): its toolchain is the drop-in, run natively
    old = TC.llvm_dir
    TC.llvm_dir = lambda env=None, root=None: d
    try:
        env = fm1base.toolchain_env()
        check("release build: JIELI_TOOLCHAIN (fm1-base's name for it) is the drop-in, Docker off, the SDK's files",
              env["JIELI_TOOLCHAIN"] == str(d) and env["JIELI_DOCKER"] == "0" and env["AC79_SDK"] == str(TC.sdk_dir())
              and "JIELI_DOCKER_IMAGE" not in env)
        TC.llvm_dir = lambda env=None, root=None: td / "none"
        try:
            fm1base.toolchain_env()
            check("release build: no drop-in refused", False)
        except fm1base.FM1BaseError as err:
            check("release build: no drop-in refused, says setup", "setup" in str(err))
    finally:
        TC.llvm_dir = old
    shim = td / "shim"
    shim.mkdir()
    (shim / "real").symlink_to(d)
    check("the release log's compiler line: the compiler and the real toolchain (a measurement shim's too)",
          fm1base.compiler_line({"JIELI_TOOLCHAIN": str(d)}) == f"compiler pi32v2-llvm ({d.resolve()})"
          and fm1base.compiler_line({"JIELI_TOOLCHAIN": str(shim)}) == f"compiler pi32v2-llvm ({d.resolve()})")
    r = subprocess.run([sys.executable, "-c", fm1base.ANNOUNCE, "compiler X (y)", sys.executable, "-c",
                        "import sys; print('build'); sys.exit(3)"], capture_output=True, text=True)
    check("... printed first, then the build runs in its place, its exit status kept",
          r.stdout.splitlines() == ["compiler X (y)", "build"] and r.returncode == 3)

# ---- the measurement shim, made by many builds at once (costs-all: 288 jobs as threads, 2026-10-29 FileNotFoundError)
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    real = fake_dropin(td / "real")
    (real / "pi32v2" / "bin" / "ld").write_text("ld\n")
    (real / "common").mkdir()
    keep_env, keep_root = fm1base.toolchain_env, fm1base.ROOT
    fm1base.toolchain_env = lambda: {"JIELI_TOOLCHAIN": str(real)}
    fm1base.ROOT = td
    errs, shims = [], []

    def make():
        try:
            shims.append(fm1base.measure_toolchain())
        except Exception as ex:      # (any error is the failure this checks)
            errs.append(ex)
    try:
        ts = [threading.Thread(target=make) for _ in range(32)]
        [t.start() for t in ts]
        [t.join() for t in ts]
    finally:
        fm1base.toolchain_env, fm1base.ROOT = keep_env, keep_root
    shim = td / "build" / "measure-toolchain"
    check("measurement shim: 32 builds at once, no error, one shim with its wrapper and links",
          not errs and len(shims) == 32 and (shim / "pi32v2" / "bin" / "ld").is_file()
          and (shim / "common").resolve() == (real / "common").resolve()
          and (shim / "real").resolve() == real.resolve() and not list((td / "build").glob("*.[0-9]*")))

# ---- the builder's COMPILER item is retired
check("COMPILER: retired (bit 442 stays unused), no longer an item", "COMPILER" in R.RETIRED and "COMPILER" not in R.ITEMS
      and all(it.bit != 442 for it in R.ITEMS.values() if getattr(it, "bit", None) is not None))
text = C.dump(C.defaults(), "a") + "COMPILER=0\n"
warned = []
cfg, _ = C.parse(text, warn=warned.append)
check("an old .config that names COMPILER loads, the setting ignored and said so",
      "COMPILER" not in cfg and any("COMPILER" in w and "ignored" in w for w in warned))
import web_api  # noqa: E402
web_api.init(C.load_costs(), C.NEEDS_AUTO)
check("web builder: no COMPILER item, nothing held fixed",
      not web_api.WEB_FIXED and all(i["key"] != "COMPILER" for i in web_api.registry_view()["registry"]["items"]))
print("compiler: FAILED" if fails else "compiler: all passed")
sys.exit(1 if fails else 0)
