#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""tools/srcgit.py and the version the builder names a package with (tools/builder/configure.py version()): the tree's
own repository gives its commit; a tree exported with `git archive` (no .git) inside another checkout (gpufarm:
~/optimist/worktrees/NAME) does NOT take the enclosing checkout's commit (2026-10-31: optimist-0.2-dev-60252ddd-modified.fwsc),
it says "unknown" or what OPTIMIST_COMMIT gives. Run from the repo root:
  python3 tests/srcgit_test.py"""
import importlib
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE / "tools"))
sys.path.insert(0, str(HERE / "tools" / "builder"))
srcgit = importlib.import_module("srcgit")
configure = importlib.import_module("configure")
failed = 0


def ok(cond, what):
    global failed
    print(("  ok   " if cond else "  FAIL ") + what)
    failed += not cond


def git(cwd, *a):
    env = {**os.environ, "GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@t", "GIT_COMMITTER_NAME": "t", "GIT_COMMITTER_EMAIL": "t@t"}
    return subprocess.run(["git", "-C", str(cwd), *a], capture_output=True, text=True, check=True, env=env).stdout.strip()


os.environ.pop("OPTIMIST_COMMIT", None)
os.environ.pop("OPTIMIST_MODIFIED", None)
with tempfile.TemporaryDirectory() as td:
    repo = Path(td).resolve() / "repo"
    (repo / "firmware").mkdir(parents=True)
    (repo / "VERSION").write_text("9.9\n")
    (repo / "firmware" / "a.c").write_text("int a;\n")
    git(repo, "init", "-q")
    git(repo, "add", "-A")
    git(repo, "commit", "-q", "-m", "x")
    head = git(repo, "rev-parse", "HEAD")
    # an export without .git, inside the repository (the farm's ~/optimist/worktrees/NAME inside ~/optimist)
    export = repo / "worktrees" / "exp"
    (export / "firmware").mkdir(parents=True)
    (export / "VERSION").write_text("9.9\n")
    (export / "firmware" / "a.c").write_text("int a;\n")

    ok(srcgit.commit(repo) == head and srcgit.commit(repo, short=True) == head[:7], "the repository itself: its commit")
    ok(srcgit.modified(repo, "firmware") is False, "... unchanged")
    (repo / "firmware" / "a.c").write_text("int b;\n")
    ok(srcgit.modified(repo, "firmware") is True, "... modified after an edit")
    (repo / "firmware" / "a.c").write_text("int a;\n")

    ok(srcgit.commit(export) is None, "an export inside the repository: no commit (not the enclosing one)")
    ok(srcgit.modified(export, "firmware") is False, "... and not 'modified' by the enclosing repository")

    configure.ROOT = export
    ok(configure.version() == "9.9-dev-unknown", f"version() of the export: unknown ({configure.version()})")
    configure.ROOT = repo
    ok(configure.version() == f"9.9-dev-{head[:7]}", f"version() of the repository: its commit ({configure.version()})")

    os.environ["OPTIMIST_COMMIT"] = "0123456789abcdef"
    configure.ROOT = export
    ok(srcgit.commit(export) == "0123456789abcdef", "OPTIMIST_COMMIT: the commit given")
    ok(configure.version() == "9.9-dev-0123456", f"version() of the export with OPTIMIST_COMMIT ({configure.version()})")
    os.environ["OPTIMIST_MODIFIED"] = "1"
    ok(configure.version() == "9.9-dev-0123456-modified", "... OPTIMIST_MODIFIED=1: -modified")
    os.environ.pop("OPTIMIST_COMMIT")
    os.environ.pop("OPTIMIST_MODIFIED")

    plain = Path(td).resolve() / "plain"
    plain.mkdir()
    ok(srcgit.commit(plain) is None, "a folder in no repository: no commit")

print("srcgit: " + ("FAILED" if failed else "all passed"))
sys.exit(1 if failed else 0)
