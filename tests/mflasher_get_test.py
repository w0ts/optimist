#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""M-Flasher fetched for the machine it runs on, and linked for the others (no real download: a local HTTP server and fake
callables stand in for github.com).

  python3 tests/mflasher_get_test.py

- the platform picking: every OS / CPU -> its release file, the question's words;
- the builder's fetch: SHA-256 good (installed in its pinned place), bad (nothing written), offline (a clear message,
  nothing written), a missing binary inside;
- the "ask once" flow: found = no question; declined = nothing; accepted = fetched; --yes downloads nothing; one question per run;
- the web installer's link per user agent (web/install_route.js, node) and the page's wiring (make_site.py);
- the Mirror app's link (the Swift side is Tests/FlashTests.swift; here its data and build phase);
- one source of truth: the web's site.json and the app's mflasher.json are tools/mflasher.py's data, and no other
  tracked file carries a pinned SHA-256."""
import hashlib
import http.server
import io
import json
import os
import platform
import shutil
import subprocess
import sys
import threading
import zipfile
from pathlib import Path

from builder_path import ROOT  # (first: tools/ and tools/builder/ on sys.path for the imports below)

import fm1_anyflash as AF
import flash as FL
import mflasher as MF
import shared

TMP = ROOT / "build" / "test-mflasher-get"
fails = 0


def ok(cond, what):
    global fails
    print(f"{what:100} {'ok' if cond else 'FAIL'}")
    fails += not cond


class Patched:
    """module attributes replaced for a block and put back (no mocking library: the tests are plain scripts)"""

    def __init__(self, *pairs):
        self.pairs = pairs
        self.saved = []

    def __enter__(self):
        for obj, name, value in self.pairs:
            self.saved.append((obj, name, getattr(obj, name)))
            setattr(obj, name, value)

    def __exit__(self, *_):
        for obj, name, value in reversed(self.saved):
            setattr(obj, name, value)


def windows_zip(with_exe=True):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        z.writestr("M-Flasher.exe" if with_exe else "readme.txt", b"MZ fake")
    return buf.getvalue()


class Server:
    """a local file server standing in for the release downloads: serves {name: bytes}, counts the requests"""

    def __init__(self, files):
        outer = self
        self.hits = []

        class H(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                outer.hits.append(self.path)
                data = files.get(self.path.rsplit("/", 1)[-1])
                self.send_response(200 if data is not None else 404)
                self.end_headers()
                if data is not None:
                    self.wfile.write(data)

            def log_message(self, *_):
                pass
        self.httpd = http.server.HTTPServer(("127.0.0.1", 0), H)
        self.url = f"http://127.0.0.1:{self.httpd.server_port}/"
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()

    def close(self):
        self.httpd.shutdown()
        self.httpd.server_close()


def as_windows(sha, base):
    """a Windows x86_64 host whose pinned zip is the given bytes' hash, fetched from base"""
    asset = ("M-Flasher-0.7.0-windows.zip", sha, "M-Flasher.exe")
    return Patched((MF.platform, "system", lambda: "Windows"), (MF.platform, "machine", lambda: "AMD64"),
                   (MF, "ASSETS", {**MF.ASSETS, ("Windows", "any"): asset}), (MF, "RELEASE", base),
                   (MF, "binary", lambda: None))


# ---------------------------------------------------------------- the platform picking ---

