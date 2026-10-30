#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The installer's ways onto an FM-1 (docs/INSTALL-PATHS.md), on the host only: no USB, no MIDI device, no M-Flasher of
the real kind (tests/fake_mflasher.py stands in for it and records every command line).

  python3 tests/anyflash_test.py

- detection: the running firmware by its identity (and port), the package by its identity, SHA-256 and flash.bin;
- the path chooser and the docs' matrix (verbatim in docs/INSTALL-PATHS.md);
- the builder's BOOTLOADER: its display, the KEEP-only override, the sidecar's round trip;
- the layouts: every switch between two of them planned on a fake flash, the kept regions untouched;
- download mode through the fake M-Flasher: backup first, M-Flasher's plan compared, a typed yes, the model check,
  a mismatch or a failed write stopping it, m-bootloader once when the build says so;
- --dry-run: nothing run, the commands and the sectors printed; M-Flasher's command lines and its pinned release."""
import io
import json
import os
import shutil
import stat
import sys
from contextlib import redirect_stderr
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(1, str(ROOT / "tools" / "builder"))
import fm1_anyflash as AF  # noqa: E402
import fm1_install as FI  # noqa: E402
import fm1_layouts as L  # noqa: E402
import fm1_route as R  # noqa: E402
import fm1pkg_make as M  # noqa: E402
import install_meta as IM  # noqa: E402
import mflasher as MF  # noqa: E402

FAKE = ROOT / "tests" / "fake_mflasher.py"
TMP = ROOT / "build" / "test-anyflash"
fails = 0


def ok(cond, what):
    global fails
    print(f"{what:96} {'ok' if cond else 'FAIL'}")
    fails += not cond


# ------------------------------------------------------------ synthetic images ---

def synth_flash(end, seed):
    """a package's flash.bin [0, end): a head with the chip key 980F, an app area header whose block ends near `end`
    (fm1_rescue.installed_end reads it), the rest a pattern"""
    f = bytearray(((i * 31 + seed) >> 3) & 0xFF for i in range(end))
    f[0:0x4000] = b"\xFF" * 0x4000
    isd = M.key_blob(0x980F) + b"[TEST]\r\n"
    f[0x500:0x500 + len(isd)] = isd
    head = bytearray(M.entry(M.crc16(isd), 0x500, len(isd), 0x02, 0x80, 0, "isd_config.ini"))
    M.enc(head, 0, 32)
    f[32:64] = head
    app = bytearray(M.entry(0xFFFF, 0x4000, end - 0x4000 - 0x1000, 0x83, 0xFF, 0, "app_area_head"))
    M.sfc(app, 0, 32, 0, 0x980F)
    f[0x4000:0x4020] = app
    return bytes(f)


def unit_flash(end, seed=7):
    """the FM-1's whole flash with a firmware of that layout installed, user data everywhere above it, and each kept
    region holding its own pattern"""
    f = bytearray(L.FLASH_SIZE)
    f[0:end] = synth_flash(end, seed)
    for a in range(end, L.FLASH_SIZE):
        f[a] = (a >> 12) & 0xFF ^ 0x5A
    f[0:0x4000] = bytes([0xB7]) * 0x4000                 # (the unit's boot area: its own)
    return bytes(f)


def write_pkg(path, flash, product):
    Path(path).write_bytes(M.ufw(flash, b"FELUCCA-LOADER-1" + b"OTA" * 100, product))
    return Path(path)


# ------------------------------------------------------------------ detection ---

