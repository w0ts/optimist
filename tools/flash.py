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
import contextlib
import os
import re
import signal
import subprocess
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
sys.path.insert(1, str(HERE / "builder"))               # (stamp: the commit a package was built from)
import fm1_install as FI  # noqa: E402
import mflasher as MF  # noqa: E402
import progress  # noqa: E402  (stream: the tool's output as it arrives)
import stamp  # noqa: E402

TOOL = HERE / "fm1_install.py"
OPTIMIST_ID = re.compile(r"FM-1_7(00|[2-6]\d)|FM-1_79\d{2}(\d{4})?")   # (Optimist: 700 development, 720..769 releases;
                                                        # BUILDING.md; FM-1_79: the release on fm1-base, fm1.toml id_prefix)
RELEASE = [os.environ.get("OPTIMIST_CLASSIC") != "1"]                                        # (the release's packages: fm1-base's update tool when the FM-1 is not
                                                        # running Optimist; set_flavour)
FM1B_PORTS = ("FM1-", "Optimist")                       # (the USB-MIDI port names fm1-base's tool is given, in turn: "FM1-XXXX" since
                                                        # 2026-10-29; "Optimist", an FM-1 on older firmware)
NEED = "needs mido and python-rtmidi in the builder venv: run `make setup` (python tools/optimist.py setup)"
RECOVER = ("If the install is cut off the FM-1 stays in update mode: run flash again to finish. If it no longer "
           "starts: hold OCT- alone while switching on (USB rescue) and flash again, or go back to the official "
           "firmware; README.md, \"Getting back\". Back up your projects first (the editor's Backup).")


class FlashError(Exception):
    def __init__(self, msg, code=1):
        super().__init__(msg)
        self.code = code


class Plan:
    def __init__(self, package, product, running, port, notes=(), tool=None, warn=None, route=None, boot_line="",
                 install_mboot=False, settings=None, settings_flag=None):
        self.settings = settings                         # fm1_route.Settings: keep or erase, asked or not (download mode)
        self.settings_flag = settings_flag               # "keep" | "wipe" | None (the policy's default)
        self.package, self.product, self.running, self.port = Path(package), product, running, port
        self.tool = tool                                 # (None: Optimist's own update tool; else fm1-base's)
        self.notes = list(notes)                         # where the package comes from, and what to know about it
        self.warn = warn                                 # why the running firmware may refuse it (platform_note)
        self.route = route                               # fm1_route.Route: the update loader, or download mode
        self.boot_line = boot_line                       # the build's BOOTLOADER as this install takes it
        self.install_mboot = install_mboot           # --install-mboot: m-bootloader asked for
        self.layout_warn, self.layout_known = layout_note(package)   # the backup warning of a layout change (never a refusal)

    def settings_line(self):
        import fm1_route as R
        return R.settings_choice(self.settings, self.settings_flag)[1]

    @property
    def download(self):
        return bool(self.route) and self.route.method == "download"

    @property
    def boot(self):
        """the bootloader the running Optimist says it has (fm1_install.py --info's boot=...): mboot | stock | unknown | None"""
        import fm1_route as R
        return R.boot_token(self.running)

    @property
    def wants_mboot(self):
        """installing m-bootloader was asked for (--install-mboot) and the FM-1 does not say it has it already"""
        return self.install_mboot and self.boot != "mboot"

    def lines(self):
        """the confirmation: about four lines (what, from where, onto what, which way); the route's steps only for
        download mode, where they differ from the usual install"""
        boot = {"mboot": ", m-bootloader on it", "stock": ", M-VAVE's bootloader on it"}.get(self.boot, "")
        how = []
        if self.download:
            how = [f"route:    download mode (M-Flasher; the computer's admin password): {self.route.why}",
                   *[f"          {i}. {st}" for i, st in enumerate(self.route.steps, 1)],
                   *([self.boot_line] if self.boot_line else []),
                   *([self.settings_line()] if self.settings else [])]
        elif self.route:
            kept = ("m-bootloader kept" if self.boot == "mboot" else "the bootloader kept")
            how = [f"route:    Flash over USB-MIDI (about a minute), settings and {kept}"]
        return [f"package:  {self.package.name}  ({self.product})",
                *self.notes,
                f"running:  {self.running.split()[0] if self.running.strip() else '?'}{boot}",
                *how,
                *([f"WARNING   {self.warn}"] if self.warn else []),
                *([f"LAYOUT    {self.layout_warn}"] if self.layout_warn else []),
                "Do not unplug the FM-1 while it writes."]


