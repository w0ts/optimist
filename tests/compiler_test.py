#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The builder's COMPILER item (tools/toolchain.py compiler_of, resolve_llvm; tools/fm1base.py toolchain_env,
compiler_of): the configuration picks pi32v2-llvm (default) or JieLi's clang; pi32v2-llvm's drop-in runs natively on
Linux x86-64 and, elsewhere, mounted in the toolchain image (its python3, not plain debian); missing, it says how to get
it; the release build gets its directory as JIELI_TOOLCHAIN. No toolchain, no Docker: fake directories and probes.
  python3 tests/compiler_test.py"""
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "builder"))
import configure as C  # noqa: E402
import fm1base  # noqa: E402
import toolchain as TC  # noqa: E402

fails = 0


def check(what, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + what)
    fails += not ok


def fake_dropin(d):
    (d / "pi32v2" / "bin").mkdir(parents=True)
    (d / "pi32v2" / "bin" / "clang").write_text("#!/usr/bin/env python3\n")
    return d


def probes(native, docker=True, image=True):
    TC.is_native_host = lambda: native
    TC.docker_running = lambda: docker
    TC.image_present = lambda _name: image


keep = (TC.is_native_host, TC.docker_running, TC.image_present)
try:
    check("compiler_of: FELUCCA_COMPILER 1 is pi32v2-llvm, else JieLi",
          TC.compiler_of({"FELUCCA_COMPILER": 1}) == "llvm" and TC.compiler_of({"FELUCCA_COMPILER": 0}) == "jieli"
          and TC.compiler_of({}) == "jieli")
    check("the builder's default is pi32v2-llvm", TC.compiler_of(C.flags(C.defaults())[0]) == "llvm")
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        env = {"PI32V2_LLVM_TOOLCHAIN": str(td / "none")}
        try:
            TC.resolve(env, "llvm")
            check("no drop-in: refused", False)
        except TC.ToolchainError as e:
            check("no drop-in: refused, says setup (or COMPILER JieLi)", "setup" in str(e) and "JieLi" in str(e))
        d = fake_dropin(td / "dropin")
        env = {"PI32V2_LLVM_TOOLCHAIN": str(d)}
        probes(native=True)
        b = TC.resolve(env, "llvm")
        check("Linux x86-64: natively, from its directory", b.kind == "native" and Path(b.path) == d.resolve())
        probes(native=False)
        b = TC.resolve(env, "llvm")
        check("elsewhere: mounted in the toolchain image (its python3), not plain debian",
              b.kind == "docker" and b.image == TC.image_name(env) and "/opt/jieli" in " ".join(b.command("x", [])))
        probes(native=False, image=False)
        try:
            TC.resolve(env, "llvm")
            check("no toolchain image: refused", False)
        except TC.ToolchainError as e:
            check("no toolchain image: refused, says setup", "setup" in str(e))
        try:
            TC.resolve(dict(env, JIELI_BACKEND="wsl"), "llvm")
            check("WSL: refused for now", False)
        except TC.ToolchainError as e:
            check("WSL: refused for now (JieLi there)", "WSL" in str(e))

        old = TC.llvm_dir
        TC.llvm_dir = lambda env=None: d
        try:
            probes(native=True)
            e1 = fm1base.toolchain_env("llvm")
            check("release build, Linux: JIELI_TOOLCHAIN is the drop-in, no image needed",
                  e1["JIELI_TOOLCHAIN"] == str(d) and "JIELI_DOCKER_IMAGE" not in e1)
            probes(native=False)
            e2 = fm1base.toolchain_env("llvm")
            check("release build, macOS: the toolchain image for fm1-base's container",
                  e2["JIELI_DOCKER_IMAGE"] == TC.image_name())
        finally:
            TC.llvm_dir = old
        cfg = C.defaults()
        (td / "a.config").write_text(C.dump(cfg, "a"))
        cfg["COMPILER"] = 0
        (td / "b.config").write_text(C.dump(cfg, "b"))
        check("fm1base.compiler_of: the .config's item (default pi32v2-llvm, COMPILER=0 JieLi)",
              fm1base.compiler_of(td / "a.config") == "llvm" and fm1base.compiler_of(td / "b.config") == "jieli")
    # the measurement shim, made by many builds at once (costs-all: 288 jobs as threads, 2026-10-29 FileNotFoundError)
    import threading  # noqa: E402
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        real = fake_dropin(td / "real")
        (real / "pi32v2" / "bin" / "ld").write_text("ld\n")
        (real / "common").mkdir()
        keep_env, keep_root = fm1base.toolchain_env, fm1base.ROOT
        fm1base.toolchain_env = lambda compiler="jieli": {"JIELI_TOOLCHAIN": str(real)}
        fm1base.ROOT = td
        errs, shims = [], []

        def make():
            try:
                shims.append(fm1base.measure_toolchain("llvm"))
            except Exception as e:      # (any error is the failure this checks)
                errs.append(e)
        try:
            ts = [threading.Thread(target=make) for _ in range(32)]
            [t.start() for t in ts]
            [t.join() for t in ts]
        finally:
            fm1base.toolchain_env, fm1base.ROOT = keep_env, keep_root
        shim = td / "build" / "measure-toolchain-llvm"
        check("measurement shim: 32 builds at once, no error, one shim with its wrapper and links",
              not errs and len(shims) == 32 and (shim / "pi32v2" / "bin" / "ld").is_file()
              and (shim / "common").resolve() == (real / "common").resolve()
              and (shim / "real").resolve() == real.resolve() and not list((td / "build").glob("*.[0-9]*")))

    import web_api  # noqa: E402
    web_api.init(C.load_costs(), C.NEEDS_AUTO)
    item = [i for i in web_api.registry_view()["registry"]["items"] if i["key"] == "COMPILER"][0]
    dumped = web_api.view({"COMPILER": 1}, "w")["dump"]
    check("web builder: COMPILER not listed, held at JieLi (its .config says COMPILER=0 for the cloud build)",
          item["hidden"] and "COMPILER=0" in dumped)
finally:
    TC.is_native_host, TC.docker_running, TC.image_present = keep
print("compiler: FAILED" if fails else "compiler: all passed")
sys.exit(1 if fails else 0)