def detection():
    cases = {("FM-1_015", "FM-1"): R.STOCK, ("FM-1_016", ""): R.STOCK, ("FM-1_045", ""): R.BAUDGIRL,
             ("FM-1_906", "Felucca"): R.FELUCCA, ("FM-1_906", "SLOOP 2.7"): R.SLOOP, ("FM-1_700", "FM1-1A2B"): R.OPTIMIST,
             ("FM-1_731", ""): R.OPTIMIST, ("FM-1_000", ""): R.AMBIG_000, ("FM-1_7900", "FM1-1A2B"): R.FM1BASE,
             ("FM-1_79000200", ""): R.FM1BASE, ("FM-1_0000100", ""): R.FM1BASE, ("FM1_OFFICIAL_OPEN", ""): R.OPEN,
             ("FM1_COMMUNITY_3", ""): R.OPEN, ("FM-1_501", ""): R.OTHER, ("FM-1_701", ""): R.OTHER,
             ("ota-FM-1_700", ""): R.LOADER, (None, ""): R.NONE}
    bad = {k: R.running_kind(*k) for k, v in cases.items() if R.running_kind(*k) != v}
    ok(not bad, f"running firmware by identity and port: {len(cases)} cases" + (f" {bad}" if bad else ""))
    v15 = (ROOT / "web" / "stock" / "FM-1_015.fwsc").read_bytes()
    info = R.package_kind(v15, FI.flash_size(FI.logical_image(v15)))
    ok(info.kind == R.P_V15 and info.flash_len == 0x93000, "package: M-VAVE's V15 by its SHA-256 (web/stock)")
    mb = (ROOT / "web" / "fm1base" / "m-bootloader" / "fm1-uboot-1.2.fwsc").read_bytes()
    info = R.package_kind(mb, FI.flash_size(FI.logical_image(mb)))
    ok(info.kind == R.P_MBOOT and info.flash_len is None, "package: m-bootloader's installer FM-1_099 (no flash.bin)")
    kinds = {}
    for product, end in (("FM-1_700", 0x93000), ("FM-1_731", 0xD0000), ("FM-1_7900", 0x93000),
                         ("FM-1_79000200", 0xCE000), ("FM-1_906", 0x93000), ("FM-1_700", 0xCE000)):
        raw = M.ufw(synth_flash(end, 1), b"FELUCCA-LOADER-1" + b"x" * 64, product)
        kinds[(product, end)] = R.package_kind(raw, FI.flash_size(FI.logical_image(raw))).kind
    ok(kinds == {("FM-1_700", 0x93000): R.P_OPTIMIST, ("FM-1_731", 0xD0000): R.P_OPTIMIST,
                 ("FM-1_7900", 0x93000): R.P_FM1BASE, ("FM-1_79000200", 0xCE000): R.P_FM1BASE,
                 ("FM-1_906", 0x93000): R.P_FELUCCA, ("FM-1_700", 0xCE000): R.P_OTHER},
       f"package: Optimist's own by identity and layout, fm1-base, Felucca ({kinds})")


# ------------------------------------------------------------------- the chooser ---

def chooser():
    pk = R.sample_package
    table = [
        (R.OPTIMIST, R.P_OPTIMIST, R.KEEP, "loader"), (R.OPTIMIST, R.P_FM1BASE, R.KEEP, "download"),
        (R.OPTIMIST, R.P_V15, R.KEEP, "loader"), (R.OPTIMIST, R.P_MBOOT, R.KEEP, "download"),
        (R.FM1BASE, R.P_OPTIMIST, R.KEEP, "loader"), (R.FM1BASE, R.P_FM1BASE, R.KEEP, "loader"),
        (R.FM1BASE, R.P_MBOOT, R.KEEP, "download"), (R.STOCK, R.P_OPTIMIST, R.KEEP, "loader"),
        (R.STOCK, R.P_FM1BASE, R.KEEP, "loader"), (R.OPEN, R.P_OPTIMIST, R.KEEP, "download"),
        (R.OPEN, R.P_V15, R.KEEP, "loader"), (R.FELUCCA, R.P_OPTIMIST, R.KEEP, "loader"),
        (R.FELUCCA, R.P_FM1BASE, R.KEEP, "download"), (R.NONE, R.P_OPTIMIST, R.KEEP, "download"),
        (R.LOADER, R.P_OPTIMIST, R.KEEP, "loader"), (R.AMBIG_000, R.P_FM1BASE, R.KEEP, "download"),
        (R.OPTIMIST, R.P_OPTIMIST, R.MBOOT, "download"), (R.FM1BASE, R.P_FM1BASE, R.MBOOT, "download"),
        (R.STOCK, R.P_V15, R.MBOOT, "download"),
    ]
    bad = [(r, p, b, R.choose(r, pk(p), b).method) for r, p, b, want in table if R.choose(r, pk(p), b).method != want]
    ok(not bad, f"route per (running, package, BOOTLOADER): {len(table)} pairs" + (f" {bad}" if bad else ""))
    st = R.choose(R.STOCK, R.PackageInfo(R.P_V15, "FM-1_015", "", 0x93000, False), R.KEEP, None, "FM-1_015")
    ok(st.method == "download" and "refuses the identity it runs" in st.why, "V15 over V15: the stock updater refuses its own identity")
    r = R.choose(R.OPTIMIST, pk(R.P_FM1BASE))
    ok(r.soft_key and "soft key" in r.enter and not r.install_mboot, "Optimist running: the soft key, the bootloader kept")
    r = R.choose(R.SLOOP, pk(R.P_FM1BASE))
    ok(not r.soft_key and "OCT- + OCT+ held 5 s" in r.enter, "SLOOP: no known soft key, its key combo named")
    r = R.choose(R.NONE, pk(R.P_OPTIMIST), R.MBOOT, True)
    ok("REC while switching on" in r.enter and not any("m-bootloader into" in s for s in r.steps),
       "m-bootloader present: its REC key named, not installed again")
    r = R.choose(R.OPTIMIST, pk(R.P_OPTIMIST), R.MBOOT, None)
    ok(r.install_mboot and any("m-bootloader into the boot area" in s for s in r.steps), "BOOTLOADER m-bootloader: installed once, in the steps")
    # the display and the KEEP-only override
    lines = {(v, k): R.bootloader_choice(v, k) for v in (None, R.KEEP, R.MBOOT) for k in (False, True)}
    ok(lines[(R.MBOOT, False)][0] == R.MBOOT and "m-bootloader (from the build)" in lines[(R.MBOOT, False)][1]
       and "backup" in lines[(R.MBOOT, False)][1], "display: m-bootloader from the build, a backup first")
    ok(lines[(R.MBOOT, True)] [0] == R.KEEP and "--keep-bootloader" in lines[(R.MBOOT, True)][1], "override: m-bootloader -> keep")
    ok(all(lines[(v, k)][0] == R.KEEP for v in (None, R.KEEP) for k in (False, True)),
       "override never installs m-bootloader: keep and no setting stay keep, with or without the flag")
    ok("keep (from the build)" in lines[(R.KEEP, False)][1] and "no build setting" in lines[(None, False)][1],
       "display: keep from the build / no build setting")


