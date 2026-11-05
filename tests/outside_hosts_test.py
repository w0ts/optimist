#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""No page Optimist ships, and nothing in the Mirror app, asks an outside host for a font, script, style, image or data,
except the listed ONLINE-ONLY features (the user, 2026-10-29: "bundle the font everywhere", "it should work offline").
Checked: web/ (the editor, installer, simulator, builder, the site generators), tools/site_manual.py, the app's bundled pages
(ios/OptimistMirror/Resources) and every URL literal in the app's Swift (ios/OptimistMirror/Sources).
Links the user clicks (<a href>) are not loads and are not checked. Every ALLOW entry says which feature needs the network and
what happens offline; a new outside host fails this test until it is listed (or, better, bundled).
    python3 tests/outside_hosts_test.py"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HTML_JS = [ROOT / "web", ROOT / "tools" / "site_manual.py", ROOT / "ios" / "OptimistMirror" / "Resources"]
SWIFT = ROOT / "ios" / "OptimistMirror" / "Sources"
EXT = {".html", ".htm", ".css", ".js", ".mjs", ".svg", ".py"}
SKIP = ("/node_modules/", "/e2e_", "/test_")        # (tests of the pages: they name hosts to refuse or to fake)

# (path suffix, URL prefix, the online-only feature). Nothing else may be asked of an outside host.
ALLOW = [
    # the web builder: Pyodide (Python in the page) from a CDN, the cloud build through the relay and GitHub. Offline: the page
    # does not start / says the network is missing. The Mirror app shows it as a remote page (webapp/builder/app.html).
    ("web/builder/logic.js", "https://cdn.jsdelivr.net/pyodide/", "builder: Pyodide"),
    ("web/builder/logic.js", "cdn.jsdelivr.net", "builder: Pyodide (the constant)"),
    ("web/builder/logic.js", "challenges.cloudflare.com", "builder: Turnstile (the constant)"),
    ("web/builder/logic.js", "https://challenges.cloudflare.com/turnstile/", "builder: Build in the cloud (Turnstile)"),
    ("web/builder/logic.js", "https://motscousus.com/optimist/cloud-build.php", "builder: Build in the cloud (relay)"),
    ("web/builder/logic.js", "https://raw.githubusercontent.com/", "builder: cloud build results"),
    ("web/builder/logic.js", "https://github.com/", "builder: opens the issue (a link)"),
    ("web/make_site.py", "https://apps.apple.com/", "the menu bar's App Store badges (a link; the images are inlined)"),
    ("web/builder/builder.js", "https://raw.githubusercontent.com/", "builder: cloud build results"),
    ("web/builder/builder.js", "cdn.jsdelivr.net", "builder: its own caption about Pyodide (text)"),
    ("web/builder/cloud.js", "https://api.github.com/", "builder: cloud build index"),
    ("web/builder/cloud.js", "https://raw.githubusercontent.com/", "builder: cloud build index"),
    ("web/installer/installer.js", "https://github.com/", "installer: links the user clicks (the project, a build's issue), not loads"),
    ("web/installer/installer.js", "https://www.m-vave.com/download", "installer: the vendor's download page (a link)"),
    ("web/builder/builder.js", "https://github.com/", "builder: a link the user clicks"),
    ("web/make_site.py", "https://github.com/", "the site's repository link (a link)"),
    ("tools/site_manual.py", "https://github.com/", "the manual's link to the repository (a link)"),
    ("web/install_route.js", "https://github.com/", "installer: M-Flasher's release page and the install paths doc, links the user clicks (the page loads nothing)"),
    # the installer's cloud builds (the user's own build, by key)
    ("web/installer/installer.js", "https://api.github.com/", "installer: cloud builds"),
    ("web/installer/installer.js", "https://raw.githubusercontent.com/", "installer: cloud builds"),
]
SWIFT_ALLOW = [
    ("Flash/FlashCatalog.swift", "https://w0ts.github.io/optimist/webapp/installer/site.json", "Flash tab: the firmware list; offline: 'no connection'"),
    ("Flash/FlashCatalog.swift", "https://w0ts.github.io/optimist/webapp/builder/app.html", "Flash tab: the web builder (cloud build, Pyodide)"),
    ("Flash/FlashCatalog.swift", "https://raw.githubusercontent.com/", "Flash tab: package download"),
    ("Flash/FlashCatalog.swift", "https://api.github.com/", "Flash tab: one's own cloud build"),
    ("Flash/BuilderWebView.swift", "https://github.com/", "builder: only the link kinds the app will open (a check, not a load)"),
    ("Web/WebTabView.swift", "https://w0ts.github.io/optimist/webapp/editor/", "Simulator (home screen): the site's editor, whose Simulator tab loads the firmware builds (not bundled); offline: 'needs the internet'. The Web tabs use the bundled editor (optimistedit:)"),
    ("Flash/InstallRoute.swift", "https://github.com/", "Flash tab: M-Flasher's release list, a link the user clicks (not a load)"),
    ("Companion/Fm1BaseSite.swift", "http", "string handling of the repo link (not a request)"),
]

