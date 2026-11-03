#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Where fetched tools live (tools/repo_root.py): build/<sub> of the MAIN checkout, never the user's home.
From the main checkout: its build/. From a linked worktree: the main checkout's build/, not the worktree's. From a plain
copy (no .git): the environment variable, else the nearest build/<sub> above, else its own. The pi32v2-llvm download
(tools/deps.py fetch_llvm) writes only inside the repository; llvm_dir finds the drop-in there.
  python3 tests/repo_root_test.py"""
import hashlib
import io
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import deps  # noqa: E402
import mflasher  # noqa: E402
import repo_root as R  # noqa: E402
import toolchain as TC  # noqa: E402

fails = 0


def check(what, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + what)
    fails += not ok


def git(cwd, *args):
    subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@example.invalid", "-C", str(cwd), *args],
                   check=True, capture_output=True)


def under(path, parent):
    return Path(path).resolve().is_relative_to(Path(parent).resolve())


with tempfile.TemporaryDirectory() as td:
    td = Path(td).resolve()
    main = td / "main"
    main.mkdir()
    git(main, "init", "-q")
    (main / "f").write_text("x")
    git(main, "add", "f")
    git(main, "commit", "-q", "-m", "x")
    wt = td / "main" / "worktrees" / "w1"
    git(main, "worktree", "add", "-q", str(wt), "-b", "w1")
    (wt / "build" / "toolchains").mkdir(parents=True)          # a worktree's own build/ must NOT be used
    copy = td / "farm" / "worktrees" / "c1"
    copy.mkdir(parents=True)
    farm_tc = td / "farm" / "build" / "toolchains"

    check("main checkout: its own build/toolchains", R.toolchains_dir(main, {}) == main / "build" / "toolchains")
    check("linked worktree: the main checkout's build/toolchains, never the worktree's",
          R.toolchains_dir(wt, {}) == main / "build" / "toolchains")
    check("linked worktree: M-Flasher's folder is the main checkout's too",
          R.build_dir("m-flasher", "M_FLASHER_HOME", wt, {}) == main / "build" / "m-flasher")
    check("plain copy with the variable: the variable", R.toolchains_dir(copy, {R.TOOLCHAINS_ENV: str(farm_tc)}) == farm_tc)
    farm_tc.mkdir(parents=True)
    check("plain copy without it: walks up to the nearest build/toolchains", R.toolchains_dir(copy, {}) == farm_tc)
    check("plain copy, nothing above: its own build/toolchains",
          R.toolchains_dir(td / "farm" / "worktrees", {}) == farm_tc)         # (the walk starts at the tree itself)
    lone = td / "lone"
    lone.mkdir()
    check("nothing anywhere: the tree's own build/toolchains", R.toolchains_dir(lone, {}) == lone / "build" / "toolchains")
    check("M-Flasher: M_FLASHER_HOME still wins", R.build_dir("m-flasher", "M_FLASHER_HOME", wt, {"M_FLASHER_HOME": "/x/y"}) == Path("/x/y"))

    # llvm_dir: <toolchains>/pi32v2-llvm-dropin-<version>, or PI32V2_LLVM_TOOLCHAIN
    version = f"pi32v2-llvm-dropin-{TC.LLVM_VERSION}"
    env = {R.TOOLCHAINS_ENV: str(farm_tc)}
    check("llvm_dir: <toolchains>/pi32v2-llvm-dropin-<version> by default", TC.llvm_dir(env, copy) == farm_tc / version)
    check("llvm_dir: PI32V2_LLVM_TOOLCHAIN wins", TC.llvm_dir(dict(env, PI32V2_LLVM_TOOLCHAIN="/x/dropin"), copy) == Path("/x/dropin"))

    # the download: default target inside the repo, nothing written outside it
    repo = td / "repo"
    (repo / "tools").mkdir(parents=True)
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:xz") as t:
        data = b"#!/bin/sh\n"
        ti = tarfile.TarInfo(f"{version}/pi32v2/bin/clang")
        ti.size = len(data)
        t.addfile(ti, io.BytesIO(data))
    blob = buf.getvalue()
    key = ("linux", "x86_64")
    keep = (deps.download, TC.LLVM_ASSETS[key], R.shared.ROOT, os.environ.get("HOME"), os.environ.get(R.TOOLCHAINS_ENV),
            TC.host_key)
    os.environ.pop(R.TOOLCHAINS_ENV, None)
    os.environ["HOME"] = str(td / "home")
    before = set(td.rglob("*"))
    urls = []
    deps.download = lambda url, dest: (urls.append(url), Path(dest).write_bytes(blob))
    TC.LLVM_ASSETS[key] = (TC.LLVM_ASSETS[key][0], hashlib.sha256(blob).hexdigest(), 1)
    TC.host_key = lambda: key
    R.shared.ROOT = repo
    try:
        target = R.toolchains_dir(repo)
        check("download target (default): inside the repository's build/toolchains", under(target, repo) and target == repo / "build" / "toolchains")
        got = deps.fetch_llvm()
        check("fetch_llvm: the drop-in is in the repository, no symlink", got == repo / "build" / "toolchains" / version
              and TC.has_clang(got) and not got.is_symlink())
        check("fetch_llvm: the host's archive, from the repository's release",
              urls == [f"{TC.LLVM_RELEASE_URL}/{TC.LLVM_ASSETS[key][0]}"])
        new = set(td.rglob("*")) - before
        check("fetch_llvm: every path it created is inside the repository (nothing in ~ or elsewhere)",
              new and all(under(p, repo) for p in new))
        TC.LLVM_ASSETS[key] = (TC.LLVM_ASSETS[key][0], "0" * 64, 1)
        shutil.rmtree(got)
        try:
            deps.fetch_llvm()
            check("fetch_llvm: an archive that does not match its SHA-256 is refused", False)
        except deps.FetchError as e:
            check("fetch_llvm: an archive that does not match its SHA-256 is refused", "SHA-256" in str(e) and not got.exists())
        TC.host_key = lambda: ("windows", "x86_64")
        try:
            deps.fetch_llvm()
            check("fetch_llvm: a host with no drop-in is refused", False)
        except deps.FetchError as e:
            check("fetch_llvm: a host with no drop-in is refused, says so", "windows" in str(e))
    finally:
        deps.download, TC.LLVM_ASSETS[key], R.shared.ROOT = keep[:3]
        TC.host_key = keep[5]
        os.environ["HOME"] = keep[3]
        if keep[4]:
            os.environ[R.TOOLCHAINS_ENV] = keep[4]

check("M-Flasher home() ends in build/m-flasher/m-flasher-<version> unless M_FLASHER_HOME is set",
      os.environ.get("M_FLASHER_HOME") or mflasher.home().parent.name == "m-flasher")
print("repo_root: FAILED" if fails else "repo_root: all passed")
sys.exit(1 if fails else 0)