def docs():
    text = (ROOT / "docs" / "INSTALL-PATHS.md").read_text()
    for name, part in (("matrix", R.matrix_markdown()), ("download-mode entry", R.entry_markdown()),
                       ("layouts", L.layouts_markdown()), ("layout switches", L.switches_markdown())):
        ok(part in text, f"docs/INSTALL-PATHS.md holds the {name} table as the code makes it")


# ---------------------------------------------------------------- the sidecar ---

def sidecar():
    d = TMP / "meta"
    d.mkdir(parents=True, exist_ok=True)
    pkg = write_pkg(d / "optimist-9.9.fwsc", synth_flash(0x93000, 3), "FM-1_700")
    ok(IM.read(pkg)[0] is None and "no build settings" in IM.read(pkg)[1], "no sidecar: no setting (keep)")
    side = IM.write(pkg, 1, "synths", "1a2b3c4d")
    meta = json.loads(side.read_text())
    ok(side.name == "optimist-9.9.install.json" and meta["bootloader"] == "m-bootloader" and IM.read(pkg)[0] == "m-bootloader",
       "PACKAGE.install.json: written and read back (m-bootloader)")
    IM.write(pkg, 0)
    ok(IM.read(pkg)[0] == "keep", "... and keep")
    IM.write(pkg, 1)
    pkg.write_bytes(pkg.read_bytes() + b"x")
    ok(IM.read(pkg)[0] is None and "another package" in IM.read(pkg)[1], "a sidecar for another package (SHA-256): ignored")
    ok(IM.from_config_text("# x\nBOOTLOADER=1\n") == "m-bootloader" and IM.from_config_text("ENG_FM6=1\n") == "keep",
       ".config text: BOOTLOADER=1 -> m-bootloader, absent -> keep")
    import registry as REG
    it = REG.ITEMS["BOOTLOADER"]
    ok(it.bit == 443 and it.no_image and it.default == 0 and dict(it.choices) == {0: "keep (as it is)", 1: "m-bootloader"}
       and it.group == "System", "registry: BOOTLOADER, bit 443, no image, keep by default, two choices")
    ok(len({i.bit for i in REG.ITEMS.values()}) == len(REG.ITEMS), "registry: no bit used twice")
    import core as C
    cfg = dict(C.defaults(), BOOTLOADER=1)
    ok(C.cfg_hash(cfg) == C.cfg_hash(C.defaults()), "BOOTLOADER changes no image hash")
    ok("BOOTLOADER=1" in C.dump(cfg) and C.parse(C.dump(cfg), warn=lambda _m: None)[0]["BOOTLOADER"] == 1,
       ".config: BOOTLOADER=1 dumped and parsed back")
    web = json.loads((ROOT / "web" / "builder" / "data" / "registry.json").read_text()) \
        if (ROOT / "web" / "builder" / "data" / "registry.json").exists() else None
    if web is not None:
        ok(any(i["key"] == "BOOTLOADER" and i["no_image"] for i in web["registry"]["items"]), "the web builder's registry has BOOTLOADER")
    src = (ROOT / "tools" / "build.py").read_text() + (ROOT / "tools" / "fm1base.py").read_text()
    ok(src.count("install_meta.write(") >= 2, "both builds write the sidecar beside the package")


# ------------------------------------------------------------------- layouts ---

ENDS = {name: end for name, _, end, _ in L.LAYOUTS}


