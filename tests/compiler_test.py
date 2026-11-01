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


def tree_links(top):
    """-> [(link, its target resolved)] of every link under top (not followed)"""
    import os
    out = []
    for d, dirs, files in os.walk(top):
        for n in dirs + files:
            p = Path(d) / n
            if p.is_symlink():
                out.append((p, Path(os.path.realpath(p))))
    return out


def container_shim_checks():
    """the measurement shim where fm1-base's build runs the toolchain in a container (macOS): only the shim is mounted
    there (/opt/jieli), so nothing in it may point at the host's toolchain (2026-10-31: "stat /opt/jieli/pi32v2/bin/clang:
    no such file or directory"), and its ld wrapper runs the container's python3 on its own copy of fm1base_ld.py"""
    import os
    import subprocess
    import threading
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        real = fake_dropin(td / "real")
        (real / "common" / "lib").mkdir(parents=True)
        (real / "common" / "bin").mkdir()
        jl = real / "common" / "lib" / "jl.py"     # (as the drop-in's jlwrap.py: its root three levels above argv[0])
        jl.write_text("#!/usr/bin/env python3\nimport os, sys\n"
                      "print(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(sys.argv[0])))), "
                      "os.path.basename(sys.argv[0]), *sys.argv[1:])\n")
        jl.chmod(0o755)
        (real / "common" / "bin" / "clang").symlink_to("../lib/jl.py")
        (real / "pi32v2" / "bin" / "ld").symlink_to("../../common/lib/jl.py")
        (real / "pi32v2" / "lib").mkdir()
        (real / "pi32v2" / "lib" / "libc.a").write_text("a\n")
        outside = td / "outside.txt"
        outside.write_text("o\n")
        (real / "pi32v2" / "lib" / "abs.txt").symlink_to(outside)     # (an absolute link: its file taken in)
        keep_env, keep_root, keep_ld = fm1base.toolchain_env, fm1base.ROOT, fm1base.LD_PY
        fm1base.toolchain_env = lambda compiler="jieli": {"JIELI_TOOLCHAIN": str(real)}
        fm1base.ROOT = td / "opt"
        probes(native=False)
        errs, shims = [], []

        def make():
            try:
                shims.append(fm1base.measure_toolchain("llvm"))
            except Exception as e:      # (any error is the failure this checks)
                errs.append(e)
        try:
            ts = [threading.Thread(target=make) for _ in range(16)]
            [t.start() for t in ts]
            [t.join() for t in ts]
            shim = td / "opt" / "build" / "measure-toolchain-llvm-docker"
            check("container shim: its own name (-docker), 16 builds at once, no error",
                  not errs and set(shims) == {shim} and not list((td / "opt" / "build").glob("*.[0-9]*")))
            links = [(p, t) for p, t in tree_links(shim) if p != shim / "real"]
            rshim = Path(os.path.realpath(shim))
            check("container shim: every link but real/ stays inside the shim and resolves (no host path in a mount)",
                  links and all(t.exists() and (t == rshim or rshim in t.parents) for _, t in links))
            check("container shim: files hard-linked, the absolute link's file taken in",
                  os.path.samefile(shim / "pi32v2" / "bin" / "clang", real / "pi32v2" / "bin" / "clang")
                  and not (shim / "pi32v2" / "lib" / "abs.txt").is_symlink()
                  and (shim / "pi32v2" / "lib" / "abs.txt").read_text() == "o\n"
                  and os.readlink(shim / "common" / "bin" / "clang") == "../lib/jl.py")
            wrapper = (shim / "pi32v2" / "bin" / "ld").read_text()
            check("container shim: ld the wrapper, no host path in it (the container's python3, the shim's own files)",
                  str(td) not in wrapper and sys.executable not in wrapper and "exec python3" in wrapper
                  and (shim / "measure" / "fm1base_ld.py").read_bytes() == fm1base.LD_PY.read_bytes()
                  and os.readlink(shim / "measure" / "bin" / "ld") == "../../common/lib/jl.py")
            env = {k: v for k, v in os.environ.items() if k != "FM1B_REAL_LD"}
            r = subprocess.run([str(shim / "pi32v2" / "bin" / "ld"), "-o", "a.elf"], capture_output=True, text=True,
                               env=env, cwd=td)
            check("container shim: the wrapper runs the real ld as ld, with the shim as its toolchain (jlwrap.py's root)",
                  r.returncode == 0 and r.stdout.split() == [str(shim), "ld", "-o", "a.elf"])
            again = fm1base.measure_toolchain("llvm")
            mark = shim / "pi32v2" / "lib" / "mark"
            mark.write_text("kept\n")
            check("container shim: made once (the next build keeps it)",
                  again == shim and fm1base.measure_toolchain("llvm") == shim and mark.is_file())
            other = td / "fm1base_ld.py"
            other.write_text("# another fm1base_ld.py\n")
            fm1base.LD_PY = other
            fm1base.measure_toolchain("llvm")
            check("container shim: made again when fm1base_ld.py changed (its copy is the checkout's)",
                  not mark.exists() and (shim / "measure" / "fm1base_ld.py").read_text() == other.read_text())
            fm1base.LD_PY = keep_ld
            cfg = td / "j.config"
            c = C.defaults()
            c["COMPILER"] = 0
            cfg.write_text(C.dump(c, "j"))
            e = fm1base.env_for(cfg, measure=True)
            check("container measurement build, JieLi too: the toolchain image (python3 for the wrapper), the shim",
                  e["JIELI_DOCKER_IMAGE"] == TC.image_name() and e["JIELI_TOOLCHAIN"].endswith("measure-toolchain-docker"))
        finally:
            fm1base.toolchain_env, fm1base.ROOT, fm1base.LD_PY = keep_env, keep_root, keep_ld
    import importlib.util
    spec = importlib.util.spec_from_file_location("fm1b_build", fm1base.BUILD_PY)
    fb = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fb)
    n = fb.builder_name("/w", "/tc/a", "img:1")
    check("fm1-base's toolchain container: its own for each toolchain and image (a measurement build never runs in the "
          "real build's container, nor the other way round)",
          n == fb.builder_name("/w", "/tc/a", "img:1") and len({n, fb.builder_name("/w", "/tc/b", "img:1"),
                                                                fb.builder_name("/w", "/tc/a", "img:2"),
                                                                fb.builder_name("/v", "/tc/a", "img:1")}) == 4)


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
        shim = td / "shim"
        shim.mkdir()
        (shim / "real").symlink_to(d)
        check("the release log's compiler line: the .config's compiler and the real toolchain (a shim's too)",
              fm1base.compiler_line(td / "a.config", {"JIELI_TOOLCHAIN": str(d)}) == f"compiler pi32v2-llvm ({d.resolve()})"
              and fm1base.compiler_line(td / "b.config", {"JIELI_TOOLCHAIN": str(shim)}) == f"compiler JieLi ({d.resolve()})")
        import subprocess  # noqa: E402
        r = subprocess.run([sys.executable, "-c", fm1base.ANNOUNCE, "compiler X (y)", sys.executable, "-c",
                            "import sys; print('build'); sys.exit(3)"], capture_output=True, text=True)
        check("... printed first, then the build runs in its place, its exit status kept",
              r.stdout.splitlines() == ["compiler X (y)", "build"] and r.returncode == 3)
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
        probes(native=True)
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
    container_shim_checks()

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