def picking():
    want = {("Darwin", "arm64"): "macos.dmg", ("Darwin", "x86_64"): "macos.dmg", ("Windows", "AMD64"): "windows.zip",
            ("Windows", "ARM64"): "windows.zip", ("Linux", "x86_64"): "linux-x86_64.tar.gz",
            ("Linux", "amd64"): "linux-x86_64.tar.gz", ("Linux", "aarch64"): "linux-aarch64.tar.gz",
            ("Linux", "arm64"): "linux-aarch64.tar.gz"}
    for (system, machine), tail in want.items():
        a = MF.asset(system, machine)
        ok(a is not None and a[0].endswith(tail) and MF.pick(system, machine)["name"] == a[0], f"{system} {machine} -> {tail}")
    ok(MF.asset("FreeBSD", "x86_64") is None and MF.asset("Linux", "riscv64") is None and MF.pick("Linux", "armv7l") is None,
       "no release for FreeBSD, riscv64, armv7l: None")
    ok(MF.question("Darwin", "arm64") == "M-Flasher (MIT, Julius Vitkauskas) is needed for this install: download v0.7.0 "
                                         "for macOS arm64 (7.4 MB)?", "the question: macOS arm64, version, size")
    ok("Linux x86_64 (4.0 MB)" in MF.question("Linux", "amd64") and "Windows x86_64 (3.1 MB)" in MF.question("Windows", "AMD64"),
       "the question names the OS, CPU and size of the file it would fetch")
    try:
        MF.question("FreeBSD", "x86_64")
        ok(False, "no question for a system with no release")
    except MF.MFlasherError as e:
        ok("no release for FreeBSD" in str(e), "no question for a system with no release: says so")
    names = [f["name"] for f in MF.RELEASE_FILES]
    ok(len(set(names)) == len(names) == 8 and all(len(f["sha256"]) == 64 and f["size"] > 1e6 for f in MF.RELEASE_FILES),
       "the release table: 8 files, each with a SHA-256 and a size")
    ok(any("macOS" in n for n in MF.notes("Darwin")[0:1]) and "Open Anyway" in MF.notes("Darwin")[0]
       and "password" in MF.notes("Darwin")[1] and MF.notes("Linux") == [], "macOS first-run notes (Gatekeeper, root helper) are said, not bypassed")
    ok(MF.asset("Darwin", "arm64")[2] == "M-Flasher.app/Contents/MacOS/M-Flasher",
       "macOS: the dmg app's bundled CLI is the binary that runs")


# ---------------------------------------------------------------- the fetch ---

def fetching():
    saved = os.environ.pop("M_FLASHER_HOME", None)
    try:
        d = MF.home()
    finally:
        if saved is not None:
            os.environ["M_FLASHER_HOME"] = saved
    ok(d == (shared.main_checkout(MF.ROOT) or MF.ROOT) / "build" / "m-flasher" / "m-flasher-0.7.0" and Path.home() / ".jieli" not in d.parents,
       "the fetched release stays inside the repo (build/m-flasher of the main checkout, git-ignored), never in the user's home")
    data = windows_zip()
    sha = hashlib.sha256(data).hexdigest()
    srv = Server({"M-Flasher-0.7.0-windows.zip": data})
    home = TMP / "home-good"
    os.environ["M_FLASHER_HOME"] = str(home)
    try:
        with as_windows(sha, srv.url):
            exe = MF.fetch()
        ok(exe == home / "m-flasher-0.7.0" / "M-Flasher.exe" and exe.read_bytes() == b"MZ fake" and len(srv.hits) == 1,
           "good SHA-256 (local server): installed in its pinned place")
        with as_windows(sha, srv.url):
            MF.fetch()
        ok(len(srv.hits) == 1, "...and kept: a second fetch downloads nothing")
        ok([p.name for p in home.iterdir()] == ["m-flasher-0.7.0"], "...and no temporary file is left beside it")

        home = TMP / "home-bad"
        os.environ["M_FLASHER_HOME"] = str(home)
        with as_windows("0" * 64, srv.url):
            try:
                MF.fetch()
                ok(False, "bad SHA-256 installs nothing")
            except MF.MFlasherError as e:
                ok("SHA-256" in str(e) and "nothing installed" in str(e) and not list(home.rglob("*.exe")) and
                   not (home / "m-flasher-0.7.0").exists(), "bad SHA-256: a clear message, nothing installed")

        home = TMP / "home-noexe"
        os.environ["M_FLASHER_HOME"] = str(home)
        bad_inside = windows_zip(with_exe=False)
        srv2 = Server({"M-Flasher-0.7.0-windows.zip": bad_inside})
        with as_windows(hashlib.sha256(bad_inside).hexdigest(), srv2.url):
            try:
                MF.fetch()
                ok(False, "an archive without the binary installs nothing")
            except MF.MFlasherError as e:
                ok("no M-Flasher.exe inside" in str(e) and not (home / "m-flasher-0.7.0").exists(),
                   "an archive without the binary: refused, nothing installed")
        srv2.close()

        home = TMP / "home-404"
        os.environ["M_FLASHER_HOME"] = str(home)
        srv3 = Server({})
        with as_windows(sha, srv3.url):
            try:
                MF.fetch()
                ok(False, "a missing file installs nothing")
            except MF.MFlasherError as e:
                ok("cannot download" in str(e) and not (home / "m-flasher-0.7.0").exists(), "a 404: a clear message, nothing installed")
        srv3.close()
    finally:
        srv.close()

    home = TMP / "home-offline"
    os.environ["M_FLASHER_HOME"] = str(home)
    with as_windows(sha, "http://127.0.0.1:9/"):                     # (the discard port: refused at once)
        try:
            MF.fetch()
            ok(False, "offline installs nothing")
        except MF.MFlasherError as e:
            ok("cannot download" in str(e) and "offline" in str(e) and "nothing installed" in str(e)
               and not (home / "m-flasher-0.7.0").exists() and not list(home.rglob("*.part")), "offline: a clear message, nothing written")
    del os.environ["M_FLASHER_HOME"]


