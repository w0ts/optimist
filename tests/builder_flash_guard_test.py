# SPDX-License-Identifier: GPL-3.0-only
"""The builder menu's flash guard (tools/builder/stamp.py) and the package source the flash confirmation shows
(tools/flash.py package_source / source_notes). No device, no build: temporary packages and a temporary git repo.

2026-10-12: a builder menu left open 21 hours flashed build/optimist-0.1-dev-1830f0b.fwsc, a 4-hour-old build of
another commit and of the profile as it was then, as "the last build"; the FM-1 went to its USB rescue. These checks
pin what f may flash now. Then, when Textual is there (this Python or the builder venv's), the menu itself is driven
headless (Builder.run_test): f with no build, after a configuration change, and after a build all refuse or go as
stamp.refusal says, and nothing reaches the device (flash.preflight replaced)."""
import importlib
import importlib.util
import os
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "builder"))
FL = importlib.import_module("flash")
ST = importlib.import_module("stamp")

fails = 0


def check(what, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + what)
    fails += not ok


def git(repo, *a):
    env = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@t", GIT_COMMITTER_NAME="t",
               GIT_COMMITTER_EMAIL="t@t")
    return subprocess.run(["git", "-C", str(repo), *a], capture_output=True, text=True, env=env, check=True).stdout.strip()


def sidecar(pkg, commit, name="mots", h="1901b25c", dirty=False):
    """the -ui.zip SOURCE.txt tools/build.py writes beside a package"""
    with zipfile.ZipFile(pkg.with_name(pkg.stem + "-ui.zip"), "w") as z:
        z.writestr("SOURCE.txt", f"Web editor for felucca.fwsc\nfrom /x/optimist at {commit}"
                                 f"{' (with uncommitted changes)' if dirty else ''}, web/editor.html (GPL-3.0-only)\n"
                                 f"build configuration: {name} hash {h}\n")


with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    pkg = td / "optimist-0.1-dev-abc.fwsc"
    pkg.write_bytes(b"package one")
    code = {str(td / "menu.py"): "aa", str(td / "registry.py"): "bb"}

    # ---- stamp.refusal: what f may flash
    why = ST.refusal(None, 1, code, code)
    check("guard: no build in this builder: refused, build first (never the newest build/optimist-*.fwsc)",
          why is not None and "build first" in why)
    s = ST.Stamp(pkg, 0x1901B25C, "mots")
    check("guard: this builder's build, the configuration unchanged, its code unchanged: may flash",
          ST.refusal(s, 0x1901B25C, code, code) is None)
    why = ST.refusal(s, 0x57764A52, code, code)
    check("guard: the configuration changed since the build (the 2026-10-12 case): refused, build again",
          why is not None and "configuration changed" in why and "1901b25c" in why and "57764a52" in why)
    why = ST.refusal(s, 0x1901B25C, code, dict(code, **{str(td / "registry.py"): "cc"}))
    check("guard: the builder's own code changed on disk since it started: refused, restart named",
          why is not None and "registry.py" in why and "make builder" in why)
    pkg.write_bytes(b"another build in this checkout")
    why = ST.refusal(s, 0x1901B25C, code, code)
    check("guard: the package was replaced since this builder made it (a CLI build): refused",
          why is not None and "replaced" in why)
    pkg.unlink()
    why = ST.refusal(s, 0x1901B25C, code, code)
    check("guard: the package is gone: refused", why is not None and "gone" in why)

    # ---- the builder's code fingerprint
    f1, f2 = td / "a.py", td / "b.py"
    f1.write_text("x = 1\n")
    f2.write_text("y = 1\n")
    fp = ST.code_fingerprint([f1, f2])
    check("fingerprint: unchanged files: nothing stale", ST.changed_code(fp) == [])
    f2.write_text("y = 2\n")
    check("fingerprint: an edited file is named", ST.changed_code(fp) == ["b.py"])
    f1.unlink()
    check("fingerprint: a removed file is named too", ST.changed_code(fp) == ["a.py", "b.py"])
    check("fingerprint: the menu's own code is covered (menu, registry, configure, flash, fm1_install)",
          {"menu.py", "registry.py", "configure.py", "flash.py", "fm1_install.py"} <= {p.name for p in ST.CODE})

    # ---- the package's source, for the confirmation (optimist.py flash and the menu)
    repo = td / "repo"
    repo.mkdir()
    git(repo, "init", "-q", "-b", "work")
    (repo / "f").write_text("1")
    git(repo, "add", "f")
    git(repo, "commit", "-q", "-m", "one")
    c1 = git(repo, "rev-parse", "HEAD")
    p = td / "optimist-0.1-dev-x.fwsc"
    p.write_bytes(b"p")
    check("source: no -ui.zip beside the package: unknown, said so",
          FL.package_source(p) is None and "unknown" in FL.source_notes(p, repo)[0])
    sidecar(p, c1)
    src = FL.package_source(p)
    check("source: the commit, the configuration's name and hash from SOURCE.txt",
          src == {"commit": c1, "dirty": False, "name": "mots", "hash": "1901b25c"})
    n = FL.source_notes(p, repo)
    check("source: built from the checkout's commit: one line, no NOTE",
          len(n) == 1 and c1[:7] in n[0] and "mots" in n[0])
    for i in (2, 3):
        (repo / "f").write_text(str(i))
        git(repo, "commit", "-q", "-am", f"c{i}")
    n = FL.source_notes(p, repo)
    check("source: the checkout moved on: a NOTE with the commit count (an older build than the tree)",
          len(n) == 2 and "2 commit(s) newer" in n[1] and "older build" in n[1])
    sidecar(p, c1, dirty=True)
    check("source: a build with uncommitted changes says so", "uncommitted" in FL.source_notes(p, repo)[0])
    check("source line: the checkout's path, branch and commit (what b compiles)",
          ST.source_line(repo) == f"source: {repo}  work@{git(repo, 'rev-parse', '--short=7', 'HEAD')}")
    plan = FL.Plan(p, "FM-1_700", "FM-1_700  [running]", None, FL.source_notes(p, repo))
    lines = plan.lines()
    check("plan: the confirmation shows where the package comes from, before the running identity",
          any(ln.startswith("built:") for ln in lines) and
          [i for i, ln in enumerate(lines) if ln.startswith("built:")][0] <
          [i for i, ln in enumerate(lines) if ln.startswith("running:")][0])


