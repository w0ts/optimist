# SPDX-License-Identifier: GPL-3.0-only
"""What runs on the FM-1, what the package is, and the way to install one over the other (docs/INSTALL-PATHS.md).

  running_kind(identity, port)    the firmware answering on USB-MIDI (fm1_install.py's handshake), by its identity
  package_kind(raw)               the package, by its identity, its SHA-256 (M-VAVE's V15) and its flash.bin
  choose(running, package, ...)   -> Route: the running firmware's update loader ("loader"), the chip's download mode
                                  through M-Flasher ("download"), or refused, with the steps and how to get there

The normal way stays the running firmware's update loader wherever it takes the package (retro-compatible: every
firmware that ever ran on an FM-1 has one). Download mode (M-Flasher) is for what that loader refuses (Optimist's
own firmware and an fm1-base package, or the other way's m-bootloader installer), a firmware that does not answer,
and the builder's BOOTLOADER = m-bootloader. Nothing here touches a device."""
from __future__ import annotations

import hashlib
import re
from dataclasses import dataclass, field

import flash_layout                  # (tools/: the MEMORY steps' one table)

STOCK_V15_SHA256 = "db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a"
LOADER_MARK = b"FELUCCA-LOADER-1"
OWN_SLOTS = flash_layout.own_slots()   # Optimist's own updater's sizes, every MEMORY step's (ota.c step 1: fm1_steps.h FL_STEPS;
                                     # an updater older than the step S0 takes 0x93000 and 0xD0000 only: -15, nothing erased)
SOFT_KEY = bytes([0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7])   # download mode from a running app (m-bootloader SPEC 4.2)

# the firmwares (running) -- kind: (name, soft key, keys into download mode, its updater)
STOCK, BAUDGIRL, OPEN, FELUCCA, SLOOP, OPTIMIST, AMBIG_000, FM1BASE, OTHER, LOADER, NONE = (
    "stock", "baudgirl", "open", "felucca", "sloop", "optimist", "ambig000", "fm1base", "other", "loader", "none")
FIRMWARES = {
    STOCK: ("M-VAVE's stock firmware (FM-1_0NN: V15 015, V16 016)", "yes (seen on 015, M-Flasher docs)",
            "none of its own: the soft key, or m-bootloader's REC at power-on"),
    BAUDGIRL: ("Baud Girl (FM-1_020 .. 098)", "not known", "not known: m-bootloader's REC at power-on"),
    OPEN: ("M-VAVE's FM1 Open firmware (FM1_OFFICIAL_OPEN, FM1_COMMUNITY_N)", "not known",
           "not known: m-bootloader's REC at power-on"),
    FELUCCA: ("Felucca (FM-1_9NN)", "yes (M-Flasher docs)", "OCT- + OCT+ held 5 s"),
    SLOOP: ("SLOOP (FM-1_9NN, its port named SLOOP)", "not known", "OCT- + OCT+ held 5 s"),
    OPTIMIST: ("Optimist's own firmware (FM-1_700, FM-1_720 .. 769)", "yes (io/usb/usb.c)",
               "OCT- + OCT+ held 5 s while stopped; or held while switching on, 3 s"),
    AMBIG_000: ("FM-1_000: Optimist in its USB rescue, or an fm1-base development build", "yes (both)",
                "OCT- + OCT+ held while switching on, 3 s (Optimist); REC while switching on (fm1-base)"),
    FM1BASE: ("an fm1-base firmware (FM-1_0 + 6 digits, Optimist's release FM-1_79..)", "yes (felucca-bsp chip/usb.c)",
              "REC held while switching on"),
    OTHER: ("another firmware (FM-1_5NN Lunar, FM-1_701 ...)", "not known", "not known: m-bootloader's REC at power-on"),
    LOADER: ("an update loader (an install was cut off, or m-bootloader's MIDI MODE)", "no", "-"),
    NONE: ("nothing answers on USB-MIDI (off, no data cable, a firmware that does not start, already in "
           "download mode)", "no", "m-bootloader: REC while switching on, or PLAY then MIDI MODE; Optimist: OCT- + OCT+ while switching on, 3 s; "
           "else the USB_KEY dongle"),
}
SOFT_KEY_OK = {STOCK, FELUCCA, OPTIMIST, AMBIG_000, FM1BASE}
MBOOT_KEYS = "hold REC while switching on (m-bootloader's download mode; PLAY: its menu, DOWNLOAD MODE or MIDI MODE)"

