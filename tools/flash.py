# SPDX-License-Identifier: GPL-3.0-only
"""Flash a build to an FM-1 over USB-MIDI: the step behind `python tools/optimist.py flash` and the builder menu's
"Flash to FM-1". It checks, shows and asks; the write itself is tools/fm1_install.py, the same update the web
installer does (it needs mido and python-rtmidi: `python tools/optimist.py setup` puts them in the builder venv).

  preflight(package)  -> a Plan: the package's identity, the running FM-1's (fm1_install.py --info), what will happen
  install(plan)       -> fm1_install.py PACKAGE --yes (never --force: only a package with the Felucca loader)

Never picks a stock or foreign package: the default is the newest build/optimist-*.fwsc, and a package must be an
Optimist one (identity FM-1_700, or FM-1_720 .. 769 for a release) that fm1_install.load_package accepts.
Nothing here touches MIDI itself: every device access goes through run_tool() (fm1_install.py in a subprocess),
which the tests replace."""
import re
import subprocess
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
sys.path.insert(1, str(HERE / "builder"))               # (stamp: the commit a package was built from)
import fm1_install as FI  # noqa: E402
import stamp  # noqa: E402

TOOL = HERE / "fm1_install.py"
OPTIMIST_ID = re.compile(r"FM-1_7(00|[2-6]\d)")         # (Optimist: 700 development, 720..769 releases; BUILDING.md)
NEED = "needs mido and python-rtmidi in the builder venv: run `make setup` (python tools/optimist.py setup)"
RECOVER = ("If the install is cut off the FM-1 stays in update mode: run flash again to finish. If it no longer "
           "starts: hold OCT- alone while switching on (USB rescue) and flash again, or go back to the official "
           "firmware; OPTIMIST.md, \"Rescue, going back\". Back up your projects first (the editor's Backup).")


class FlashError(Exception):
    def __init__(self, msg, code=1):
        super().__init__(msg)
        self.code = code


class Plan:
    def __init__(self, package, product, running, port, notes=()):
        self.package, self.product, self.running, self.port = Path(package), product, running, port
        self.notes = list(notes)                         # where the package comes from, and what to know about it

    def lines(self):
        return [f"package:  {self.package.name}  ({self.product})",
                f"          {self.package}",
                *self.notes,
                f"running:  {self.running}",
                "",
                "This replaces the firmware on the FM-1 now. Do not unplug it while it writes (about a minute).",
                RECOVER]


def last_package(build=None):
    """the newest build/optimist-*.fwsc (the user-facing name a build leaves), else None"""
    found = sorted((Path(build) if build else ROOT / "build").glob("optimist-*.fwsc"),
                   key=lambda p: p.stat().st_mtime)
    return found[-1] if found else None


def midi_python():
    """a Python that has mido and rtmidi: this one, else the builder venv's; None when neither"""
    import deps
    probe = "import mido, rtmidi"
    for py in (sys.executable, str(deps.venv_python())):
        try:
            if Path(py).exists() and subprocess.run([py, "-c", probe], capture_output=True).returncode == 0:
                return py
        except OSError:
            continue
    return None


def run_tool(args, capture=True):
    """fm1_install.py with args under a Python that has the MIDI modules -> (exit code, output). The only place
    a device is reached"""
    py = midi_python()
    if not py:
        raise FlashError(NEED)
    r = subprocess.run([py, str(TOOL), *args], capture_output=capture, text=True)
    return r.returncode, ((r.stdout or "") + (r.stderr or "")).strip()


def check_package(path):
    """-> the package's identity; FlashError when it is missing, damaged, stock or foreign"""
    p = Path(path)
    if not p.is_file():
        raise FlashError(f"{p}: no such package", 2)
    try:
        product, _ = FI.load_package(str(p), False)
    except FI.InstallError as e:
        raise FlashError(str(e), 2)
    if not OPTIMIST_ID.fullmatch(product):
        raise FlashError(f"{p.name}: identity {product}, not an Optimist build (FM-1_700, FM-1_720 .. FM-1_769); "
                         "not flashed from here (tools/fm1_install.py installs other Felucca packages)", 2)
    return product


SOURCE_RE = re.compile(r"at ([0-9a-f]{7,40})( \(with uncommitted changes\))?.*\nbuild configuration: (\S+)"
                       r"(?: hash ([0-9a-f]{8}))?")


def package_source(path):
    """-> {"commit", "dirty", "name", "hash"} of a build's package, read from the SOURCE.txt of the -ui.zip that
    tools/build.py leaves beside it, or None (no sidecar: a download, a copy)"""
    p = Path(path)
    ui = p.with_name(p.stem + "-ui.zip")
    try:
        with zipfile.ZipFile(ui) as z:
            text = z.read("SOURCE.txt").decode("utf-8", "replace")
    except (OSError, KeyError, zipfile.BadZipFile):
        return None
    m = SOURCE_RE.search(text)
    if not m:
        return None
    return {"commit": m[1], "dirty": bool(m[2]), "name": m[3], "hash": m[4] or ""}


def source_notes(path, root=ROOT):
    """the lines the confirmation shows about where a package comes from: its commit and configuration, and when
    the checkout has moved on since it was built (the package is then an older firmware than the tree)"""
    src = package_source(path)
    if src is None:
        return ["built:    (unknown: no -ui.zip beside the package)"]
    out = [f"built:    {src['name']} (config {src['hash'] or '?'}) from {src['commit'][:7]}"
           + (" with uncommitted changes" if src["dirty"] else "")]
    now = stamp.head(root)[0]
    if now and not now.startswith(src["commit"]):
        behind = stamp.git(root, "rev-list", "--count", f"{src['commit']}..{now}")
        out.append(f"NOTE      the checkout is at {now[:7]} now" + (f", {behind} commit(s) newer" if behind else "")
                   + ": this package is an older build (build again for the newest)")
    return out


def preflight(package, port=None, notes=None):
    """-> Plan; FlashError (with an exit code) when the package or the FM-1 is not right. Reads the FM-1's
    identity (fm1_install.py --info); writes nothing. notes: the lines about the package's source (default:
    source_notes, from its -ui.zip)"""
    if package is None:
        raise FlashError("no build to flash: build/ has no optimist-*.fwsc (python tools/optimist.py build, "
                         "or give a PACKAGE.fwsc)")
    product = check_package(package)
    notes = source_notes(package) if notes is None else notes
    rc, text = run_tool(["--info", *(["--port", port] if port else [])])
    if rc:
        raise FlashError(("no FM-1 found over USB-MIDI (data cable? another app using it?): " if rc == 3
                          else f"cannot read the FM-1's identity (exit {rc}): ") + text, rc)
    return Plan(package, product, text, port, notes)


def install(plan, capture=True):
    """write the plan's package (the caller has asked) -> (exit code, output). Never --force"""
    args = [str(plan.package), "--yes", *(["--port", plan.port] if plan.port else [])]
    return run_tool(args, capture)