def layouts():
    for n1, e1 in ENDS.items():
        for n2, e2 in ENDS.items():
            cur, pkg = unit_flash(e1), synth_flash(e2, 9)
            p = L.plan(cur, pkg)
            tgt = L.target_image(cur, pkg, p)
            kept = all(tgt[a:b] == cur[a:b] for a, b, _ in L.KEEP)
            right = tgt[0x4000:e2] == pkg[0x4000:e2]
            lo, hi = L._clear_range(e1, e2)
            cleared = tgt[lo:hi] == b"\xFF" * (hi - lo)
            above = max(e2, hi)
            rest = tgt[above:L.DATA_TOP] == cur[above:L.DATA_TOP]
            want = {(0x93000, 0xD0000): (0xD0000, 0xDA000), (0x93000, 0xCE000): (0xCE000, 0xDA000),
                    (0xD0000, 0x93000): (0x93000, 0xD0000), (0xCE000, 0x93000): (0x93000, 0xCE000),
                    (0xD0000, 0xCE000): (0xCE000, 0xD0000)}.get((e1, e2), (0, 0))
            lost_ok = (not p.lost) if e2 <= e1 else (p.lost and p.lost[0][0] == e1)
            ok(kept and right and cleared and rest and (lo, hi) == want and lost_ok and set(L.changed(cur, tgt)) == set(p.sectors),
               f"layout {n1} -> {n2}: package area, clear {lo:#x}-{hi:#x}, kept regions untouched, data above kept")
    try:
        L.plan(unit_flash(0x93000), synth_flash(0x93000, 1)[:0x3000] + b"\xFF" * 0x1000)
        ok(False, "a flash.bin ending at 0x4000 is refused")
    except L.PlanError:
        ok(True, "a flash.bin that ends inside the boot area is refused")


# ----------------------------------------------------------- download mode, faked ---

class Dev:
    """the fake M-Flasher's folder: the unit's flash, its state and the calls"""

    def __init__(self, name, flash, **state):
        self.d = TMP / name
        shutil.rmtree(self.d, ignore_errors=True)
        self.d.mkdir(parents=True)
        (self.d / "flash.bin").write_bytes(flash)
        st = {"download": False, "model": "FM-1", "bootloader": "stock (M-VAVE)", "firmware": "Optimist",
              "unit": "0123456789abcdef"}
        st.update(state)
        (self.d / "state.json").write_text(json.dumps(st))
        exe = self.d / "m-flasher"
        exe.write_text(f"#!/bin/sh\nFAKE_MF_DIR='{self.d}' FAKE_MF_TOOLS='{ROOT / 'tools'}' exec "
                       f"'{sys.executable}' '{FAKE}' \"$@\"\n")
        exe.chmod(exe.stat().st_mode | stat.S_IEXEC)
        self.exe = exe
        self.soft = 0

    @property
    def flash(self):
        return (self.d / "flash.bin").read_bytes()

    def calls(self):
        p = self.d / "calls.jsonl"
        return [json.loads(ln) for ln in p.read_text().splitlines()] if p.exists() else []

    def soft_key(self):
        self.soft += 1
        st = json.loads((self.d / "state.json").read_text())
        st["download"] = True
        (self.d / "state.json").write_text(json.dumps(st))
        return True

    def env(self, answers=("yes",)):
        out = io.StringIO()
        q = list(answers)
        e = AF.Env(exe=self.exe, out=out, ask=lambda _p: q.pop(0) if q else "", soft_key=self.soft_key,
                   wait_back=lambda prod: prod, sleep=lambda _s: None,
                   run=lambda args, answer=None, capture=True: MF.run(args, answer, True, on_line=lambda _l: None))
        return e, out


def flow(dev, route, pkg_path, flash, yes=False, answers=("yes", "yes", "yes")):
    e, out = dev.env(answers)
    err = None
    try:
        AF.download_install(e, route, str(pkg_path), flash, dev.d / "backups", yes, "FM-1_700")
    except AF.AnyError as x:
        err = x
    return err, out.getvalue()


def cmds(dev):
    return [c["args"][0] for c in dev.calls()]