# packages
P_V15, P_STOCK, P_MBOOT, P_OPTIMIST, P_FM1BASE, P_FELUCCA, P_OTHER = (
    "v15", "stock", "mboot", "optimist", "fm1base", "felucca", "other")
PACKAGES = {P_V15: "M-VAVE's V15, unchanged (its SHA-256)", P_STOCK: "another M-VAVE package (FM-1_0NN)",
            P_MBOOT: "m-bootloader's installer (FM-1_099)", P_OPTIMIST: "an Optimist build of its own (FM-1_7NN)",
            P_FM1BASE: "an fm1-base package (Optimist's release FM-1_79.., any FM-1_0 + 6 digits)",
            P_FELUCCA: "a Felucca / SLOOP package (FM-1_9NN)", P_OTHER: "another firmware's package"}


def running_kind(identity: str | None, port: str = "") -> str:
    if not identity:
        return NONE
    t = identity.strip()
    if t.lower().startswith("ota-"):
        return LOADER
    if re.fullmatch(r"FM1_(OFFICIAL_OPEN|COMMUNITY_\d+)", t):
        return OPEN
    if t == "FM-1_000":
        return AMBIG_000
    if re.fullmatch(r"FM-1_0(0[1-9]|1\d)", t):
        return STOCK
    if re.fullmatch(r"FM-1_0[2-9]\d", t) and t != "FM-1_099":
        return BAUDGIRL
    if re.fullmatch(r"FM-1_7(00|[2-6]\d)", t):
        return OPTIMIST
    if re.fullmatch(r"FM-1_9\d\d", t):
        return SLOOP if re.search(r"sloop", port or "", re.I) else FELUCCA
    if re.fullmatch(r"FM-1_\d{4,}", t):
        return FM1BASE
    return OTHER


def _product(raw: bytes) -> str:
    return "".join(chr((m - i - 1) & 0xFF) for i in range(20) if (m := raw[i * 0x30 + 0x2F]) != 0x7D) \
        if len(raw) >= 20 * 0x30 else ""


@dataclass(frozen=True)
class PackageInfo:
    kind: str
    product: str
    sha256: str
    flash_len: int | None      # its flash.bin's size (the layout it writes), None without one
    own_loader: bool           # its update loader is the Felucca family's (FELUCCA-LOADER-1)


def package_kind(raw: bytes, flash_len: int | None = None) -> PackageInfo:
    """flash_len: the package's flash.bin size (fm1_install.flash_size of its logical image; None: none)"""
    product = _product(raw)
    digest = hashlib.sha256(raw).hexdigest()
    own = LOADER_MARK in raw
    if digest == STOCK_V15_SHA256:
        kind = P_V15
    elif product == "FM-1_099":
        kind = P_MBOOT
    elif re.fullmatch(r"FM-1_0(0[1-9]|1\d)", product):
        kind = P_STOCK
    elif re.fullmatch(r"FM-1_7(00|[2-6]\d)", product) and flash_len in OWN_SLOTS and own:
        kind = P_OPTIMIST
    elif re.fullmatch(r"FM-1_9\d\d", product) and flash_len in OWN_SLOTS:
        kind = P_FELUCCA
    elif re.fullmatch(r"FM-1_(0\d{6}|79\d{2,})", product) or re.fullmatch(r"FM-1_\d{4,}", product):
        kind = P_FM1BASE
    else:
        kind = P_OTHER
    return PackageInfo(kind, product, digest, flash_len, own)


