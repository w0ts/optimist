#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The builder menu's build step (tools/builder/menu.py run_build, tools/progress.py stream) against a fake release
build command: (1) it prints progress and succeeds: the bar's label follows the output; (2) it prints the linker's
XIP overflow and exits 1: the panel says "does not fit: XIP overflowed by 9,836 B" and the menu is free again; (3) it
hangs: q kills it, the worker thread ends and no process is left; plus the streaming runner on a program whose
grandchild keeps the pipe open. Textual's pilot, no toolchain. Needs Textual (the builder's venv)."""
import asyncio
import os
os.environ["OPTIMIST_CLASSIC"] = "0"             # (the release path: fm1base.build runs the command)
import shutil
import sys
import tempfile
import threading
import time
from pathlib import Path

from builder_path import ROOT, TOOLS
import configure as C  # noqa: E402
import menu as M  # noqa: E402
import fm1base  # noqa: E402
import progress as PG  # noqa: E402

C.exact_sizes = lambda cfg: None
C.exact_builds = lambda: {}
fails = 0
SCRATCH = Path(tempfile.mkdtemp(prefix="bstep-", dir=ROOT / "build" if (ROOT / "build").is_dir() else ROOT)).resolve()
os.environ["OPTIMIST_BUILD_DIR"] = SCRATCH.relative_to(ROOT.resolve()).as_posix()
PIDFILE = SCRATCH / "pid"


def check(what, ok):
    global fails
    print(f"{what:84s} {'ok' if ok else 'FAIL'}")
    fails += not ok


OK = ("import time\nfor i in range(3):\n    print('size: stage %d of the fake build' % i, flush=True)\n"
      "    time.sleep(0.7)\nprint('package  fake', flush=True)\n")
LD = ("print('ir_pass: 12 functions at optsize', flush=True)\n"
      "print('/opt/jieli/pi32v2/bin/ld: optimist/build/optimist.elf section `.text\\' will not fit in region `XIP\\'')\n"
      "print('/opt/jieli/pi32v2/bin/ld: region `XIP\\' overflowed by 9836 bytes')\n"
      "print('build: pi32v2/bin/ld failed')\nraise SystemExit(1)\n")
HANG = (f"import os, time\nopen({str(PIDFILE)!r}, 'w').write(str(os.getpid()))\n"
        "print('ir_pass: waiting forever', flush=True)\ntime.sleep(600)\n")


def fake(script):
    def build(cfgfile, bdir, python, run, release=None, name_stem="optimist", parallel=False, measure=False):
        return run([sys.executable, "-u", "-c", script], dict(os.environ))
    return build


def label(app):
    return str(app.query_one("#prog_label").content)


def prog_on(app):
    return app.query_one("#prog").has_class("on")


def alive(pid):
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


STEP_DONE = threading.Event()                      # (the worker's build step returned: its thread is free)
_steps = M.Builder.build_steps


def _noted(self, *a, **k):
    try:
        return _steps(self, *a, **k)
    finally:
        STEP_DONE.set()


M.Builder.build_steps = _noted


async def make(pilot_size=(200, 60)):
    app = M.Builder(C.defaults(), "default")
    est = C.budget(app.cfg, app.costs)
    cap = {k: v for k, v in est["total"].items()}
    app.budget = lambda cfg=None: dict(est, total=dict(cap, flash=0, ram=0, pool=0, ramtext=0), exact=False)
    return app


async def wait_for(pred, secs):
    end = time.time() + secs
    while time.time() < end:
        if pred():
            return True
        await asyncio.sleep(0.05)
    return pred()


async def case_ok():
    fm1base.build = fake(OK)
    app = await make()
    async with app.run_test(size=(200, 60)) as pilot:
        await pilot.pause()
        app.action_build()
        seen = await wait_for(lambda: "size: stage" in label(app), 5)
        check("1 progress: the bar's label follows the build's output", seen)
        check("1 progress: ... and the bar is shown while it runs", prog_on(app))
        await wait_for(lambda: app.build_out.startswith("BUILD"), 10)
        check("1 success: the panel says BUILD OK", app.build_out.startswith("BUILD OK"))
        check("1 success: the bar is gone and a new build may start", not prog_on(app) and not app.building)


async def case_overflow():
    fm1base.build = fake(LD)
    app = await make()
    async with app.run_test(size=(200, 60)) as pilot:
        await pilot.pause()
        app.action_build()
        await wait_for(lambda: app.build_out.startswith("BUILD"), 15)
        first = app.build_out.splitlines()[0]
        check("2 failure: the reason in plain words, with the exit status",
              first == "BUILD FAILED: does not fit: XIP overflowed by 9,836 B (exit status 1)")
        check("2 failure: the linker's own line stays in the panel", "overflowed by 9836 bytes" in app.build_out)
        check("2 failure: the bar is gone, the menu is free (b works again)", not prog_on(app) and not app.building)
        check("2 failure: nothing to flash", app.built is None)


async def case_postlink():
    """the estimate fits, the real build fails a check after the link, the measurement build fits: the panel says
    the check's failure, not "measurement build: does not fit" (2026-10-29: drums_mix, pi32v2-llvm)"""
    real = C.build
    fail = "  FAIL  RAM code calls through a pointer: drums_mix at 0x1c06f84\nbuild: checks failed\n"
    C.build = lambda cfg, name, measure=False, **k: ((True, {r: 0 for r in C.REGIONS}, "measured\n") if measure
                                                     else (False, None, fail))
    try:
        app = await make()
        async with app.run_test(size=(200, 60)) as pilot:
            await pilot.pause()
            text, _, built = app.build_steps(app.cfg, "default", False)
    finally:
        C.build = real
    check("4 post-link check: the panel says BUILD FAILED", text.startswith("BUILD FAILED"))
    check("4 post-link check: ... with the check's own line", "drums_mix" in text)
    check("4 post-link check: ... not the measurement's 'does not fit'", "measurement build: does not fit" not in text)
    check("4 post-link check: nothing to flash", built is None)