def download_mode():
    pk = R.sample_package
    route = R.choose(R.OPTIMIST, pk(R.P_FM1BASE))
    # same layout: M-Flasher's firmware command
    cur = unit_flash(0x93000)
    new = synth_flash(0x93000, 21)
    pkg = write_pkg(TMP / "same.fwsc", new, "FM-1_7900")
    dev = Dev("same", cur)
    err, out = flow(dev, route, pkg, new)
    c = cmds(dev)
    want = L.target_image(cur, new, L.plan(cur, new))
    ok(err is None and dev.soft == 1 and c[:3] == ["find", "find", "info"] and "backup" in c
       and c.index("backup") < c.index("firmware"), "download: soft key, find, info, backup before any write")
    fw = [x for x in dev.calls() if x["args"][0] == "firmware"]
    ok(len(fw) == 2 and fw[0]["stdin"] == "no\n" and "--yes" not in fw[0]["args"] and "--yes" in fw[1]["args"]
       and "--restart" in fw[1]["args"], "firmware: M-Flasher's plan first (answered no), then the write with --yes --restart")
    bad_opts = {"--wipe-settings", "--with-bootloader", "--board"}
    ok(not any(bad_opts & set(x["args"]) for x in dev.calls()), "never --wipe-settings, --with-bootloader or --board")
    ok(dev.flash == want and dev.flash[:0x4000] == cur[:0x4000], "the unit's flash is the plan's; the boot area untouched")
    bk = sorted((dev.d / "backups").glob("fm1-*.bin"))
    ok(bk and bk[0].read_bytes() == cur and str(bk[0]) in out, "the backup is the flash before, its path printed")
    # a layout switch: restore of the planned flash
    cur, new = unit_flash(0x93000), synth_flash(0xCE000, 22)
    pkg = write_pkg(TMP / "switch.fwsc", new, "FM-1_79000200")
    dev = Dev("switch", cur)
    err, out = flow(dev, route, pkg, new)
    p = L.plan(cur, new)
    c = cmds(dev)
    ok(err is None and "restore" in c and "firmware" not in c and dev.flash == L.target_image(cur, new, p)
       and dev.flash[0xCE000:0xDA000] == b"\xFF" * 0xC000, "normal -> 0xCE000: restore of the planned flash, the squeezed store cleared")
    ok(all(dev.flash[a:b] == cur[a:b] for a, b, _ in L.KEEP), "... kept regions untouched on the unit")
    t = [x for x in dev.calls() if x["args"][0] == "restore"][0]["args"][1]
    ok(json.loads(Path(t).with_suffix(".json").read_text()).get("unit id") == "0123456789abcdef",
       "the planned image carries the backup's unit id (M-Flasher restores it whole)")
    # the user says no
    dev = Dev("no", unit_flash(0x93000))
    err, out = flow(dev, route, write_pkg(TMP / "n.fwsc", synth_flash(0x93000, 5), "FM-1_7900"), synth_flash(0x93000, 5),
                    answers=("no",))
    ok(err is not None and err.code == "cancel" and dev.flash == unit_flash(0x93000)
       and not any("--yes" in x["args"] for x in dev.calls()), "typed no: nothing written, no --yes ever sent")
    # model check
    dev = Dev("smk", unit_flash(0x93000), model="SMK-37 Pro / Elite")
    err, out = flow(dev, route, pkg, new)
    ok(err is not None and err.code == "model" and "backup" not in cmds(dev), "an SMK-37 (or an uncertain model): stops before the backup")
    # M-Flasher plans another sector than we do
    dev = Dev("mismatch", unit_flash(0x93000), extra=0xC0000)
    nf = synth_flash(0x93000, 6)
    err, out = flow(dev, route, write_pkg(TMP / "m.fwsc", nf, "FM-1_7900"), nf)
    ok(err is not None and err.code == "planmismatch" and dev.flash == unit_flash(0x93000), "plans disagree: nothing written")
    # the write fails
    dev = Dev("fail", unit_flash(0x93000), fail="restore")
    err, out = flow(dev, route, pkg, new)
    ok(err is not None and dev.flash == unit_flash(0x93000), "M-Flasher refuses: stops, the flash as it was")
    # m-bootloader from the build: once, after the backup, then a new backup, then the firmware
    mb = R.choose(R.OPTIMIST, pk(R.P_OPTIMIST), R.MBOOT, None)
    nf = synth_flash(0x93000, 8)
    dev = Dev("mboot", unit_flash(0x93000))
    err, out = flow(dev, mb, write_pkg(TMP / "mb.fwsc", nf, "FM-1_700"), nf)
    c = cmds(dev)
    ok(err is None and c.count("bootloader") == 1 and c.index("backup") < c.index("bootloader")
       and c.count("backup") == 2 and c.index("bootloader") < c.index("firmware") and dev.flash[0] == 0xB0,
       "m-bootloader: backup, bootloader (boot area), a new backup, then the firmware")
    dev = Dev("mboot2", unit_flash(0x93000), bootloader="m-bootloader 1.2-22e2559 (fm1)")
    err, out = flow(dev, mb, write_pkg(TMP / "mb.fwsc", nf, "FM-1_700"), nf)
    ok(err is None and "bootloader" not in cmds(dev) and "found: kept" in out, "m-bootloader already there: not written again")
    dev = Dev("keep", unit_flash(0x93000))
    err, out = flow(dev, route, write_pkg(TMP / "k.fwsc", nf, "FM-1_7900"), nf)
    ok(err is None and "bootloader" not in cmds(dev) and dev.flash[:0x4000] == unit_flash(0x93000)[:0x4000],
       "BOOTLOADER keep: the boot area is never written")