# ------------------------------------------------------------- the firmware's own word ---
# A running Optimist reads its boot area and says which bootloader is there: INFO tag 0x68, "boot=mboot", "boot=stock"
# or "boot=unknown" (firmware/src/system/bootid.h); fm1_install.py --info prints it at the end of its line. No token (an
# older firmware, another one, no editor in the build): not known, as before
BOOT_TOKEN = re.compile(r"(?:^|\s)boot=(mboot|stock|unknown)(?:\s|$)")
BOOT_PRESENT = {"mboot": True, "stock": False, "unknown": None}
INFO_TAG = re.compile(rb"\x68([\x01-\x7f])(boot=(?:mboot|stock|unknown))")


def boot_token(text: str | None) -> str | None:
    """the token's word ("mboot" | "stock" | "unknown") in an fm1_install.py --info line, None without"""
    m = BOOT_TOKEN.search(text or "")
    return m[1] if m else None


def boot_of_info(reply: bytes) -> str | None:
    """the word of INFO's tag 0x68 in the editor's INFO reply (F0 7D 46 4C 01 .. F7), None without: the tag's id, its
    length, then its text (web/EDITOR_PROTOCOL.md; the reply's other bytes are 7-bit too, so the text is looked for
    with its id and a length that matches it)"""
    for m in INFO_TAG.finditer(bytes(reply or b"")):
        if m[1][0] == len(m[2]):
            return m[2][5:].decode()
    return None


def mboot_present_of(token: str | None) -> bool | None:
    """True: m-bootloader is there; False: another bootloader (M-VAVE's) is; None: not known"""
    return BOOT_PRESENT.get(token or "")


# --------------------------------------------------------------- the running loader ---

def loader_takes(running: str, pkg: PackageInfo, running_text: str = "", mboot_present: bool | None = None) -> tuple:
    """(True, why) when the running firmware's update loader takes this package, (False, why) when it refuses it
    (or may, unverified): then download mode. mboot_present: the firmware's own word (True: m-bootloader is there)"""
    if pkg.kind == P_MBOOT:
        return False, "m-bootloader is installed through download mode here (a full backup first)"
    if mboot_present and running in (OPTIMIST, AMBIG_000) and pkg.kind in (P_V15, P_STOCK):
        return False, ("m-bootloader is installed (the firmware says so): Optimist's updater refuses M-VAVE's package "
                       "(-5, its head differs)")
    if running == LOADER:
        return True, "the update loader finishes the cut-off install (fm1_install.py resumes it)"
    if running in (FELUCCA, SLOOP):                 # (their updater's rules: not read here; Optimist's came from Felucca 1.6.1)
        if pkg.kind == P_FM1BASE:
            return False, "its updater is assumed to refuse fm1-base packages, as Optimist's (from Felucca 1.6.1) does"
        return True, "its own update loader (unverified for this package: download mode if it refuses)"
    if running in (OPTIMIST, AMBIG_000):
        if pkg.kind == P_V15:
            return True, "the way back to the official firmware (README.md, Getting back); not with m-bootloader installed: the updater stops with -5, then download mode"
        if pkg.flash_len not in OWN_SLOTS:
            return False, (f"its updater takes the layouts {', '.join(f'{s:#x}' for s in OWN_SLOTS)} only; this flash.bin is "
                           f"{pkg.flash_len:#x}" if pkg.flash_len else "its updater needs a flash.bin")
        if pkg.kind == P_FM1BASE:
            return False, "its updater refuses fm1-base packages (2026-10-29: -16, nothing written)"
        if not pkg.own_loader:
            return False, "its updater takes the Felucca family's loader or M-VAVE's only"
        return True, "its own update loader (the normal install)"
    if running == OPEN:
        if pkg.kind in (P_V15, P_STOCK):
            return True, "the Open firmware takes M-VAVE's own firmware (armed first: fm1-base's fm1_install.py)"
        return False, "the Open firmware's updater takes M-VAVE's own firmware only"
    if running == STOCK:
        if pkg.product == running_text:
            return False, f"the stock updater refuses the identity it runs ({running_text})"
        return True, "the stock updater (M-UPGRADE's protocol) takes any other package"
    if running == FM1BASE:
        return True, "fm1-base's updater (felucca-bsp chip/ota.c) takes any package with a bootable head"
    if running in (BAUDGIRL, OTHER):
        return True, "its updater is assumed stock-like (unverified): download mode if it refuses"
    return False, "nothing answers on USB-MIDI: only download mode reaches the flash"