async def case_hang():
    fm1base.build = fake(HANG)
    PIDFILE.unlink(missing_ok=True)
    STEP_DONE.clear()
    app = await make()
    async with app.run_test(size=(200, 60)) as pilot:
        await pilot.pause()
        app.action_build()
        await wait_for(lambda: "waiting forever" in label(app) and PIDFILE.exists(), 10)
        check("3 hang: the label shows the last output line and the seconds", "waiting forever" in label(app)
              and " s)" in label(app))
        pid = int(PIDFILE.read_text())
        await pilot.press("q")
        await pilot.pause()
    check("3 hang: q leaves the app", True)
    gone = await wait_for(lambda: not alive(pid), 5)
    check("3 hang: the build process is killed", gone)
    check("3 hang: the worker's build step returned (its thread is free)", await wait_for(STEP_DONE.is_set, 5))


def stream_tests():
    t = time.time()
    rc, out = PG.stream(["sh", "-c", "sleep 30 & echo hello"])
    check("stream: ends when the program ends although a grandchild holds the pipe", rc == 0 and "hello" in out
          and time.time() - t < 8)
    ev = threading.Event()
    threading.Timer(0.5, ev.set).start()
    t = time.time()
    rc, out = PG.stream(["sh", "-c", "echo start; sleep 60"], cancel=ev)
    check("stream: cancel kills the program (130, a note)", rc == 130 and "cancelled" in out and time.time() - t < 8)
    rc, out = PG.stream(["sh", "-c", "read x; echo got:$x"])
    check("stream: the program has no stdin (a read ends at once)", rc == 0 and "got:" in out)
    check("reason: the linker's overflow", PG.failure_reason("a\n region `XIP' overflowed by 9836 bytes\n")
          == "does not fit: XIP overflowed by 9,836 B")
    check("reason: else the last error line", PG.failure_reason("x\nclang: error: boom\nwarn y\nz\n") ==
          "clang: error: boom")
    check("reason: else the last line", PG.failure_reason("one\ntwo\n") == "two")


async def main():
    try:
        stream_tests()
        await case_ok()
        await case_overflow()
        await case_postlink()
        await case_hang()
    finally:
        shutil.rmtree(SCRATCH, ignore_errors=True)
    print("builder build step test " + ("FAILED" if fails else "passed"))
    return 1 if fails else 0


if __name__ == "__main__":
    rc = asyncio.run(main())          # (returns only when the loop's executor has joined its threads: a hung one would block here)
    left = [t.name for t in threading.enumerate() if t is not threading.main_thread() and not t.daemon]
    check("after the loop closed: no non-daemon thread is left", not left)
    sys.exit(rc or fails)
