# SPDX-License-Identifier: GPL-3.0-only
"""Git worktrees share what is fetched and installed (BUILDING.md, "Worktrees").

In a linked worktree of the repository, the SDK files (sdk/), the builder's venv (tools/builder/venv), the
emulator's clone and build (emulator/fm1-emulator) and the sample download cache (build/hiphop-src) are the
MAIN checkout's: used from there when present, installed there once when not, never fetched per worktree.
Per worktree stay build/ (outputs, generated headers), the emulator's flash state and logs, and firmwares/.

The main checkout is the parent of `git rev-parse --git-common-dir` (found from the .git file when git is not
installed). Installs into a shared directory take install_lock(): one process installs, the others wait and then
find it done. Reading is not locked. No hidden folders: the lock is the visible file optimist-install.lock
inside the directory it guards (all of them git-ignored).
"""
import contextlib
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOCK_NAME = "optimist-install.lock"
STALE_SECONDS = 3600            # a lock older than this is a crashed install's (Windows: also the only test)
NOTED_ENV = "OPTIMIST_SHARED_NOTED"
LOCAL_PROFILES_ENV = "OPTIMIST_LOCAL_PROFILES"      # =1: a worktree uses its own config/ (a branch that changes a profile)


def _git_common_dir(root):
    if not (Path(root) / ".git").is_file():     # (a main checkout has a .git folder; a linked worktree a file)
        return None
    try:
        p = subprocess.run(["git", "-C", str(root), "rev-parse", "--git-common-dir"], capture_output=True,
                           text=True, timeout=30)
    except (OSError, subprocess.SubprocessError):
        p = None
    if p is not None and p.returncode == 0 and p.stdout.strip():
        return (Path(root) / p.stdout.strip()).resolve()
    return _common_dir_from_file(root)


def _common_dir_from_file(root):
    """no git: root/.git is a file 'gitdir: <main>/.git/worktrees/<name>' in a linked worktree, whose commondir
    file says '../..'"""
    dotgit = Path(root) / ".git"
    try:
        if not dotgit.is_file():
            return None
        text = dotgit.read_text().strip()
        if not text.startswith("gitdir:"):
            return None
        gitdir = (Path(root) / text[len("gitdir:"):].strip()).resolve()
        common = (gitdir / (gitdir / "commondir").read_text().strip()).resolve()
    except OSError:
        return None
    return common


def main_checkout(root=None):
    """-> the main worktree's path when root is a LINKED worktree, else None (the main checkout itself, no git,
    a bare repository)"""
    root = Path(root or ROOT).resolve()
    common = _git_common_dir(root)
    if common is None or common.name != ".git":
        return None
    main = common.parent
    return None if main == root else main


def resolve(rel, valid, root=None):
    """-> (path, shared): where `rel` (relative to the repository root) is used. In a linked worktree: the main
    checkout's when valid(path), else this worktree's own when valid, else the main checkout's (where an install
    puts it). In the main checkout: its own."""
    root = Path(root or ROOT)
    main = main_checkout(root)
    if main is None:
        return root / rel, False
    shared, own = main / rel, root / rel
    if valid(shared) or not valid(own):
        return shared, True
    return own, False


def config_dir(root=None, env=None):
    """-> (the folder holding profiles/, my-profiles/, last-used.txt and user.config, shared): in a linked worktree
    the MAIN checkout's config/ (the profiles saved in the builder are the ones every worktree sees), unless
    OPTIMIST_LOCAL_PROFILES=1 (a branch that changes a profile on purpose) or the main checkout has no
    config/profiles; in the main checkout, its own"""
    root = Path(root or ROOT)
    env = os.environ if env is None else env
    if env.get(LOCAL_PROFILES_ENV, "").strip().lower() in ("1", "yes", "true", "on"):
        return root / "config", False
    if not ((main_checkout(root) or root) / "config" / "profiles").is_dir():
        return root / "config", False
    path, is_shared = resolve("config/profiles", Path.is_dir, root)
    return path.parent, is_shared


def note(label, path):
    """one line on stderr per resource and command (children of a command inherit what was said)"""
    said = set(filter(None, os.environ.get(NOTED_ENV, "").split(",")))
    if label in said:
        return
    os.environ[NOTED_ENV] = ",".join(sorted(said | {label}))
    home = str(Path.home())
    shown = str(path)
    if shown.startswith(home):
        shown = "~" + shown[len(home):]
    print(f"{label}: using the main checkout's ({shown})", file=sys.stderr, flush=True)


def _alive(pid):
    if os.name == "nt":
        return True             # (os.kill(pid, 0) would terminate it there: only the age tells)
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def _holder(lock):
    try:
        text = lock.read_text()
        return int(text.split()[0])
    except (OSError, ValueError, IndexError):
        return None


def _stale(lock):
    try:
        age = time.time() - lock.stat().st_mtime
    except OSError:
        return False            # (gone: not stale, just free)
    pid = _holder(lock)
    return age > STALE_SECONDS or (pid is not None and not _alive(pid) and age > 2)


@contextlib.contextmanager
def install_lock(directory, what, timeout=3600, poll=0.2):
    """hold directory/optimist-install.lock while installing `what` into directory; waits for another install
    (any process, any worktree) and then runs: re-check under the lock whether the install is still needed"""
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    lock = directory / LOCK_NAME
    deadline = time.time() + timeout
    told = False
    while True:
        try:
            fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
        except FileExistsError:
            if _stale(lock):                # (a crashed install's: one waiter wins the rename and removes it)
                try:
                    gone = lock.with_name(f"{LOCK_NAME}.stale-{os.getpid()}")
                    os.replace(lock, gone)
                    gone.unlink(missing_ok=True)
                except OSError:
                    pass
                continue
            if time.time() > deadline:
                raise TimeoutError(f"{lock}: another install of {what} did not finish (delete the file if "
                                   f"none is running)")
            if not told:
                print(f"{what}: waiting for another install into {directory} (pid {_holder(lock) or '?'})",
                      file=sys.stderr, flush=True)
                told = True
            time.sleep(poll)
            continue
        break
    try:
        with os.fdopen(fd, "w") as f:
            f.write(f"{os.getpid()} {what}\n")
        yield lock
    finally:
        lock.unlink(missing_ok=True)
