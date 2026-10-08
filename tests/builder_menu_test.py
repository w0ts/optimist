#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The firmware builder's menu (tools/builder/menu.py) headless, through Textual's App.run_test: an error of
validate() shows on the lines of the items it names, in red, as soon as the configuration becomes invalid (no b),
and goes once it is fixed; the message panel lists it; b on an invalid configuration says CANNOT BUILD and keeps
it until the next b. No build here. Needs Textual: run with the builder's venv (tools/menuconfig makes it;
tests/run_tests.sh uses it when it is there)."""
import asyncio
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "builder"))
import configure as C  # noqa: E402
import menu as M  # noqa: E402

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
    # BLE replaces samples (tools/builder/room.py): the menu's toggling
    ud, _ = C.load_profile("user-default")
    app = M.Builder(dict(ud), "user default")
    async with app.run_test(size=(200, 60)) as pilot:
        await pilot.pause()
        await toggle(app, pilot, "BLE")
        check("BLE ticked on user-default: FLUTE goes off at once, BLE stays on, nothing else changes",
              app.cfg["BLE"] == 1 and app.cfg["SET_FLUTE"] == 0 and
              {k for k in app.cfg if app.cfg[k] != ud[k]} == {"BLE", "SET_FLUTE"} and "[ ]" in label(app, "SET_FLUTE"))
        check("... the flash fits again (no OVER in the bars)", not app.over)
        check("... the message panel says what went and offers the others with their sizes",
              "FLUTE samples removed to make room" in panel(app) and "HORNS samples 26 KB" in panel(app) and
              "PIANO samples 44 KB" in panel(app))
        await toggle(app, pilot, "SET_HORNS")
        check("HORNS unticked instead: FLUTE is back, HORNS off, still fits",
              app.cfg["SET_FLUTE"] == 1 and app.cfg["SET_HORNS"] == 0 and not app.over and "[x]" in label(app, "SET_FLUTE"))
        check("... the panel says FLUTE came back", "FLUTE samples restored" in panel(app))
        await toggle(app, pilot, "BLE")
        check("BLE unticked: HORNS (the user's own pick) stays off, FLUTE stays on, BLE off",
              app.cfg["BLE"] == 0 and app.cfg["SET_HORNS"] == 0 and app.cfg["SET_FLUTE"] == 1)
    app = M.Builder(dict(ud), "user default")
    async with app.run_test(size=(200, 60)) as pilot:
        await pilot.pause()
        await toggle(app, pilot, "BLE")
        await toggle(app, pilot, "BLE")
        check("BLE ticked then unticked: user-default exactly as it was (FLUTE restored)", app.cfg == ud and not app.over)
        await toggle(app, pilot, "BLE")
        await toggle(app, pilot, "SET_FLUTE")
        check("FLUTE ticked by hand while BLE is on: it stays on (the overflow is shown, not hidden)",
              app.cfg["SET_FLUTE"] == 1 and bool(app.over))
        await toggle(app, pilot, "BLE")
        check("... and BLE off then leaves FLUTE as the hand set it", app.cfg == ud)
    roomy, _ = C.load_profile("x0x-drums")
    app = M.Builder(dict(roomy), "x0x drums")
    async with app.run_test(size=(200, 60)) as pilot:
        await pilot.pause()
        await toggle(app, pilot, "BLE")
        check("a profile where BLE fits as it is: nothing is removed, no BLE message",
              app.cfg == dict(roomy, BLE=1) and "to make room" not in panel(app))
    print("builder menu test " + ("FAILED" if fails else "passed"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