HOSTS = re.compile(r"fonts\.googleapis\.com|fonts\.gstatic\.com|cdn\.jsdelivr\.net|unpkg\.com|cdnjs\.cloudflare\.com|ajax\.googleapis\.com|challenges\.cloudflare\.com", re.I)
EXT_URL = r"(?:https?:)?//"
LOADING_TAG = re.compile(r"<(script|link|img|source|iframe|audio|video|embed|object|track|image|use|input|frame)\b[^>]*>", re.I | re.S)
LOADING_ATTR = re.compile(r"\b(?:src|href|data|poster|srcset|xlink:href|action)\s*=\s*[\"']?\s*(" + EXT_URL + r"[^\"'\s>]*)", re.I)
CSS_URL = re.compile(r"(?<![A-Za-z.])url\(\s*[\"']?\s*(" + EXT_URL + r"[^\"')\s]*)")
CSS_IMPORT = re.compile(r"@import\s+[\"']\s*(" + EXT_URL + r"[^\"']*)")
JS_CALL = re.compile(r"\b(?:fetch|importScripts|import|Worker|SharedWorker|WebSocket|EventSource|sendBeacon)\s*\(\s*[`\"']\s*(" + EXT_URL + r"[^`\"']*)")
JS_ASSIGN = re.compile(r"\.(?:src|srcset|data)\s*=\s*[`\"']\s*(" + EXT_URL + r"[^`\"']*)")
JS_XHR = re.compile(r"\.open\(\s*[\"'][A-Z]+[\"']\s*,\s*[`\"']\s*(" + EXT_URL + r"[^`\"']*)")
JS_IMPORT_FROM = re.compile(r"\b(?:from|import)\s+[\"'](" + EXT_URL + r"[^\"']*)")
# a URL constant that the code loads later (export const X = "https://..."): any absolute http(s) string literal in JS/py
JS_LITERAL = re.compile(r"[`\"'](https?://[^`\"'\s]+)")
SWIFT_URL = re.compile(r"\"(https?://[^\"\s]*)")


def allowed(table, path, url):
    return any(str(path).endswith(s) and (url.startswith(p) or p in url) for s, p, _ in table)


def page_loads(path, text):
    """[(line, url)] each load of an outside host in one HTML / CSS / JS file"""
    found = []
    ln = lambda i: text.count("\n", 0, i) + 1
    for m in HOSTS.finditer(text):
        found.append((ln(m.start()), m.group(0)))
    for m in LOADING_TAG.finditer(text):
        for a in LOADING_ATTR.finditer(m.group(0)):
            found.append((ln(m.start()), a.group(1)))
    for rx in (CSS_URL, CSS_IMPORT, JS_CALL, JS_ASSIGN, JS_XHR, JS_IMPORT_FROM):
        for m in rx.finditer(text):
            found.append((ln(m.start()), m.group(1)))
    if str(path).endswith((".js", ".mjs", ".py")):
        for m in JS_LITERAL.finditer(text):
            if "w3.org/" not in m.group(1):
                found.append((ln(m.start()), m.group(1)))
    return sorted(set(found))


def swift_urls(text):
    return sorted({(text.count("\n", 0, m.start()) + 1, m.group(1)) for m in SWIFT_URL.finditer(text)
                   if not text[:m.start()].rsplit("\n", 1)[-1].lstrip().startswith("//")})