# ---------------------------------------------------------------- ask once ---

def asking():
    data = windows_zip()
    sha = hashlib.sha256(data).hexdigest()
    srv = Server({"M-Flasher-0.7.0-windows.zip": data})
    os.environ["M_FLASHER_HOME"] = str(TMP / "home-ask")
    try:
        questions, said = [], []

        def yes(q):
            questions.append(q)
            return True

        def no(q):
            questions.append(q)
            return False
        with as_windows(sha, srv.url):
            with Patched((MF, "binary", lambda: Path("/already/here/m-flasher"))):
                ok(MF.ensure(yes, said.append) == Path("/already/here/m-flasher") and not questions, "found: no question, no download")
            try:
                MF.ensure(no, said.append)
                ok(False, "declined")
            except MF.MFlasherError as e:
                ok(len(questions) == 1 and "was not downloaded" in str(e) and not srv.hits, "declined: one question, nothing downloaded")
            exe = MF.ensure(yes, said.append)
            ok(exe.is_file() and len(srv.hits) == 1 and questions[-1].startswith("M-Flasher (MIT, Julius Vitkauskas) is needed")
               and any("installed" in s for s in said), "accepted: fetched, checked, installed, the path said")
            # the installer's flow: one question per run, however often M-Flasher is needed
            shutil.rmtree(TMP / "home-ask")
            env = AF.Env(out=io.StringIO(), may_fetch=no)
            for _ in range(2):
                try:
                    AF.need_mflasher(env)
                except AF.AnyError as e:
                    code = e.code
            ok(code == "nomf" and len(questions) == 3, "the installer asks ONCE per run (a second need does not ask again)")
            env = AF.Env(out=io.StringIO(), may_fetch=None)
            try:
                AF.need_mflasher(env)
                ok(False, "no may_fetch (--yes)")
            except AF.AnyError as e:
                ok(e.code == "nomf" and "fm1_anyflash.py setup" in str(e) and len(questions) == 3 and len(srv.hits) == 1,
                   "--yes (no may_fetch): downloads nothing, says to run setup")
            out = io.StringIO()
            env = AF.Env(out=out, may_fetch=yes)
            AF.need_mflasher(env)
            ok(env.exe and env.exe.is_file() and f"M-Flasher: {env.exe}" in out.getvalue(), "accepted in the installer: the binary that runs is said")
            # the builder's side (tools/flash.py): the plan is not download mode -> nothing; download mode -> asked
            plan = type("P", (), {"download": False})()
            ok(FL.mflasher_question(plan) is None and FL.ensure_mflasher(plan, no) is None, "builder: a loader install needs no M-Flasher")
        with as_windows(sha, srv.url):
            shutil.rmtree(TMP / "home-ask", ignore_errors=True)
            plan = type("P", (), {"download": True})()
            ok(FL.mflasher_question(plan).startswith("M-Flasher (MIT"), "builder: a download-mode install without M-Flasher has its question")
            try:
                FL.ensure_mflasher(plan, no)
                ok(False, "builder declined")
            except FL.FlashError as e:
                ok("not downloaded" in str(e), "builder: declined -> FlashError, nothing written")
            hits = len(srv.hits)
            exe = FL.ensure_mflasher(plan, yes)
            ok(exe.is_file() and len(srv.hits) == hits + 1, "builder: accepted -> fetched into its pinned place")
        with Patched((MF, "binary", lambda: None), (MF.platform, "system", lambda: "FreeBSD")):
            try:
                FL.mflasher_question(plan)
                ok(False, "no release")
            except FL.FlashError as e:
                ok("no release for FreeBSD" in str(e), "builder: a system with no release -> FlashError saying so")
    finally:
        srv.close()
        del os.environ["M_FLASHER_HOME"]


