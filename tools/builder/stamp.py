# SPDX-License-Identifier: GPL-3.0-only
"""What a build in the builder menu was made from, and whether "f: Flash to FM-1" may write it (menu.py).

The menu flashes only the package its own last build made, of the configuration shown now, by the builder code that
is running now. 2026-10-12: a menu left open for 21 hours flashed a 4-hour-old package of another commit and of
the profile as it was then (USB audio and MACROS on; the profile had since changed) as the "last build": the FM-1
failed to start it and went to its USB rescue (FM-1_000). The identity (FM-1_700) is the same for every build, so the
confirmation could not tell.

  Stamp(package, cfg_hash, name)       what a successful build made: the package, the configuration's hash and name
  refusal(stamp, cfg_hash, code0)       why f must not flash it (a sentence), else None
  source_line(root)                     the checkout and commit a build compiles (the menu shows it)
  code_fingerprint(), changed_code(a)   the builder's own Python files, as running vs on disk: the menu is stale when
                                        they differ (quit and run make builder again)
Plain Python: tests/builder_stamp_test.py runs it without Textual or a device."""
import hashlib
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent
ROOT = TOOLS.parent

# the builder's in-process code: the menu imports these once, at its start; tools/build.py and fm1_install.py's writes
# run in subprocesses (always the files on disk)
CODE = tuple(sorted(HERE.glob("*.py"))) + tuple(TOOLS / f for f in ("flash.py", "fm1_install.py", "shared.py",
                                                                      "deps.py", "backports.json"))
RESTART = "quit the builder (q) and start it again (make builder)"


def git(root, *args):
    """git -C root args -> its output, "" without git or outside a checkout"""
    try:
        r = subprocess.run(["git", "-C", str(root), *args], capture_output=True, text=True)
    except OSError:
        return ""
    return r.stdout.strip() if r.returncode == 0 else ""


def head(root=ROOT):
    """-> (full commit, branch) of the checkout, ("", "") without git; branch "" when detached"""
    return git(root, "rev-parse", "HEAD"), git(root, "symbolic-ref", "--short", "-q", "HEAD")


def source_line(root=ROOT):
    """the checkout a build compiles: its path, branch and commit (the builder builds the tree it runs in)"""
    commit, branch = head(root)
    where = f"{branch}@{commit[:7]}" if commit else "no git"
    return f"source: {root}  {where}"


def code_fingerprint(files=CODE):
    """-> {file name: sha256} of the builder's own code as it is on disk now"""
    out = {}
    for f in files:
        try:
            out[str(f)] = hashlib.sha256(Path(f).read_bytes()).hexdigest()
        except OSError:
            out[str(f)] = None
    return out


def changed_code(start, now=None):
    """the builder files that differ from start (a fingerprint taken when the menu started) -> [short names]"""
    now = code_fingerprint([Path(f) for f in start]) if now is None else now
    return sorted(Path(f).name for f in set(start) | set(now) if start.get(f) != now.get(f))


def file_sha(path):
    try:
        return hashlib.sha256(Path(path).read_bytes()).hexdigest()
    except OSError:
        return None


class Stamp:
    """what one successful build of the menu made: the package (and its contents' hash: another build in this
    checkout, the CLI's or a script's, replaces build/optimist-*.fwsc), the configuration's hash and name"""
    def __init__(self, package, cfg_hash, name):
        self.package, self.cfg_hash, self.name = Path(package), cfg_hash, name
        self.sha = file_sha(package)


def refusal(stamp, cfg_hash, code_start, code_now=None):
    """-> why the menu must not flash stamp's package now (one sentence), or None when it may.
    stamp: the last successful build of this menu (None: none yet); cfg_hash: the configuration shown now;
    code_start: the fingerprint the menu took when it started"""
    if stamp is None:
        return "no build of this configuration in this builder yet: build first (b), then flash (f)"
    if not stamp.package.is_file():
        return f"the last build's package is gone ({stamp.package.name}): build again (b)"
    if file_sha(stamp.package) != stamp.sha:
        return f"{stamp.package.name} was replaced since this builder made it (another build in this checkout): build again (b)"
    if stamp.cfg_hash != cfg_hash:
        return (f"the configuration changed since the last build ({stamp.name} {stamp.cfg_hash:08x}, now "
                f"{cfg_hash:08x}): build again (b), then flash")
    stale = changed_code(code_start, code_now)
    if stale:
        return f"the builder's own code changed since it started ({', '.join(stale)}): {RESTART}, then build and flash"
    return None