def dry_runs():
    pkg = write_pkg(TMP / "dry.fwsc", synth_flash(0xCE000, 30), "FM-1_79000200")
    dev = Dev("dry", unit_flash(0x93000))
    os.environ["M_FLASHER"] = str(dev.exe)
    img = TMP / "dry-flash.bin"
    img.write_bytes(unit_flash(0x93000))
    out, err = io.StringIO(), io.StringIO()
    with redirect_stderr(err):
        rc = FI.main([str(pkg), "--dry-run", "--running", "FM-1_700", "--flash-image", str(img)], out=out)
    o = out.getvalue()
    ok(rc == 0 and not dev.calls() and dev.soft == 0, "--dry-run: no MIDI backend needed, M-Flasher never run")
    ok("route:    download" in o and f"{dev.exe} backup" in o and "firmware PACKAGE" in o, "--dry-run: the route and M-Flasher's command lines")
    ok("0x93000" in o and "big (0xCE000)" in o and "user data overwritten" in o and "restore of the planned flash" in o,
       "--dry-run --flash-image: the exact sectors of a layout switch, the data it overwrites")
    pkg2 = write_pkg(TMP / "dry2.fwsc", synth_flash(0x93000, 31), "FM-1_700")
    out = io.StringIO()
    with redirect_stderr(io.StringIO()):
        rc = FI.main([str(pkg2), "--dry-run", "--running", "FM-1_700"], out=out)
    ok(rc == 0 and "route:    loader" in out.getvalue() and "the bootloader is not touched" in out.getvalue(),
       "--dry-run, Optimist over Optimist: the running firmware's loader, what it writes")
    IM.write(pkg2, 1)
    out = io.StringIO()
    with redirect_stderr(io.StringIO()):
        FI.main([str(pkg2), "--dry-run", "--running", "FM-1_700"], out=out)
    o = out.getvalue()
    ok("bootloader: m-bootloader (from the build)" in o and "route:    download" in o and "bootloader --backup-dir" in o,
       "--dry-run with BOOTLOADER m-bootloader: shown, download mode, the bootloader step")
    out = io.StringIO()
    with redirect_stderr(io.StringIO()):
        FI.main([str(pkg2), "--dry-run", "--running", "FM-1_700", "--keep-bootloader"], out=out)
    o = out.getvalue()
    ok("bootloader: keep (--keep-bootloader" in o and "route:    loader" in o, "--keep-bootloader: keep shown, the loader again")
    out = io.StringIO()
    AF.main(["backup", "--dry-run", "--backup-dir", str(TMP / "b")], AF.Env(out=out))
    ok(f"{dev.exe} backup {TMP / 'b'}" in out.getvalue() and not dev.calls(), "fm1_anyflash.py backup --dry-run: the command only")
    del os.environ["M_FLASHER"]


def settings():
    pk = lambda product, kind, end=0x93000: R.PackageInfo(kind, product, "", end, True)
    own, rel, v15 = pk("FM-1_700", R.P_OPTIMIST), pk("FM-1_79000200", R.P_FM1BASE, 0xCE000), pk("FM-1_015", R.P_V15)
    table = [("FM-1_700", own, "keep", False), ("FM-1_731", pk("FM-1_700", R.P_OPTIMIST, 0xD0000), "keep", False),
             ("FM-1_7900", rel, "keep", False), ("FM-1_700", rel, "keep", True), ("FM-1_79000200", own, "keep", True),
             ("FM-1_015", own, "wipe", True), ("FM-1_906", own, "wipe", True), ("FM-1_700", v15, "wipe", True),
             (None, own, "wipe", True)]
    bad = [(r, p.product) for r, p, d, a in table if (R.settings_policy(r, p).default, R.settings_policy(r, p).ask) != (d, a)]
    ok(not bad, f"settings policy per (running, package): {len(table)} pairs" + (f" {bad}" if bad else ""))
    ok(R.settings_policy("FM-1_700", v15).target == "stock" and R.settings_policy("FM-1_015", own).target == "optimist",
       "a wipe erases the new firmware's settings: stock's BTIF, Optimist's record")
    pol = R.settings_policy("FM-1_015", own)
    ok(R.settings_choice(pol, "keep")[0] == "keep" and R.settings_choice(pol, None, False)[0] == "keep"
       and R.settings_choice(pol, None)[0] == "wipe", "--keep-settings and a no answer keep; the default erases here")
    # the wipe on a fake flash: Optimist's record only, the rest kept
    cur, new = unit_flash(0x93000), synth_flash(0x93000, 50)
    pkg = write_pkg(TMP / "wipe.fwsc", new, "FM-1_700")
    dev = Dev("wipe", cur)
    e, out = dev.env(("yes",))
    AF.download_install(e, R.Route("download", "test", "x", True), str(pkg), new, dev.d / "b", False, "FM-1_700",
                        "optimist")
    f = dev.flash
    keep_ok = all(f[a:b] == cur[a:b] for a, b, _ in L.KEEP if (a, b) != (0xFC000, 0xFF000))
    ok(f[0xFC000:0xFE000] == b"\xFF" * 0x2000 and f[0xFE000:0xFF000] == cur[0xFE000:0xFF000] and keep_ok
       and "restore" in cmds(dev) and not any("--wipe-settings" in x["args"] for x in dev.calls()),
       "wipe: 0xFC000-0xFDFFF erased by the planned restore, autosave B and every other kept region untouched")
    dev = Dev("nowipe", cur)
    e, out = dev.env(("yes",))
    AF.download_install(e, R.Route("download", "test", "x", True), str(pkg), new, dev.d / "b", False, "FM-1_700")
    ok(all(dev.flash[a:b] == cur[a:b] for a, b, _ in L.KEEP) and "firmware" in cmds(dev),
       "Optimist -> Optimist: settings, pairings, RF trims, system flash untouched; M-Flasher's firmware command, no wipe")
    out = io.StringIO()
    with redirect_stderr(io.StringIO()):
        FI.main([str(pkg), "--dry-run", "--running", "FM-1_015", "--route", "download"], out=out)
    ok("settings: erased" in out.getvalue(), "--dry-run, V15 -> Optimist in download mode: the erase shown as the default")
    out = io.StringIO()
    with redirect_stderr(io.StringIO()):
        FI.main([str(pkg), "--dry-run", "--running", "FM-1_015", "--route", "download", "--keep-settings"], out=out)
    ok("settings: kept, --keep-settings" in out.getvalue(), "... --keep-settings: kept")
    out = io.StringIO()
    with redirect_stderr(io.StringIO()):
        FI.main([str(pkg), "--dry-run", "--running", "FM-1_700"], out=out)
    ok("settings: kept (the running firmware's update loader" in out.getvalue(), "the loader's way: the settings kept, said")