def set_flavour(classic):
    """classic: Optimist's own firmware (its update path only); else the release (fm1-base's tool as the fallback)"""
    RELEASE[0] = not classic


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


def run_tool(args, capture=True, on_line=None, tool=None):
    """fm1_install.py with args under a Python that has the MIDI modules -> (exit code, output). The only place
    a device is reached. on_line: called with each line and each \\r progress update as the tool prints it (the
    menu's progress bar; progress.parse_flash reads them)"""
    py = midi_python()
    if not py:
        raise FlashError(NEED)
    tool = tool or TOOL
    writing = "--info" not in args                      # (a write: Ctrl+C is refused until it is done, FI.no_interrupt in the tool too)
    with (FI.no_interrupt() if writing else contextlib.nullcontext()):
        if on_line:
            rc, out = progress.stream([py, str(tool), *args], on_line=on_line, env=dict(os.environ, PYTHONUNBUFFERED="1"))
            return rc, out.strip()
        r = subprocess.run([py, str(tool), *args], capture_output=capture, text=True)
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


def fm1base_tool():
    """fm1base/tools/fm1_install.py of the submodule, or None when it is not checked out"""
    import fm1base
    try:
        return fm1base.install_tool()
    except fm1base.FM1BaseError:
        return None


def preflight(package, port=None, notes=None, install_mboot=False):
    """-> Plan; FlashError (with an exit code) when the package or the FM-1 is not right. Reads the FM-1's
    identity (fm1_install.py --info); writes nothing. notes: the lines about the package's source (default:
    source_notes, from its -ui.zip)"""
    if package is None:
        raise FlashError("no build to flash: build/ has no optimist-*.fwsc (python tools/optimist.py build, "
                         "or give a PACKAGE.fwsc)")
    product = check_package(package)
    notes = source_notes(package) if notes is None else notes
    rc, text = run_tool(["--info", *(["--port", port] if port else [])])
    tool = None
    if rc == 3 and RELEASE[0] and not port:             # (no Optimist FM-1: an fm1-base one, its tool, its port name)
        fm1b = fm1base_tool()
        if fm1b:
            for name in FM1B_PORTS:
                rc2, text2 = run_tool(["--info", "--port", name], tool=fm1b)
                if rc2 == 0:
                    rc, text, tool, port = 0, text2, fm1b, name
                    break
    if rc:
        raise FlashError(("no FM-1 found over USB-MIDI (data cable? another app using it?): " if rc == 3
                          else f"cannot read the FM-1's identity (exit {rc}): ") + text, rc)
    route, boot_line = route_of(package, text, port, install_mboot)
    if route.method == "download":
        tool = None                                     # (download mode: Optimist's tool, through M-Flasher)
    warn = platform_note(package, product, text) if route.method == "loader" else None
    return Plan(package, product, text, port, notes, tool, warn, route, boot_line, install_mboot,
                settings_of(package, text))


def settings_of(package, running):
    """fm1_route.settings_policy for this package on the FM-1 that --info described"""
    import fm1_route as R
    raw = Path(package).read_bytes()
    ident = running.split()[0] if running.strip() else None
    return R.settings_policy(ident, R.package_kind(raw, FI.flash_size(FI.logical_image(raw))))


def with_settings(plan, flag):
    """a copy of the plan with --keep-settings / --wipe-settings (flag "keep" / "wipe" / None: the policy's default)"""
    import copy
    out = copy.copy(plan)
    out.settings_flag = flag
    return out


def route_of(package, running, port=None, install_mboot=False):
    """-> (fm1_route.Route, the bootloader line) for this package on the FM-1 that --info described"""
    import fm1_anyflash as AF
    import fm1_route as R
    boot = R.boot_token(running)
    text = R.BOOT_TOKEN.sub("", running).strip()
    m = re.match(r"(\S+)\s+\[[^]]*\]\s+port:\s*(.*)$", text)
    ident, name = (m[1], m[2].strip()) if m else (text.split()[0] if text else None, port or "")
    raw = Path(package).read_bytes()
    info = R.package_kind(raw, FI.flash_size(FI.logical_image(raw)))
    choice, line = AF.build_setting(package, install_mboot)
    return R.choose(R.running_kind(ident, name), info, choice, R.mboot_present_of(boot), ident or ""), line


def install_mboot(plan):
    """the same plan with --install-mboot: m-bootloader asked for explicitly (the builder's entry), through download
    mode; its route chosen again (the device is not read again)"""
    route, line = route_of(plan.package, plan.running, plan.port, True)
    warn = platform_note(plan.package, plan.product, plan.running) if route.method == "loader" else None
    return Plan(plan.package, plan.product, plan.running, plan.port, plan.notes, plan.tool if route.method == "loader"
                else None, warn, route, line, True, plan.settings, plan.settings_flag)


