#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The firmware builder's menu (tools/builder/menu.py) headless, through Textual's App.run_test: an error of
validate() shows on the lines of the items it names, in red, as soon as the configuration becomes invalid (no b),
and goes once it is fixed; the message panel lists it; b on an invalid configuration says CANNOT BUILD and keeps
it until the next b. No build here. Needs Textual: run with the builder's venv (tools/menuconfig makes it;
tests/run_tests.sh uses it when it is there)."""
import asyncio
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from builder_path import ROOT
import configure as C  # noqa: E402
import menu as M  # noqa: E402
import registry as R
import room as RM
import stamp as ST
from ble_room_rule import expected_room, off_value, with_ble  # (tests/: the rule, from costs.json alone)

# the estimate only, from costs.json: a real build left in build/exact-sizes.json (a full gate makes some, a light one
# fewer) must neither replace a total nor anchor it, or the same costs.json passes one gate and fails another
C.exact_sizes = lambda cfg: None
C.exact_builds = lambda: {}
fails = 0


def check(what, ok):
    global fails
    print(f"{what:78s} {'ok' if ok else 'FAIL'}")
    fails += not ok


def shown_over(cfg, costs):
    """the menu's own rule for its red bars (menu.py update_budget: fits, no FIT_MARGIN; over_any adds the margin, which
    only --fit and the BLE room use): a configuration within the margin of a limit is not shown over"""
    return any(o > 0 for _, _, o in C.fits(C.budget(cfg, costs)["total"], cfg).values())


def label(app, key):
    return app.nodes[key].label.plain


def panel(app):
    return str(app.query_one("#msgs_t").content)


async def toggle(app, pilot, key):
    tree = app.query_one("#tree")
    tree.move_cursor(app.nodes[key])
    await pilot.press("space")
    await pilot.pause()


def tree_keys(app):
    """the item keys in the tree's display order, per group"""
    return [[n.data] + [c.data for c in n.children] for g in app.query_one("#tree").root.children for n in g.children]


async def sort_test():
    """o cycles default -> flash -> ram -> pool -> ramtext: items sorted largest first inside each group, children
    under their parent, unmeasured last, the sorted column marked, the choice only per session"""
    app = M.Builder(C.defaults(), "default")
    async with app.run_test(size=(200, 60)) as pilot:
        await pilot.pause()
        default = tree_keys(app)
        check("sort: starts in the default order", app.sort_by is None and "default order" in str(app.query_one("#bars").content))
        for want in ("flash", "ram", "pool", "ramtext", None):
            await pilot.press("o")
            await pilot.pause()
            check(f"sort: o selects {want or 'default order'}", app.sort_by == want)
            if want is None:
                check("sort: back to the default order", tree_keys(app) == default)
                break
            check(f"sort {want}: the bar names it", f"sort (o): {want}" in str(app.query_one("#bars").content))
            cost = lambda k: (app.savings.get(k) or {}).get(want)
            ok_order = ok_unmeasured = ok_kids = True
            for grp in app.query_one("#tree").root.children:
                tops = [n.data for n in grp.children]
                vals = [cost(k) for k in tops]
                known = [v for v in vals if v is not None]
                ok_order &= known == sorted(known, reverse=True)
                ok_unmeasured &= vals == known + [None] * (len(vals) - len(known))
                for n in grp.children:
                    ok_kids &= all(R.ITEMS[c.data].parent == n.data for c in n.children)
                    kv = [cost(c.data) for c in n.children]
                    kk = [v for v in kv if v is not None]
                    ok_order &= kk == sorted(kk, reverse=True) and kv == kk + [None] * (len(kv) - len(kk))
            check(f"sort {want}: largest first inside each group and parent", ok_order)
            check(f"sort {want}: unmeasured items last", ok_unmeasured)
            check(f"sort {want}: children stay under their parent", ok_kids)
            check(f"sort {want}: same items as the default order",
                  sorted(sum(tree_keys(app), [])) == sorted(sum(default, [])))
            check(f"sort {want}: the sorted column is highlighted",
                  any(s.style and "reverse" in str(s.style) for k in app.nodes for s in app.nodes[k].label.spans)
                  or app.over.get(want, 0) > 0)       # (an overflowing region keeps its red marks over the sort mark)
    check("sort: per session, a new builder starts in the default order", M.Builder(C.defaults(), "default").sort_by is None)