# ---------------------------------------------------------------- the web ---

UAS = {
    "mac": ({"userAgent": "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 Chrome/130.0 Safari/537.36",
             "userAgentData": {"platform": "macOS"}}, {"architecture": "arm", "bitness": "64"}, "macos", "aarch64", ["macos.dmg"]),
    "mac-ua-only": ({"userAgent": "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) Safari/605.1"}, None, "macos", "x86_64", ["macos.dmg"]),
    "win": ({"userAgent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/130.0", "userAgentData": {"platform": "Windows"}},
            {"architecture": "x86"}, "windows", "x86_64", ["windows.zip"]),
    "win-ua-only": ({"userAgent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Gecko/20100101 Firefox/130.0"}, None, "windows", "x86_64",
                    ["windows.zip"]),
    "linux-x86": ({"userAgent": "Mozilla/5.0 (X11; Linux x86_64) Gecko/20100101 Firefox/130.0"}, None, "linux", "x86_64",
                  ["_amd64.deb", ".x86_64.rpm", "linux-x86_64.tar.gz"]),
    "linux-arm-hint": ({"userAgent": "Mozilla/5.0 (X11; Linux x86_64) Chrome/130.0", "userAgentData": {"platform": "Linux"}},
                       {"architecture": "arm", "bitness": "64"}, "linux", "aarch64", ["_arm64.deb", ".aarch64.rpm", "linux-aarch64.tar.gz"]),
    "linux-arm-ua": ({"userAgent": "Mozilla/5.0 (X11; Linux aarch64) Gecko/20100101 Firefox/130.0"}, None, "linux", "aarch64",
                     ["_arm64.deb", ".aarch64.rpm", "linux-aarch64.tar.gz"]),
    "linux-unknown-cpu": ({"userAgent": "Mozilla/5.0 (X11; Linux) Gecko/20100101"}, None, "linux", "",
                          ["_amd64.deb", ".x86_64.rpm", "linux-x86_64.tar.gz", "_arm64.deb", ".aarch64.rpm", "linux-aarch64.tar.gz"]),
    "iphone": ({"userAgent": "Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15 Safari/604.1"}, None,
               "ios", "", []),
    "ipad-desktop": ({"userAgent": "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) Safari/605.1", "maxTouchPoints": 5}, None, "ios", "x86_64", []),
    "android": ({"userAgent": "Mozilla/5.0 (Linux; Android 14; Pixel 8) Chrome/130.0 Mobile", "userAgentData": {"platform": "Android"}},
                None, "android", "", []),
    "other": ({"userAgent": "SomethingElse/1.0"}, None, "other", "", []),
}


def web_links():
    node = shutil.which("node")
    if not node:
        print("web link per user agent: skipped (no node)")
        return
    js = ("import { detectPlatform, mflasherFor } from %r;"
          "const [cases, data] = JSON.parse(process.argv[1]);"
          "process.stdout.write(JSON.stringify(Object.entries(cases).map(([k, [nav, high]]) => { const p = detectPlatform(nav, high); "
          "const v = mflasherFor(data, p); return [k, p.os, p.arch, v.mobile, v.files.map(f => f.name), v.files.map(f => f.url), "
          "v.files.map(f => f.sha256), v.page]; })));") % str(ROOT / "web" / "install_route.js")
    cases = {k: [v[0], v[1]] for k, v in UAS.items()}
    r = subprocess.run([node, "--input-type=module", "-e", js, json.dumps([cases, MF.site_data()])], capture_output=True, text=True)
    if r.returncode:
        ok(False, f"web/install_route.js runs ({r.stderr.strip()[:300]})")
        return
    by = {row[0]: row for row in json.loads(r.stdout)}
    for k, (_, _, os_, arch, tails) in UAS.items():
        _, got_os, got_arch, mobile, names, urls, shas, page = by[k]
        right = (got_os, got_arch) == (os_, arch) and len(names) == len(tails) and all(n.endswith(t) for n, t in zip(names, tails))
        right = right and mobile == (os_ in ("ios", "android")) and page == MF.PAGE
        right = right and all(u == MF.RELEASE + n for u, n in zip(urls, names))
        right = right and all(s == next(f["sha256"] for f in MF.RELEASE_FILES if f["name"] == n) for s, n in zip(shas, names))
        ok(right, f"web link: {k}: {got_os} {got_arch or '?'} -> {[n[:34] for n in names] or 'a computer is needed'}")
    page = (ROOT / "web" / "installer" / "installer.js").read_text(encoding="utf-8")
    ok("mflasherFor(opts.mflasher" in page and "TEXT.mfSha" in page and "mflasherLinks(mf)" in page,
       "the installer page shows the links with their SHA-256 when a switch needs download mode")
    ok("fetch(" not in page[page.index("async function mflasherLinks"):page.index("async function sendSoftKey")],
       "...and downloads nothing itself")