def platform_note(package, product, running):
    """fm1_install.platform_warning for this package on the FM-1 that --info described (running: its line,
    "FM-1_700  [running]  port: ..."), or None"""
    m = re.match(r"(([^_\s]+)_(\d+))\s", running)
    if not m:
        return None
    return FI.platform_warning(FI.Identity(m[1], m[2], int(m[3])), product, FI.load_package(str(package), False)[1])


def mflasher_question(plan):
    """the question to ask before a download-mode install when M-Flasher is not found here, else None"""
    if not plan.download or MF.binary() is not None:
        return None
    try:
        return MF.question()
    except MF.MFlasherError as e:
        raise FlashError(str(e))


def ensure_mflasher(plan, ask, say=print, download=None):
    """a download-mode install needs M-Flasher: found, or asked for once (ask(question) -> bool) and fetched, SHA-256
    checked, installed in its pinned place (MF.home()). Offline, refused or a bad check: FlashError, nothing written.
    -> the binary (None when the plan is not download mode)"""
    if not plan.download:
        return None
    try:
        return MF.ensure(ask, say, download)
    except MF.MFlasherError as e:
        raise FlashError(str(e))


def install(plan, capture=True, on_line=None):
    """write the plan's package (the caller has asked, the plan's warning shown) -> (exit code, output). --force
    only for a plan with a warning the caller accepted (the package passed load_package's own checks already:
    --force then lifts the installer's platform warning alone). on_line: see run_tool"""
    args = [str(plan.package), "--yes", *(["--force"] if plan.warn else []), *(["--port", plan.port] if plan.port else []),
            *(["--install-mboot"] if plan.install_mboot and not plan.tool else []),
            *(["--route", "download"] if plan.download else []),
            *([f"--{plan.settings_flag}-settings"] if plan.download and plan.settings_flag else [])]
    extra = {"tool": plan.tool} if plan.tool else {}      # (Optimist's own tool: as it was called before the release)
    rc, out = run_tool(args, capture, on_line, **extra) if on_line else run_tool(args, capture, **extra)
    if rc == 0:
        remember_install(plan.package)
    return rc, out


# ---- the layout warning (the user, 2026-10-31: "when we flash a different layout, we warn the user to backup their data
# first"): the FM-1's installed layout is not readable over the update protocol, so this computer remembers the layout of
# its last install (build/last-install.json); with none, the warning says the installed layout is not known
LAST_INSTALL = ROOT / "build" / "last-install.json"


def package_end(package):
    """the package's flash.bin end (its MEMORY step's app end); None when it is not one of ours"""
    import flash_layout
    try:
        end = FI.flash_size(FI.logical_image(Path(package).read_bytes()))
    except (OSError, ValueError, IndexError) as e:     # (a file too short for a package's head)
        print(f"flash: cannot read the package's layout ({e})", file=sys.stderr)
        return None
    return end if end in flash_layout.known_ends() else None


def last_install_end():
    """the app end of the last install from this computer, None when none is recorded (or the record is unreadable)"""
    import json
    try:
        return int(json.loads(LAST_INSTALL.read_text())["end"], 16)
    except FileNotFoundError:
        return None
    except (OSError, ValueError, KeyError, TypeError) as e:
        print(f"flash: {LAST_INSTALL} unreadable ({e}): the installed layout is not known", file=sys.stderr)
        return None


def layout_note(package):
    """-> (the warning line, whether the installed layout is known to differ): the last install's layout against the
    package's (flash_layout.switch_warning); ("", False) for the same layout or a package not of ours"""
    import flash_layout
    end = package_end(package)
    if end is None:
        return "", False
    prev = last_install_end()
    line = flash_layout.switch_warning(prev, end)
    if prev is not None and line:
        line += " (the layout of the last install from this computer)"
    return line, prev is not None and bool(line)


def remember_install(package):
    """after an install that worked: its layout is the FM-1's now (build/last-install.json)"""
    import json
    end = package_end(package)
    if end is None:
        return
    try:
        LAST_INSTALL.parent.mkdir(parents=True, exist_ok=True)
        LAST_INSTALL.write_text(json.dumps({"end": f"{end:#x}", "package": Path(package).name}) + "\n")
    except OSError as e:
        print(f"flash: the installed layout was not recorded ({e})", file=sys.stderr)