def js_route():
    """web/install_route.js chooses as fm1_route.py does, for every pair of the matrix and both BOOTLOADER values"""
    node = shutil.which("node")
    if not node:
        print("web/install_route.js against fm1_route.py: skipped (no node)")
        return
    import subprocess
    cases = [(r, k, b) for r in R.MATRIX_RUNNING + (R.LOADER, R.OTHER) for k in R.MATRIX_PACKAGES for b in (R.KEEP, R.MBOOT)]
    pkgs = {k: R.sample_package(k).__dict__ for k in R.MATRIX_PACKAGES}
    js = ("import { chooseRoute as choose, runningKind, bootloaderChoice, bootloaderFromConfig } from %r;"
          "const [cases, pkgs] = JSON.parse(process.argv[1]);"
          "const out = cases.map(([r, k, b]) => { const p = pkgs[k]; return choose(r, { kind: p.kind, product: p.product, "
          "flashLen: p.flash_len, ownLoader: p.own_loader }, b, r === 'stock' ? 'FM-1_016' : '').method; });"
          "const ids = JSON.parse(process.argv[2]).map(([i, port]) => runningKind(i, port));"
          "const boots = [[null, false], ['keep', true], ['m-bootloader', false], ['m-bootloader', true]].map(([v, k]) => bootloaderChoice(v, k).choice);"
          "process.stdout.write(JSON.stringify([out, ids, boots, bootloaderFromConfig('X=1\\nBOOTLOADER=1\\n')]));") % str(ROOT / "web" / "install_route.js")
    ids = [("FM-1_015", ""), ("FM-1_906", "SLOOP"), ("FM-1_700", ""), ("FM-1_7900", ""), ("FM-1_000", ""), ("ota-FM-1_700", ""),
           ("FM1_OFFICIAL_OPEN", ""), ("FM-1_045", ""), (None, ""), ("FM-1_501", "")]
    r = subprocess.run([node, "--input-type=module", "-e", js, json.dumps([cases, pkgs]), json.dumps(ids)],
                       capture_output=True, text=True)
    if r.returncode:
        ok(False, f"web/install_route.js runs ({r.stderr.strip()[:200]})")
        return
    out, kinds, boots, cfg = json.loads(r.stdout)
    want = [R.choose(rk, R.sample_package(k), b, None, "FM-1_016" if rk == R.STOCK else "").method for rk, k, b in cases]
    diff = [c for c, a, b in zip(cases, out, want) if a != b]
    ok(not diff, f"web/install_route.js chooses as fm1_route.py: {len(cases)} cases" + (f" {diff[:5]}" if diff else ""))
    ok(kinds == [R.running_kind(i, p) for i, p in ids], "... and names the running firmware the same")
    ok(boots == ["keep", "keep", "m-bootloader", "keep"] and cfg == "m-bootloader", "... and takes BOOTLOADER the same (KEEP-only override)")