def site_data():
    sys.path.insert(0, str(ROOT / "web"))
    import make_site
    ok(make_site.mflasher_release() == MF.site_data(), "make_site.py puts tools/mflasher.py's data in site.json (web installer)")
    out = TMP / "site"
    make_site.main("0.0-test", out)
    site = json.loads((out / "webapp" / "installer" / "site.json").read_text(encoding="utf-8"))
    html = (out / "webapp" / "installer" / "index.html").read_text(encoding="utf-8")
    ed = json.loads((out / "webapp" / "editor" / "installer" / "site.json").read_text(encoding="utf-8"))
    ok(site["mflasher"] == MF.site_data() and ed["mflasher"] == MF.site_data() and json.dumps(MF.site_data()) in html,
       "the built site: the installer page, its site.json and the editor's carry the same release")


# ---------------------------------------------------------------- the app and the single source ---

def app_and_source():
    out = TMP / "app" / "mflasher.json"
    subprocess.run([sys.executable, str(ROOT / "tools" / "sync_mflasher.py"), str(out)], check=True, capture_output=True)
    ok(json.loads(out.read_text(encoding="utf-8")) == MF.site_data(), "the app's mflasher.json (tools/sync_mflasher.py) is tools/mflasher.py's data")
    yml = (ROOT / "ios" / "OptimistMirror" / "project.yml").read_text(encoding="utf-8")
    ok("tools/sync_mflasher.py" in yml and "mflasher.json" in yml and "sync_mflasher.py" in (ROOT / "ios" / "OptimistMirror" / "OptimistMirror.xcodeproj" / "project.pbxproj").read_text(encoding="utf-8"),
       "the app's build phase writes it (project.yml and the generated project)")
    swift = (ROOT / "ios" / "OptimistMirror" / "Sources" / "Flash" / "MFlasherRelease.swift").read_text(encoding="utf-8")
    ok("isMacCatalystApp" in swift and 'kind == "dmg"' in swift and "M-Flasher for macOS / Windows / Linux" in swift,
       "the app: iPhone / iPad say a computer is needed, Catalyst links the macOS dmg")
    tracked = subprocess.run(["git", "-C", str(ROOT), "ls-files", "-z"], capture_output=True, text=True, check=True).stdout.split("\0")
    skip = {"tools/mflasher.py", "tests/mflasher_get_test.py"}
    stray = []
    for f in tracked:
        p = ROOT / f
        if not f or f in skip or f.startswith(("fm1base/", "third_party/", "web/fm1base/")) or not p.is_file() or p.stat().st_size > 2_000_000:
            continue
        try:
            text = p.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        stray += [f"{f}: {m['name']}" for m in MF.RELEASE_FILES if m["sha256"] in text]
        if "0.7.0-macos.dmg" in text or "m-flasher/releases/download" in text:
            stray.append(f"{f}: a release URL or file name")
    ok(not stray, "one source of truth: no other tracked file carries a pinned SHA-256 or release URL" + (f" {stray[:4]}" if stray else ""))


if __name__ == "__main__":
    shutil.rmtree(TMP, ignore_errors=True)
    TMP.mkdir(parents=True)
    saved = platform.system
    for t in (picking, fetching, asking, web_links, site_data, app_and_source):
        t()
    shutil.rmtree(TMP, ignore_errors=True)
    print("mflasher get tests passed" if not fails else f"MFLASHER GET TESTS FAILED ({fails})")
    sys.exit(1 if fails else 0)
