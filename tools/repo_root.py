# SPDX-License-Identifier: GPL-3.0-only
"""Where the tools the repository fetches live: build/<sub> of the MAIN checkout (nothing in the user's home).

One rule for every fetched tool (the pi32v2-llvm drop-in: build/toolchains; M-Flasher: build/m-flasher), in this order:
  1. the environment variable of the tool (OPTIMIST_TOOLCHAINS, M_FLASHER_HOME), when set: the farm's copies of a tree
     (tar, no .git, run in ~/optimist/worktrees/<name>) set it to ~/optimist/build/<sub>
  2. a linked git worktree: the MAIN checkout's build/<sub> (git rev-parse --git-common-dir, tools/shared.py), so a
     worktree never downloads a second copy
  3. the nearest build/<sub> in this tree or above it (a plain copy below a checkout that has one)
  4. this tree's build/<sub> (where setup downloads when nothing exists yet)
Setup writes only to the result of this rule; it is always inside a repository checkout.
"""
import os
from pathlib import Path

import shared

TOOLCHAINS_ENV = "OPTIMIST_TOOLCHAINS"


def build_dir(sub, env_var=None, root=None, env=None):
    """-> the directory build/<sub> (or the env_var's value) the tools of this tree share"""
    env = os.environ if env is None else env
    root = Path(root or shared.ROOT).resolve()
    if env_var and env.get(env_var):
        return Path(env[env_var])
    main = shared.main_checkout(root)
    if main is not None:
        return main / "build" / sub
    for d in (root, *root.parents):
        if (d / "build" / sub).is_dir():
            return d / "build" / sub
    return root / "build" / sub


def toolchains_dir(root=None, env=None):
    return build_dir("toolchains", TOOLCHAINS_ENV, root, env)