async def main():
    await sort_test()
    app = M.Builder(C.defaults(), "default")
    async with app.run_test(size=(200, 60)) as pilot:
        await pilot.pause()
        check("the default configuration: no line marked", not any("✗" in label(app, k) for k in app.nodes))
        tree = app.query_one("#tree")
        tree.cursor_line = 0
        await pilot.press("right")
        await pilot.pause()
        down = tree.cursor_line
        check("right pages the list down (like page down)", down > 1)
        await pilot.press("pagedown")
        await pilot.pause()
        check("... as page down does", tree.cursor_line > down)
        far = tree.cursor_line
        await pilot.press("left")
        await pilot.pause()
        check("left pages the list up", 0 <= tree.cursor_line < far)
        before = dict(app.cfg)
        await pilot.press("right", "left")
        check("left / right change no item", app.cfg == before)
        tree.cursor_line = 0
        await pilot.pause()
        for k in ("DRUM_SYNTH", "DS_KAWA", "DS_PLAITS", "DRUM_SAMPLED"):   # (the three drum synths: each a drum source)
            await toggle(app, pilot, k)
        marked = [k for k in app.nodes if "✗" in label(app, k)]
        check("drum synths and sampled kits off: their lines marked at once, before any b",
              "DRUM_SYNTH" in marked and "DS_KAWA" in marked and "DRUM_SAMPLED" in marked and
              "drum source" in label(app, "DRUM_SYNTH") and not app.build_out)
        styles = [s.style for s in app.nodes["DRUM_SYNTH"].label.spans if "✗" in app.nodes["DRUM_SYNTH"].label.plain[s.start:s.end]]
        check("... in red", any("red" in str(s) for s in styles))
        check("... only the items the error names", all(k in ("DRUM_SYNTH", "DS_KAWA", "DS_PLAITS", "DRUM_SAMPLED", "DRUM_X0X909",
                                                              "DRUM_X0X808") or k.startswith("KIT_") for k in marked))
        check("... the message panel lists the error", "ERROR" in panel(app) and "drum source" in panel(app))
        await pilot.press("b")
        await pilot.pause()
        check("b: CANNOT BUILD in the panel, nothing built", app.build_out.startswith("CANNOT BUILD") and
              panel(app).startswith("CANNOT BUILD"))
        await toggle(app, pilot, "DRUM_SYNTH")
        check("the drum synth back: the marks go at once", not any("✗" in label(app, k) for k in app.nodes))
        check("... CANNOT BUILD stays in the panel until the next b", panel(app).startswith("CANNOT BUILD"))
        # (FM6's three ENGINE modes and their "none left" mark: gone with the modes, 2026-10-18, FM6 plays MODERN only)
        if "MOTION" in app.nodes:
            await toggle(app, pilot, "MOTION")
            check("motion recording with 16 sections: valid, no mark", app.cfg["MOTION"] == 1 and
                  app.cfg["SECTIONS"] == 16 and not any("✗" in label(app, k) for k in app.nodes))
    app = M.Builder(C.defaults(), "default")             # (a fresh menu: every sampled kit ticked)
    async with app.run_test(size=(200, 60)) as pilot:
        await pilot.pause()
        if "KIT_DEEP" in app.nodes and app.costs and C.kit_shared(app.costs):
            kits = C.kit_keys()
            check("every sampled kit says it shares the PERC samples with the others",
                  all("shares PERC samples" in label(app, k) and "KB" in label(app, k) for k in kits))
            check("... and none is the last kit while all are ticked",
                  not any("last kit" in label(app, k) for k in kits))
            for k in kits[1:]:
                app.nodes["DRUM_SAMPLED"].expand()
                await toggle(app, pilot, k)
            check("one sampled kit left: it shows it is the last and the real saving (the samples)",
                  "last kit: off drops the samples" in label(app, kits[0]) and "flash +" in label(app, kits[0]) and
                  not any("last kit" in label(app, k) for k in kits[1:]))
    # BLE in the menu (tools/builder/room.py after_toggle): ticking it removes nothing, like any other tick (2026-10-21)
    costs = C.load_costs()
    for prof in ("synth", "drum-machine", "everything-that-fits", "fm-va-studio", "x0x-drums"):
        base, _ = C.load_profile(prof)
        if base["BLE"]:                   # (BLE on in the profile already: ticking it would turn it off)
            print(f"BLE ticked on {prof}: the profile has BLE on already (user-default since 2026-10-17), not a case")
            continue
        withble = with_ble(base, 0)
        app = M.Builder(dict(base), prof)
        async with app.run_test(size=(200, 60)) as pilot:
            await pilot.pause()
            await toggle(app, pilot, "BLE")
            check(f"BLE ticked on {prof}: nothing else changes, fitting or not (like any other tick), no message",
                  app.cfg == withble and "to make room" not in panel(app))
            check("... the bars show the overflow when it does not fit", bool(app.over) == shown_over(withble, costs))
            await toggle(app, pilot, "BLE")
            check("... unticked: the profile again", app.cfg == base)
    for prof in ("synth", "drum-machine", "fm-va-studio", "x0x-drums"):    # (BLE_DIAG ticked after BLE)
        base, _ = C.load_profile(prof)
        app = M.Builder(dict(base), prof)
        async with app.run_test(size=(200, 60)) as pilot:
            await pilot.pause()
            await toggle(app, pilot, "BLE")
            after_ble = dict(app.cfg)
            app.nodes["BLE"].expand()
            await toggle(app, pilot, "BLE_DIAG")
            check(f"BLE_DIAG ticked after BLE on {prof}: only it changes, nothing more is removed, the bars show what is left over",
                  app.cfg == dict(after_ble, BLE_DIAG=1) and bool(app.over) == shown_over(app.cfg, costs))
    app = M.Builder(C.defaults(), "default")             # (the ring in the bars: information)
    async with app.run_test(size=(200, 60)) as pilot:
        await pilot.pause()
        bars = lambda: str(app.query_one("#bars").content)
        check("the bars show the undo ring", "UNDO" in bars() and "ring" in bars())
        check("no Reserve group in the menu", "RESERVE_UNDO_KB" not in [n.data for g in app.query_one("#tree").root.children for n in g.children])
    app = M.Builder(C.defaults(), "default")             # q and ctrl+c are refused while the FM-1 is written, work again after
    async with app.run_test(size=(200, 60)) as pilot:
        await pilot.pause()
        app.flashing = True
        await pilot.press("q")
        await pilot.press("ctrl+c")
        await pilot.pause()
        check("flashing: q and ctrl+c are refused, the menu stays", app.is_running)
        app.flashing = False
        await pilot.press("q")
        await pilot.pause()
        check("... not flashing: q quits", not app.is_running)
    # f: Flash to FM-1 (fm1_install mocked: no device is touched)
    import flash as FL
    calls = []

    def fake_tool(args, capture=True, on_line=None):
        calls.append(list(args))
        return (0, "Optimist_730  [running]  port: FM-1") if "--info" in args else (0, "writing 100%\ndone: the FM-1 runs Optimist_730")
    saved = (FL.run_tool, FL.FI.load_package, FL.last_package)
    FL.run_tool, FL.FI.load_package = fake_tool, (lambda path, force: ("FM-1_730", b""))
    with tempfile.TemporaryDirectory() as td:
        pkg = Path(td) / "optimist-1.0-x.fwsc"
        pkg.write_bytes(b"x")
        FL.last_package = lambda build=None: None
        try:
            app = M.Builder(C.defaults(), "default")
            async with app.run_test(size=(200, 60)) as pilot:
                await pilot.pause()
                check("f is bound: Flash to FM-1 in the footer", any(b.key == "f" and "flash" in b.description for b in app.BINDINGS))
                builds = []
                app.run_build = lambda cfg, name, then_emu=False: builds.append(name)    # (no toolchain here)
                await pilot.press("f")
                await pilot.pause()
                check("f with no build: it builds first (the build is started), nothing asked, no device touched",
                      type(app.screen).__name__ != "Pick" and not calls and builds == ["default"]
                      and app.flash_after_build and "then the flash" in app.build_out)
                app.flash_after_build = False                              # (the build never ends here)
                app.progress_hide()
                built = ST.Stamp(pkg, C.cfg_hash(app.cfg), "default")      # (what run_build records: stamp.py)
                app.show_build("BUILD OK\n  f: Flash to FM-1", {}, built)
                check("a successful build offers it: the panel says so and remembers the package (its stamp)",
                      app.built is built and app.built.package == pkg and "Flash to FM-1" in app.build_out)
                app.show_build("BUILD FAILED", {}, None)
                check("a failed build forgets the package", app.built is None)
                app.show_build("BUILD OK\n  f: Flash to FM-1", {}, built)
                await pilot.press("f")
                for _ in range(40):
                    await pilot.pause(0.05)
                    if type(app.screen).__name__ == "Pick":
                        break
                title = str(app.screen.title_) if type(app.screen).__name__ == "Pick" else ""
                check("f: a confirmation screen with the running identity, the package and the recovery",
                      "Optimist_730  [running]" in title and pkg.name in title and "FM-1_730" in title and
                      "Getting back" in title and app.screen.options[0] == "Cancel")
                check("... only the identity was read so far (nothing written before the answer)",
                      calls == [["--info"]])
                await pilot.press("escape")
                await pilot.pause()
                check("escape cancels: nothing written", calls == [["--info"]] and type(app.screen).__name__ != "Pick")
                await pilot.press("f")
                for _ in range(40):
                    await pilot.pause(0.05)
                    if type(app.screen).__name__ == "Pick":
                        break
                await pilot.press("down", "enter")                     # (Cancel is first: down, then Flash)
                for _ in range(40):
                    await pilot.pause(0.05)
                    if "FLASH OK" in app.build_out:
                        break
                writes = [c for c in calls if "--info" not in c]
                check("confirming writes the package with --yes, never --force, and reports it",
                      writes == [[str(pkg), "--yes"]] and "FLASH OK" in app.build_out and "done:" in app.build_out)
                FL.run_tool = lambda args, capture=True, on_line=None: (3, "error: FM-1 not found")
                await pilot.press("f")
                for _ in range(40):
                    await pilot.pause(0.05)
                    if "FLASH REFUSED" in app.build_out:
                        break
                check("no FM-1 port: refused in the panel, no confirmation screen",
                      "FLASH REFUSED" in app.build_out and "no FM-1 found" in app.build_out and
                      type(app.screen).__name__ != "Pick")
        finally:
            FL.run_tool, FL.FI.load_package, FL.last_package = saved
    check("the menu's last-used and default .config paths follow configure's config folder (main checkout in a worktree)",
          M.LAST == C.CONFIG / "last-used.txt")
    wt_probe = Path(tempfile.mkdtemp(prefix="wtaware-menu-")).resolve()
    try:                                                  # a linked worktree of a temporary repo: remember() writes to the main one
        main, wt = wt_probe / "main", wt_probe / "wt"
        (main / "config" / "profiles").mkdir(parents=True)
        git = lambda *a: subprocess.run(["git", "-C", str(main), "-c", "user.name=t", "-c", "user.email=t@t", *a],
                                        check=True, capture_output=True)
        git("init", "-q", "-b", "main")
        git("commit", "-q", "--allow-empty", "-m", "one")
        git("worktree", "add", "-q", str(wt), "-b", "x")
        (wt / "config").mkdir(exist_ok=True)
        shutil.copytree(ROOT / "tools", wt / "tools", ignore=shutil.ignore_patterns("venv", "__pycache__", "toolchain*"))
        probe = "import sys; sys.path.insert(0, 'tools/builder'); import menu as M; M.remember('profile', 'p'); print(M.LAST)"
        for local, where in ((None, main), ("1", wt)):
            env = {k: v for k, v in os.environ.items() if k not in ("OPTIMIST_LOCAL_PROFILES", "OPTIMIST_SHARED_NOTED")}
            if local:
                env["OPTIMIST_LOCAL_PROFILES"] = local
            run = subprocess.run([sys.executable, "-c", probe], cwd=wt, env=env, capture_output=True, text=True)
            check(f"worktree: remember() writes last-used.txt in {'the main checkout' if where == main else 'the worktree (local profiles)'}",
                  run.returncode == 0 and (where / "config" / "last-used.txt").read_text() == "profile\np\n" and
                  Path(run.stdout.strip()) == where / "config" / "last-used.txt")
            (where / "config" / "last-used.txt").unlink(missing_ok=True)
    finally:
        shutil.rmtree(wt_probe, ignore_errors=True)
    print("builder menu test " + ("FAILED" if fails else "passed"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
