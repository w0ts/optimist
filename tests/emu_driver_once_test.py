#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""tests/emu/run.py --jobs: the emulator checks' driver is built once, by the parent; its children only use it.
integration57: each child ran `cargo build` into build/emu-drv/ (session.driver()); one relinked the binary while
another started it -> FileNotFoundError, 65/2 and 105/1 where --jobs 1 gave 115/0. No emulator or cargo needed:
cargo and the checks are stand-ins. Exit 1 on a failure."""
import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "emu"))
import run as R                    # noqa: E402  (tests/emu/run.py: its imports put tests/emu on the path)
import session as S                # noqa: E402

fails = 0


def check(name, ok):
    global fails
    fails += not ok
    print(f"{name} {'ok' if ok else 'FAIL'}")


def no_cargo(*a, **k):
    raise AssertionError(f"cargo run in a child: {a[0] if a else k}")


with tempfile.TemporaryDirectory() as tmp:
    exe = Path(tmp) / "emu-drv"
    exe.write_text("")
    real_run = subprocess.run

    # 1. a child (EMU_CHECK_DRIVER set) takes the parent's driver as it is and never runs cargo
    S.subprocess.run, S._DRIVER = no_cargo, None
    os.environ[S.DRIVER_ENV] = str(exe)
    try:
        got = S.driver()
        check("a child uses the parent's driver, no cargo", got == exe)
    except AssertionError as x:
        check(f"a child uses the parent's driver, no cargo ({x})", False)

    # 2. the parent's driver missing: the child stops on it (EmuError), still no cargo
    S._DRIVER = None
    os.environ[S.DRIVER_ENV] = str(exe) + "-missing"
    try:
        S.driver()
        check("a missing driver stops the child", False)
    except S.EmuError:
        check("a missing driver stops the child, no cargo", True)
    except AssertionError as x:
        check(f"a missing driver stops the child, no cargo ({x})", False)
    del os.environ[S.DRIVER_ENV]
    S.subprocess.run, S._DRIVER = real_run, None

    # 3. run_parallel: the driver built once, before any child; every child is handed its path
    calls, envs = [], {}

    def fake_driver():
        calls.append(len(envs))                    # (children started before the build: none)
        return exe

    def fake_child(n, fw, a):
        envs[n] = a.child_env.get(S.DRIVER_ENV)
        return f"PASS  {n}: stand-in\n", 1, 0, 0.0

    R.session.driver, R.run_child = fake_driver, fake_child
    names = list(R.CHECKS)
    a = argparse.Namespace(jobs=len(names), out=tmp, mhz=96)
    done = R.run_parallel(names, Path(tmp) / "fw.fwsc", a)
    check("the driver built once, before the children", calls == [0])
    check("every check ran, each handed the driver's path",
          set(done) == set(names) and set(envs) == set(names) and set(envs.values()) == {str(exe)})

    # 4. the parent's build failed: the children get a path that is not there (each stops), none builds
    def failing_driver():
        raise S.EmuError("cargo failed (stand-in)")

    envs.clear()
    R.session.driver = failing_driver
    R.run_parallel(names, Path(tmp) / "fw.fwsc", a)
    check("the build failed: no child left to build it",
          len(envs) == len(names) and all(v and not Path(v).exists() for v in envs.values()))

print("emu driver once: " + ("all checks ok" if not fails else f"{fails} FAILED"))
sys.exit(1 if fails else 0)