def flash_py():
    import flash as F
    pkg = write_pkg(TMP / "optimist-8.8.fwsc", synth_flash(0x93000, 40), "FM-1_700")
    IM.write(pkg, 1)
    seen = []

    def fake_tool(args, capture=True, on_line=None, tool=None):
        seen.append(list(args))
        return (0, "FM-1_700  [running]  port: FM1-1A2B") if "--info" in args else (0, "done")
    real, F.run_tool = F.run_tool, fake_tool
    try:
        plan = F.preflight(pkg, notes=[])
        text = "\n".join(plan.lines())
        ok("bootloader: m-bootloader (from the build)" in text and plan.download and "route:    download mode" in text,
           "make flash / the builder's f: the confirmation shows the bootloader and the route")
        F.install(plan)
        ok(seen[-1][:2] == [str(pkg), "--yes"] and "--route" in seen[-1] and "--keep-bootloader" not in seen[-1]
           and "--wipe-settings" not in seen[-1] and "settings: kept" in text,
           "... the install runs fm1_install.py --yes --route download")
        kept = F.keep_bootloader(plan)
        F.install(kept)
        ok(kept.keep_bootloader and not kept.download and "--keep-bootloader" in seen[-1] and "--route" not in seen[-1],
           "the builder's 'keep the bootloader this time': --keep-bootloader, the loader again")
        IM.write(pkg, 0)
        plan = F.preflight(pkg, notes=[])
        ok("bootloader: keep (from the build)" in "\n".join(plan.lines()) and not plan.download,
           "BOOTLOADER keep: shown, the running firmware's loader")
    finally:
        F.run_tool = real


def mflasher():
    a = MF.argv("/x/m-flasher", "firmware", arg="p.fwsc", backup_dir="/b", yes=True, restart=True)
    ok(a == ["/x/m-flasher", "firmware", "p.fwsc", "--backup-dir", "/b", "--yes", "--restart"], "argv: firmware")
    ok(MF.argv("m", "bootloader", backup_dir="/b") == ["m", "bootloader", "--backup-dir", "/b"]
       and MF.argv("m", "backup", arg="/d") == ["m", "backup", "/d"], "argv: bootloader, backup")
    for bad in (lambda: MF.argv("m", "run-ram"), lambda: MF.argv("m", "info", restart=True), lambda: MF.argv("m", "restore")):
        try:
            bad()
            ok(False, "argv refuses run-ram, --restart on info, restore without a file")
        except MF.MFlasherError:
            pass
    ok(True, "argv refuses run-ram, --restart on info, restore without a file")
    info = MF.parse_info("Connecting to /dev/sg2\n  model       FM-1\n  bootloader  m-bootloader 1.2-22e2559 (fm1)\n"
                         "  chip key    0x980F\n    * the installed bootloader is built for the FM-1\n")
    ok(info == {"model": "FM-1", "bootloader": "m-bootloader 1.2-22e2559 (fm1)", "chip key": "0x980F"}, "parse_info")
    ok(MF.backup_path("x\nBackup saved: /h/fm1-20261030.bin\n") == "/h/fm1-20261030.bin", "backup_path")
    ok(AF.mf_plan_sectors("Writes 3 sectors (12 KiB): 0x04000-0x05FFF, 0x80000-0x80FFF") == (0x4000, 0x5000, 0x80000)
       and AF.mf_plan_sectors("Nothing to write: the flash already holds this.") == () and AF.mf_plan_sectors("?") is None,
       "M-Flasher's plan line read back")
    ok(MF.asset("Darwin", "arm64")[0].endswith("macos.dmg") and MF.asset("Linux", "amd64")[0].endswith("x86_64.tar.gz")
       and MF.asset("Windows", "AMD64")[2] == "M-Flasher.exe" and MF.asset("FreeBSD", "x86_64") is None,
       "the pinned release per system")
    ok(not MF.check_mboot_copies(), "web/fm1base/m-bootloader and M-Flasher's bundled build are m-bootloader 1.2's release files")
    home = TMP / "mfhome"
    shutil.rmtree(home, ignore_errors=True)
    os.environ["M_FLASHER_HOME"] = str(home)
    try:
        MF.fetch(download=lambda url, dest: Path(dest).write_bytes(b"not the release"))
        ok(False, "a download that does not match its SHA-256 installs nothing")
    except MF.MFlasherError:
        ok(not any(home.rglob("M-Flasher*")) and not any(home.rglob("m-flasher")), "a download that does not match its SHA-256 installs nothing")
    del os.environ["M_FLASHER_HOME"]
    sub = (ROOT / ".gitmodules").read_text()
    ok("third_party/m-flasher" in sub and "third_party/m-bootloader" in sub, ".gitmodules: m-flasher and m-bootloader")


if __name__ == "__main__":
    shutil.rmtree(TMP, ignore_errors=True)
    TMP.mkdir(parents=True)
    for t in (detection, chooser, docs, sidecar, layouts, download_mode, dry_runs, settings, js_route, flash_py, mflasher):
        t()
    shutil.rmtree(TMP, ignore_errors=True)
    print("anyflash tests passed" if not fails else f"ANYFLASH TESTS FAILED ({fails})")
    sys.exit(1 if fails else 0)