def selftest():
    for t in ['<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow">', '<script src="//cdn.example.com/x.js"></script>',
              "<img src='http://a.example/i.png'>", "@font-face { src: url(https://example.com/f.woff2); }", "@import 'https://example.com/a.css';",
              "fetch('https://example.com/data.json')", "new Worker('https://example.com/w.js')", "import('https://e.example/m.js')",
              "x.src = 'https://e.example/s.js'"]:
        assert page_loads("x.html", t), f"not found: {t}"
    assert page_loads("x.js", "const U = 'https://e.example/a'"), "a URL constant in JS"
    for t in ['<a href="https://example.com/">link</a>', '<link rel="stylesheet" href="../fonts/fonts.css">', "@font-face { src: url(Barlow-400.woff2); }",
              "fetch('data.json')", "const u = new URL('//', location)"]:
        assert not page_loads("x.html", t), f"false alarm: {t}"
    assert swift_urls('static let a = URL(string: "https://e.example/x")!\n// "https://commented.example"') == [(1, "https://e.example/x")]


def scan():
    bad, n = [], 0
    for base in HTML_JS:
        for p in sorted([base] if base.is_file() else base.rglob("*")):
            if p.suffix.lower() not in EXT or not p.is_file() or any(s in str(p) for s in SKIP):
                continue
            n += 1
            for line, url in page_loads(p, p.read_text(encoding="utf-8", errors="replace")):
                if not allowed(ALLOW, p.relative_to(ROOT), url):
                    bad.append(f"{p.relative_to(ROOT)}:{line}: {url}")
    for p in sorted(SWIFT.rglob("*.swift")):
        n += 1
        for line, url in swift_urls(p.read_text(encoding="utf-8")):
            if not allowed(SWIFT_ALLOW, p.relative_to(SWIFT), url):
                bad.append(f"{p.relative_to(ROOT)}:{line}: {url}")
    return n, bad


def fonts_present():
    """the bundled pages' fonts.css and the files it names are in the app's resources (and in web/fm1base's source)"""
    bad = []
    res = ROOT / "ios" / "OptimistMirror" / "Resources" / "Fm1Base"
    for page in sorted(res.glob("*/index.html")):
        for href in re.findall(r"<link[^>]*href=\"([^\"]*fonts\.css)\"", page.read_text(encoding="utf-8")):
            css = (page.parent / href).resolve()
            if not css.is_file():
                bad.append(f"{page.relative_to(ROOT)}: {href} is not bundled")
                continue
            bad += [f"{css.relative_to(ROOT)}: {f} is not bundled" for f in re.findall(r"url\(([^)]+)\)", css.read_text(encoding="utf-8"))
                    if not (css.parent / f).is_file()]
    return bad


def bundled_editor():
    """the app's bundled editor (tools/sync_ios_editor.py: web/editor.html + what it loads): every file it copies exists, is one
    this test scans (web/ or the logo), and every local file the page's own tags load is among them"""
    sys.path.insert(0, str(ROOT / "tools"))
    import sync_ios_editor as s
    bad = []
    names = {dst for _, dst in s.FILES}
    for src, dst in s.FILES:
        if not (ROOT / src).is_file():
            bad.append(f"tools/sync_ios_editor.py: {src} does not exist")
        elif not src.startswith(("web/", "assets/logo/")):
            bad.append(f"tools/sync_ios_editor.py: {src} is not under web/ or assets/logo/ (not scanned for outside hosts)")
        elif src.startswith("assets/logo/") and page_loads(ROOT / src, (ROOT / src).read_text(encoding="utf-8", errors="replace")):
            bad.append(f"{src}: loads an outside host")
    page = (ROOT / "web" / "editor.html").read_text(encoding="utf-8")
    for tag in LOADING_TAG.finditer(page):
        for a in re.finditer(r"\b(?:src|href)\s*=\s*[\"']([^\"'#]+)", tag.group(0)):
            ref = a.group(1)
            if re.match(r"(?:[a-z]+:|//|data:)", ref) or tag.group(1).lower() == "a" or "{" in ref or "icon" in tag.group(0).split("href")[0]:
                continue                              # (outside hosts are scan()'s; links and the browser-tab icons are not loads)
            if re.sub(r"^(?:\.\./)+", "", ref.removeprefix("./")) not in names:
                bad.append(f"web/editor.html loads {ref}, which the app's bundled editor does not hold (tools/sync_ios_editor.py)")
    return bad


def main():
    selftest()
    n, bad = scan()
    bad += fonts_present() + bundled_editor()
    if bad:
        print("\n".join(bad))
        print(f"FAIL: {len(bad)} outside-host use(s) not in the allow-list (tests/outside_hosts_test.py)")
        return 1
    print(f"ok: {n} files, no outside host except the {len(ALLOW) + len(SWIFT_ALLOW)} listed online-only uses")
    return 0


if __name__ == "__main__":
    sys.exit(main())