# --------------------------------------------------------------------------- the route ---

KEEP, MBOOT = "keep", "m-bootloader"


@dataclass(frozen=True)
class Route:
    method: str                     # "loader" | "download" | "refused"
    why: str
    enter: str = ""                 # how download mode is entered
    soft_key: bool = False          # the installer sends the soft key over USB-MIDI
    install_mboot: bool = False     # m-bootloader is installed (if the FM-1 does not have it), after the firmware
    steps: tuple = field(default=())
    firmware: bool = True           # the package has a firmware to write (m-bootloader's installer has none)


def unit_runs(route: Route) -> tuple:
    """the M-Flasher runs that open the unit on a download-mode route, at most (mflasher.OPENS_UNIT: each one is the
    system's password dialog on macOS, an administrator prompt on Windows; find and download-mode are not): the backup,
    which is also the read the plan is made on and the model check (its lines are info's); the firmware's write
    (firmware, or restore of the planned flash on a layout switch), checked and answered in the same run, which restarts
    the unit; m-bootloader's write when asked for, after the firmware, which restarts it then. A package without a
    firmware and no bootloader to write: a restart"""
    runs = ("backup",) + (("firmware",) if route.firmware else ()) + (("bootloader",) if route.install_mboot else ())
    return runs if len(runs) > 1 else runs + ("restart",)


def bootloader_choice(build_value: str | None, install_mboot: bool = False) -> tuple:
    """(choice, line): the bootloader of this install. MIDI first (the user, 2026-10-31: "if we can use midi, just use
    midi"): the build's BOOTLOADER=1 means keep what is there; m-bootloader is installed only when asked for
    explicitly (--install-mboot, the builder's entry, the web button, the app's option), through download mode"""
    if install_mboot:
        return MBOOT, ("bootloader: m-bootloader (--install-mboot, asked for): installed if the FM-1 does not have it "
                       "yet, through download mode, after a full backup and your yes")
    if build_value == MBOOT:
        return KEEP, "bootloader: keep (kept as it is; --install-mboot installs m-bootloader, through download mode)"
    if build_value == KEEP:
        return KEEP, "bootloader: keep (from the build): not touched"
    return KEEP, "bootloader: keep (no build setting with this package): not touched"


# ------------------------------------------------------------------- settings and pairings ---
# Never M-Flasher's --wipe-settings: it erases what a package declares as VM and BTIF, which in Optimist's and fm1-base's
# packages is [app end, 0xE9000) and 0xE9000: the projects, and the SDK's VM with the radio calibration. A wipe here is
# this installer's own erase of the new firmware's settings (fm1_layouts.WIPE), in the planned flash, nothing else.

OWN_ID = re.compile(r"FM-1_7(00|[2-6]\d)")
RELEASE_ID = re.compile(r"FM-1_79\d{2,}")


@dataclass(frozen=True)
class Settings:
    default: str        # "keep" | "wipe"
    ask: bool           # the user is asked (the default suggested)
    reason: str
    target: str         # whose settings a wipe erases: "optimist" | "felucca" | "stock" | "" (none known)


def _family(identity: str) -> str:
    if OWN_ID.fullmatch(identity or "") or identity == "FM-1_000":
        return "own"
    if RELEASE_ID.fullmatch(identity or ""):
        return "release"
    return ""