# ---- the menu itself (Textual, headless): f goes through stamp.refusal; nothing reaches the device
def menu_checks():
    import asyncio
    import configure as C
    import menu as M
    calls = []
    save = (M.F.preflight, M.F.install)
    M.F.preflight = lambda pkg, port=None, notes=None: calls.append(("preflight", str(pkg))) or FL.Plan(
        pkg, "FM-1_700", "FM-1_700  [running]", None, [])
    M.F.install = lambda plan, capture=True: calls.append(("install", str(plan.package))) or (0, "done")

    async def run():
        app = M.Builder(C.defaults(), "test")
        async with app.run_test() as pilot:
            await pilot.press("f")
            await pilot.pause()
            check("menu: f with no build in this builder: FLASH REFUSED, the device not read",
                  app.build_out.startswith("FLASH REFUSED") and "build first" in app.build_out and not calls)
            with tempfile.TemporaryDirectory() as td:
                pkg = Path(td) / "optimist-0.1-dev-t.fwsc"
                pkg.write_bytes(b"built")
                app.built = ST.Stamp(pkg, C.cfg_hash(app.cfg) ^ 1, "test")   # (a build of another configuration)
                await pilot.press("f")
                await pilot.pause()
                check("menu: f after the configuration changed: refused, the device not read",
                      app.build_out.startswith("FLASH REFUSED") and "configuration changed" in app.build_out
                      and not calls)
                app.built = ST.Stamp(pkg, C.cfg_hash(app.cfg), "test")
                app.code0 = dict(app.code0, **{str(ST.HERE / "registry.py"): "older"})
                await pilot.press("f")
                await pilot.pause()
                check("menu: f when the builder's code is stale: refused, restart named, the device not read",
                      app.build_out.startswith("FLASH REFUSED") and "make builder" in app.build_out and not calls)
                app.code0 = ST.code_fingerprint()
                await pilot.press("f")
                for _ in range(20):
                    await pilot.pause(0.05)
                    if calls:
                        break
                check("menu: f after this builder's build of this configuration: the device is read (preflight) "
                      "for that package, nothing written before the answer",
                      calls == [("preflight", str(pkg))])
                await pilot.press("escape")
    try:
        asyncio.run(run())
    finally:
        M.F.preflight, M.F.install = save


if importlib.util.find_spec("textual") is not None:
    menu_checks()
elif os.environ.get("BUILDER_GUARD_CHILD") != "1":
    import deps
    py = deps.venv_python()
    ok = py.exists() and subprocess.run([str(py), "-c", "import textual"], capture_output=True).returncode == 0
    if ok:                                                   # (the menu's checks under the builder venv's Python)
        r = subprocess.run([str(py), __file__], env=dict(os.environ, BUILDER_GUARD_CHILD="1"), capture_output=True,
                           text=True)
        print("\n".join(ln for ln in r.stdout.splitlines() if ln.startswith(("ok   menu", "FAIL"))))
        fails += r.returncode != 0
    else:
        print("note: no Textual here nor in the builder venv (make setup): the menu's own checks did not run")

print("builder flash guard: " + ("all passed" if not fails else f"{fails} FAILED"))
sys.exit(1 if fails else 0)
