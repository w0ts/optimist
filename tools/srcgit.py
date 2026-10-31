# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""Which commit a source tree is: the one name every package, version string and SOURCE.txt gets it from.

  1. OPTIMIST_COMMIT in the environment, when set: the commit the caller says (a farm script exporting a tree with
     `git archive` passes the commit it archived); the tree counts as unchanged unless OPTIMIST_MODIFIED=1.
  2. git, but only when the source folder IS the repository's top level (`git rev-parse --show-toplevel` equals it).
     A tree exported without .git and put inside another checkout (gpufarm: ~/optimist/worktrees/NAME) would
     otherwise find that checkout and be named after ITS commit (2026-10-31: optimist-0.2-dev-60252ddd-modified.fwsc).
  3. otherwise None: the callers write "unknown", plainly. Never a commit that is not the tree's.
"""
import os
import subprocess
from pathlib import Path


def _git(root, *args):
    try:                                # (no git: a tree from a zip, or Windows without git)
        r = subprocess.run(["git", "-C", str(root), *args], capture_output=True, text=True)
    except OSError:
        return ""
    return r.stdout.strip() if r.returncode == 0 else ""


def is_toplevel(root):
    """True when root is the top level of a git repository (not a folder inside one)"""
    top = _git(root, "rev-parse", "--show-toplevel")
    return bool(top) and Path(top).resolve() == Path(root).resolve()


def commit(root, short=False):
    """-> the tree's commit (7 characters when short), or None when it is not known"""
    given = os.environ.get("OPTIMIST_COMMIT", "").strip()
    if given:
        return given[:7] if short else given
    if not is_toplevel(root):
        return None
    return _git(root, "rev-parse", *(["--short"] if short else []), "HEAD") or None


def modified(root, *paths):
    """-> True when the tree has uncommitted changes under paths (False for a given commit unless OPTIMIST_MODIFIED=1,
    and for a tree that is not a repository of its own: nothing is known)"""
    if os.environ.get("OPTIMIST_COMMIT", "").strip():
        return os.environ.get("OPTIMIST_MODIFIED", "") == "1"
    if not is_toplevel(root):
        return False
    return bool(_git(root, "status", "--porcelain", "--", *paths))