def settings_policy(running_text: str | None, pkg: PackageInfo) -> Settings:
    """keep or erase the settings and pairings, by the (running, package) pair. Optimist -> Optimist keeps them
    (any version, any layout); between Optimist's own build and its release on fm1-base it asks, keep suggested (the
    same storage code reads the same addresses, 0xFC000 / 0xFD000; not run on an FM-1); another firmware and Optimist:
    asks, erase suggested. Projects and sequences are kept either way"""
    a, b = _family(running_text or ""), _family(pkg.product)
    target = "optimist" if b else ("stock" if pkg.kind in (P_V15, P_STOCK) else
                                   "felucca" if pkg.kind == P_FELUCCA else "")
    if a and b and a == b:
        return Settings("keep", False, "Optimist to Optimist: its settings record, the pairings and the RF "
                                       "trims are read by every later build", target)
    if a and b:
        return Settings("keep", True, "Optimist's own build and its release on fm1-base keep the settings at the same "
                                      "place with the same code (storage.c); not tried on an FM-1 yet", target)
    if not target:
        return Settings("keep", True, "the package's firmware is not one whose settings this installer knows: "
                                      "nothing can be erased for it", "")
    return Settings("wipe", True, "another firmware's settings are not this one's (M-Flasher warns of the same): "
                                  "erase the settings and pairings the new firmware reads; projects are kept", target)


def settings_choice(policy: Settings, flag: str | None, asked: bool | None = None) -> tuple:
    """(keep|wipe, line). flag: --keep-settings / --wipe-settings ("keep" / "wipe"), else the answer to the question
    (asked: True = erase), else the policy's default"""
    if flag in ("keep", "wipe"):
        choice, why = flag, f"--{flag}-settings"
    elif asked is not None:
        choice, why = ("wipe" if asked else "keep"), "your answer"
    else:
        choice, why = policy.default, "the default for this pair" + (" (asked)" if policy.ask else "")
    if choice == "wipe" and not policy.target:
        choice, why = "keep", "nothing known to erase for this firmware"
    text = "erased (the new firmware's settings and pairings only)" if choice == "wipe" else "kept"
    return choice, f"settings: {text}, {why}: {policy.reason}"


def entry_for(running: str, mboot_present: bool | None) -> tuple:
    """(soft key?, how the user gets there otherwise)"""
    keys = FIRMWARES[running][2]
    if mboot_present:
        keys = MBOOT_KEYS + ("; or " + keys if running != NONE and keys not in ("-",) else "")
    return running in SOFT_KEY_OK, keys


def choose(running: str, pkg: PackageInfo, bootloader: str = KEEP, mboot_present: bool | None = None,
           running_text: str = "") -> Route:
    """the way for this package onto this FM-1: USB-MIDI whenever the running loader takes the package. bootloader:
    bootloader_choice's (MBOOT only when installing m-bootloader was asked for, never from the build's BOOTLOADER); mboot_present: whether the FM-1 has
    m-bootloader (None: not known until M-Flasher reads the flash in download mode)"""
    soft, keys = entry_for(running, mboot_present)
    enter = ("the installer sends the soft key over USB-MIDI" + (f" (or: {keys})" if keys != "-" else "")) if soft \
        else f"by hand: {keys}"
    want = bootloader == MBOOT and pkg.kind != P_MBOOT and mboot_present is not True
    takes, why = loader_takes(running, pkg, running_text, mboot_present)
    if bootloader == MBOOT and mboot_present is True and pkg.kind != P_MBOOT:
        why = "m-bootloader is on the FM-1 already (the firmware says so), kept; " + why
    if not want and takes:
        return Route("loader", why, steps=("the running firmware checks the package and starts its update loader",
                                           "the loader writes the firmware area; the FM-1 restarts",
                                           "the installer checks the identity it comes back with"))
    reason = "m-bootloader was asked for (--install-mboot)" if want else why
    steps = ["download mode: " + enter, "M-Flasher reads the flash and saves a full backup (its path is printed); "
             "the model it reads (model, bootloader, firmware) must be an FM-1"]
    if pkg.kind == P_MBOOT:
        steps.append("m-bootloader into the boot area only: M-Flasher's plan checked, then your yes; the FM-1 restarts")
    else:
        steps += ["the installer plans the firmware area (only the sectors that change; a layout switch clears what "
                  "the other layout left); M-Flasher reads the flash again and shows its own plan, compared, then your yes",
                  "M-Flasher saves another backup, writes and verifies every sector, stops on a mismatch; the FM-1 "
                  "restarts" + (" after m-bootloader" if want else "")]
        if want:
            steps.append("m-bootloader into the boot area only (if the FM-1 does not have it), asked with the firmware")
    route = Route("download", reason, enter, soft, want or pkg.kind == P_MBOOT, tuple(steps), pkg.kind != P_MBOOT)
    n = len(unit_runs(route))
    return Route(route.method, route.why, route.enter, route.soft_key, route.install_mboot,
                 route.steps + (f"macOS asks for the admin password once per M-Flasher run that opens the FM-1: {n} "
                                "at most (" + ", ".join(unit_runs(route)) + ")",), route.firmware)


