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
import room as RM
from ble_room_rule import expected_room, off_value, with_ble  # (tests/: the rule, from costs.json alone)

fails = 0


def check(what, ok):
    global fails
    print(f"{what:78s} {'ok' if ok else 'FAIL'}")
    fails += not ok


def label(app, key):
    return app.nodes[key].label.plain


def panel(app):
    return str(app.query_one("#msgs_t").content)


async def toggle(app, pilot, key):
    tree = app.query_one("#tree")
    tree.move_cursor(app.nodes[key])
    await pilot.press("space")
    await pilot.pause()


async def main():
    app = M.Builder(C.defaults(), "default")
    async with app.run_test(size=(200, 60)) as pilot:
        await pilot.pause()
        check("the default configuration: no line marked", not any("✗" in label(app, k) for k in app.nodes))
        await toggle(app, pilot, "DRUM_SYNTH")
        await toggle(app, pilot, "DRUM_SAMPLED")
        marked = [k for k in app.nodes if "✗" in label(app, k)]
        check("drum synth and sampled kits off: both lines marked at once, before any b",
              "DRUM_SYNTH" in marked and "DRUM_SAMPLED" in marked and "drum source" in label(app, "DRUM_SYNTH") and
              not app.build_out)
        styles = [s.style for s in app.nodes["DRUM_SYNTH"].label.spans if "✗" in app.nodes["DRUM_SYNTH"].label.plain[s.start:s.end]]
        check("... in red", any("red" in str(s) for s in styles))
        check("... only the items the error names", all(k in ("DRUM_SYNTH", "DRUM_SAMPLED") or k.startswith("KIT_")
                                                        for k in marked))
        check("... the message panel lists the error", "ERROR" in panel(app) and "drum source" in panel(app))
        await pilot.press("b")
        await pilot.pause()
        check("b: CANNOT BUILD in the panel, nothing built", app.build_out.startswith("CANNOT BUILD") and
              panel(app).startswith("CANNOT BUILD"))
        await toggle(app, pilot, "DRUM_SYNTH")
        check("the drum synth back: the marks go at once", not any("✗" in label(app, k) for k in app.nodes))
        check("... CANNOT BUILD stays in the panel until the next b", panel(app).startswith("CANNOT BUILD"))
        for k in ("FM6_MARK1", "FM6_MODERN", "FM6_OPL"):
            if not app.cfg[k]:
                continue
            app.query_one("#tree").move_cursor(app.nodes["ENG_FM6"])
            app.nodes["ENG_FM6"].expand()
            await pilot.pause()
            await toggle(app, pilot, k)
        check("FM6 without an ENGINE mode: FM6 and its three modes marked",
              all("✗" in label(app, k) for k in ("ENG_FM6", "FM6_MARK1", "FM6_MODERN", "FM6_OPL")))
        await toggle(app, pilot, "FM6_MARK1")
        check("... a mode back: no mark", not any("✗" in label(app, k) for k in app.nodes))
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
    # BLE replaces samples (tools/builder/room.py): the menu's toggling, for every profile, with and without BLE_DIAG.
    # The expected item is computed from costs.json as it is (tests/ble_room_rule.py), nothing is pinned
    costs = C.load_costs()
    for prof in ("user-default", "drum-machine", "everything-that-fits", "fm-va-studio", "x0x-drums"):
        for diag in (0,):
            base, _ = C.load_profile(prof)
            tag = f"BLE ticked on {prof}{' with BLE_DIAG' if diag else ''}"
            withble = with_ble(base, diag)
            over, order, chosen = expected_room(withble, costs)
            others = [k for k in order if k != chosen]
            app = M.Builder(dict(base), prof)
            async with app.run_test(size=(200, 60)) as pilot:
                await pilot.pause()
                if diag:                  # (BLE_DIAG first: ticking an option switches its parent on)
                    app.nodes["BLE"].expand()
                    await toggle(app, pilot, "BLE_DIAG")
                else:
                    await toggle(app, pilot, "BLE")
                if not over:
                    check(f"{tag}: it fits as it is: nothing is removed, no message",
                          app.cfg == withble and "to make room" not in panel(app) and not app.over)
                    continue
                if chosen is None:
                    check(f"{tag}: it overflows and no single item frees enough: nothing is removed, the panel says so, "
                          "the overflow is shown",
                          app.cfg == withble and "no single item frees enough" in panel(app) and bool(app.over))
                    continue
                check(f"{tag}: {chosen} goes off at once, BLE stays on, nothing else changes",
                      app.cfg["BLE"] == 1 and app.cfg[chosen] == off_value(chosen) and
                      {k for k in app.cfg if app.cfg[k] != base[k]} == {"BLE", chosen} | ({"BLE_DIAG"} if diag else set()) and
                      "[ ]" in label(app, chosen))
                check("... the build fits again (no OVER in the bars)", not app.over)
                check("... the message panel says what went and offers the others with their sizes",
                      f"{RM.short(chosen)} removed to make room" in panel(app) and
                      all(f"{RM.short(k)} {RM.kb(C.savings_of(withble, costs)[k]['flash'])}" in panel(app)
                          for k in others[:RM.OFFERS]))
                if others:
                    pick = others[0]
                    if pick.startswith("SET_") or "[" in label(app, pick):
                        await toggle(app, pilot, pick)
                    back = not C.over_any(dict(withble, **{pick: off_value(pick)}), costs)
                    check(f"{tag}: {RM.short(pick)} unticked instead: " + (f"{RM.short(chosen)} is back, the pick off, it fits"
                          if back else f"{RM.short(chosen)} stays removed"),
                          (app.cfg[chosen] == base[chosen] and app.cfg[pick] == off_value(pick) and not app.over and
                           f"{RM.short(chosen)} restored" in panel(app)) if back else
                          (app.cfg[chosen] == off_value(chosen) and app.cfg[pick] == off_value(pick)))
                    await toggle(app, pilot, "BLE")
                    check("... BLE unticked: the user's own pick stays off, BLE off" + (", the first one stays on" if back else
                          ", the first one comes back"),
                          app.cfg["BLE"] == 0 and app.cfg[pick] == off_value(pick) and app.cfg[chosen] == base[chosen])
    for prof in ("user-default", "drum-machine", "fm-va-studio", "x0x-drums"):    # (BLE_DIAG ticked after BLE)
        base, _ = C.load_profile(prof)
        app = M.Builder(dict(base), prof)
        async with app.run_test(size=(200, 60)) as pilot:
            await pilot.pause()
            await toggle(app, pilot, "BLE")
            after_ble = dict(app.cfg)
            app.nodes["BLE"].expand()
            await toggle(app, pilot, "BLE_DIAG")
            check(f"BLE_DIAG ticked after BLE on {prof}: only it changes, nothing more is removed, the bars show what is left over",
                  app.cfg == dict(after_ble, BLE_DIAG=1) and bool(app.over) == bool(C.over_any(app.cfg, costs)))
    for prof in ("fm-va-studio", "x0x-drums"):
        base, _ = C.load_profile(prof)
        over, order, chosen = expected_room(with_ble(base, 0), costs)
        if not chosen:
            continue
        app = M.Builder(dict(base), prof)
        async with app.run_test(size=(200, 60)) as pilot:
            await pilot.pause()
            await toggle(app, pilot, "BLE")
            await toggle(app, pilot, "BLE")
            check(f"{prof}: BLE ticked then unticked: exactly as it was ({RM.short(chosen)} restored)", app.cfg == base and not app.over)
            await toggle(app, pilot, "BLE")
            await toggle(app, pilot, chosen)
            check(f"{prof}: {RM.short(chosen)} ticked by hand while BLE is on: it stays on (the overflow is shown, not hidden)",
                  app.cfg[chosen] == base[chosen] and bool(app.over))
            await toggle(app, pilot, "BLE")
            check("... and BLE off then leaves it as the hand set it", app.cfg == base)
    app = M.Builder(C.defaults(), "default")             # (the Reserve items: the first group, the ring in the bars)
    async with app.run_test(size=(200, 60)) as pilot:
        await pilot.pause()
        check("the Reserve items are the first lines of the menu",
              [n.data for n in app.query_one("#tree").root.children[0].children] == ["RESERVE_UNDO_KB", "RESERVE_FLASH_KB"])
        bars = lambda: str(app.query_one("#bars").content)
        check("the bars show the undo ring", "UNDO" in bars() and "ring" in bars())
        flash0 = C.fits(C.budget(app.cfg, app.costs)["total"], app.cfg)["flash"][1]
        await toggle(app, pilot, "RESERVE_FLASH_KB")      # (the next value: 8 KB)
        check("flash reserve 8 KB: the budget's slot limit is 8 KB smaller",
              app.cfg["RESERVE_FLASH_KB"] == 8 and C.fits(C.budget(app.cfg, app.costs)["total"], app.cfg)["flash"][1] == flash0 - 8192)
        app.cfg["RESERVE_UNDO_KB"] = 32
        app.refresh_all()
        await pilot.pause()
        short = C.undo_short(app.cfg, C.budget(app.cfg, app.costs)["total"])
        check("undo reserve 32 KB: the bars say what is kept, the panel warns when the ring falls short",
              "keep at least 32,768" in bars() and (not short or "below the 32,768 B kept" in panel(app)))
        app.cfg["UNDO_HISTORY"] = 0
        app.refresh_all()
        await pilot.pause()
        check("... without the undo history it is an error on the reserve line", "✗" in label(app, "RESERVE_UNDO_KB"))
    # f: Flash to FM-1 (fm1_install mocked: no device is touched)
    import flash as FL
    calls = []

    def fake_tool(args, capture=True):
        calls.append(list(args))
        return (0, "Optimist_705  [running]  port: FM-1") if "--info" in args else (0, "writing 100%\ndone: the FM-1 runs Optimist_705")
    saved = (FL.run_tool, FL.FI.load_package, FL.last_package)
    FL.run_tool, FL.FI.load_package = fake_tool, (lambda path, force: ("FM-1_705", b""))
    with tempfile.TemporaryDirectory() as td:
        pkg = Path(td) / "optimist-1.0-x.fwsc"
        pkg.write_bytes(b"x")
        FL.last_package = lambda build=None: None
        try:
            app = M.Builder(C.defaults(), "default")
            async with app.run_test(size=(200, 60)) as pilot:
                await pilot.pause()
                check("f is bound: Flash to FM-1 in the footer", any(b.key == "f" and "flash" in b.description for b in app.BINDINGS))
                await pilot.press("f")
                await pilot.pause()
                check("f with no build: says so, nothing asked, no device touched",
                      type(app.screen).__name__ != "Pick" and not calls)
                app.show_build("BUILD OK\n  f: Flash to FM-1", {}, pkg)
                check("a successful build offers it: the panel says so and remembers the package",
                      app.built_pkg == pkg and "Flash to FM-1" in app.build_out)
                app.show_build("BUILD FAILED", {}, None)
                check("a failed build forgets the package", app.built_pkg is None)
                app.show_build("BUILD OK\n  f: Flash to FM-1", {}, pkg)
                await pilot.press("f")
                for _ in range(40):
                    await pilot.pause(0.05)
                    if type(app.screen).__name__ == "Pick":
                        break
                title = str(app.screen.title_) if type(app.screen).__name__ == "Pick" else ""
                check("f: a confirmation screen with the running identity, the package and the recovery",
                      "Optimist_705  [running]" in title and pkg.name in title and "FM-1_705" in title and
                      "Rescue, going back" in title and app.screen.options[0] == "Cancel")
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
                FL.run_tool = lambda args, capture=True: (3, "error: FM-1 not found")
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