def head_refused(running: str, pkg: PackageInfo, code: str) -> bool:
    """True when the running Optimist's updater may have refused an M-VAVE package with -5 (its first 0x4000 bytes, the boot
    area, differ from the installed ones: m-bootloader is installed); the install stopped before anything was erased"""
    return running in (OPTIMIST, AMBIG_000) and pkg.kind in (P_V15, P_STOCK) and code in ("noloader", "stopped")


def refused_route(running: str, pkg: PackageInfo) -> Route:
    """the download-mode route after a -5: M-Flasher (docs/INSTALL-PATHS.md); the firmware's check is unchanged"""
    route = choose(running, pkg, KEEP, True, "")
    steps = route.steps if route.method == "download" else choose(running, pkg, MBOOT, True, "").steps
    return Route("download", "the updater refused the package (-5: its bootloader differs, as with m-bootloader installed)",
                 "OCT- + OCT+ held 5 s while stopped, or REC held while switching on (with m-bootloader)", False, False, steps)


# ------------------------------------------------------------------ the docs' matrix ---

MATRIX_RUNNING = (STOCK, OPEN, FELUCCA, SLOOP, OPTIMIST, AMBIG_000, FM1BASE, BAUDGIRL, NONE)
MATRIX_PACKAGES = (P_V15, P_OPTIMIST, P_FELUCCA, P_FM1BASE, P_MBOOT)
_SAMPLE = {P_V15: ("FM-1_015", 0x93000), P_OPTIMIST: ("FM-1_700", 0x93000), P_FELUCCA: ("FM-1_906", 0x93000),
           P_FM1BASE: ("FM-1_79000200", 0xCE000), P_MBOOT: ("FM-1_099", None)}


def sample_package(kind: str) -> PackageInfo:
    product, flen = _SAMPLE[kind]
    return PackageInfo(kind, product, "", flen, kind in (P_OPTIMIST, P_FELUCCA, P_FM1BASE))


def matrix_markdown() -> str:
    """the install-path matrix (docs/INSTALL-PATHS.md holds it verbatim: tests/install_paths_test.py)"""
    head = "| running \\ package | " + " | ".join(PACKAGES[k] for k in MATRIX_PACKAGES) + " |"
    rows = [head, "|" + "---|" * (len(MATRIX_PACKAGES) + 1)]
    for r in MATRIX_RUNNING:
        cells = []
        for k in MATRIX_PACKAGES:
            keep = choose(r, sample_package(k), KEEP, None, "FM-1_016" if r == STOCK else "")
            mb = choose(r, sample_package(k), MBOOT, None, "FM-1_016" if r == STOCK else "")
            cell = {"loader": "update loader", "download": "download mode", "refused": "refused"}[keep.method]
            if k != P_MBOOT and mb.method != keep.method:
                cell += f"; with m-bootloader: {mb.method} mode" if mb.method == "download" else ""
            cells.append(cell)
        rows.append(f"| {FIRMWARES[r][0]} | " + " | ".join(cells) + " |")
    return "\n".join(rows) + "\n"


def entry_markdown() -> str:
    rows = ["| running | soft key over USB-MIDI | keys |", "|---|---|---|"]
    rows += [f"| {FIRMWARES[r][0]} | {FIRMWARES[r][1]} | {FIRMWARES[r][2]} |" for r in FIRMWARES if r != LOADER]
    rows.append(f"| m-bootloader installed (any firmware) | - | {MBOOT_KEYS} |")
    return "\n".join(rows) + "\n"
