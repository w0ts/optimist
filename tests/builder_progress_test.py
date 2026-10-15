# SPDX-License-Identifier: GPL-3.0-only
"""Progress for building and flashing (tools/progress.py): the stage line tools/build.py prints, the parser of
fm1_install.py's progress output, and the streaming runner both the menu and the CLI use. No device, no toolchain."""
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import progress as P  # noqa: E402

fails = 0


def check(what, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + what)
    fails += not ok


# ---- the stage line build.py prints and the menu reads
line = P.stage_line(3, 5, "app")
check("stage: the line round-trips through the parser", P.parse_stage(line) == (3, 5, "app"))
check("stage: a label with spaces and punctuation", P.parse_stage(P.stage_line(1, 5, "toolchain, config")) ==
      (1, 5, "toolchain, config"))
for junk in ("", "app      build/felucca.bin  538900 B", "[3/5] app", "==> 3/5 app", "==> [3/5]", "==> [0/5] x",
             "==> [6/5] x", "  ==> [3/5] app", "loader: image 8908 B"):
    check(f"stage: not a stage line: {junk!r}", P.parse_stage(junk) is None)
check("stage: no stage line contains 'package' or 'error' (the menu's output filter keeps those words)",
      all(not any(w in P.stage_line(1, 5, lab) for w in ("package", "error", "FAIL", "over"))
          for lab in P.BUILD_STAGES))

# ---- the bar's value at the start of each stage: weighted by the time the stages take, rising, never 100
full = [P.build_percent(n, 5) for n in range(1, 6)]
check("bar: a build's first stage starts at 0, every later one higher, none reaches 100",
      full[0] == 0 and all(a < b for a, b in zip(full, full[1:])) and full[-1] < 100)
check("bar: the app stage (most of the time) starts early and the checks late: 'app' at ~19, 'checks' at ~91",
      18 <= full[2] <= 20 and 90 <= full[3] <= 92)
meas = [P.build_percent(n, 4) for n in range(1, 5)]
check("bar: a measurement build (4 stages, no package) also runs 0 up to below 100",
      meas[0] == 0 and all(a < b for a, b in zip(meas, meas[1:])) and meas[-1] < 100)
check("bar: a stage count this module does not know still gives a rising bar",
      [P.build_percent(n, 7) for n in (1, 4, 7)] == sorted([P.build_percent(n, 7) for n in (1, 4, 7)])
      and P.build_percent(1, 7) == 0)

# ---- fm1_install.py's progress output
check("flash: checking the package gives the number of reads, no total",
      P.parse_flash("checking the package (7 reads)") == ("check", 7))
check("flash: writing gives the percentage (the tool pads it to 3 columns)",
      P.parse_flash("writing   0%") == ("write", 0) and P.parse_flash("writing  42%") == ("write", 42)
      and P.parse_flash("writing 100%") == ("write", 100))
check("flash: the mode switch is a phase of its own", P.parse_flash("switching to update mode") == ("switch", 0))
check("flash: restarting is the last phase", P.parse_flash("restarting") == ("restart", 100))
for junk in ("", "done: the FM-1 runs FM-1_700", "starting the update on FM-1_700", "writing 101%", "writing x%"):
    check(f"flash: no progress in {junk!r}", P.parse_flash(junk) is None)

# ---- one overall flash percentage that only rises. The loader reads the package twice (its check, then the write),
# so the tool's "writing N%" climbs to ~98 and starts again at ~2 (measured, 2026-10-14): the first pass is 5-50,
# the second 50-98, the app's own check before it 0-5, restarting 100.
RUN = [("check", 1), ("check", 19), ("switch", 0), ("write", 0), ("write", 2), ("write", 60), ("write", 98),
       ("write", 2), ("write", 3), ("write", 97), ("write", 100), ("restart", 100)]
t = P.FlashTracker()
overall = [t.feed(e) for e in RUN]
check("flash overall: never decreasing over a whole run, in 0..100",
      all(a <= b for a, b in zip(overall, overall[1:])) and all(0 <= v <= 100 for v in overall))
check("flash overall: the app's check stays under 5", overall[0] < 5 and overall[1] < 5)
check("flash overall: the loader's first pass is 5-50", all(5 <= v <= 50 for v in overall[3:7]))
check("flash overall: the second pass (after the drop 98 -> 2) is 50-98", all(50 <= v <= 98 for v in overall[7:11]))
check("flash overall: restart is 100", overall[-1] == 100)
check("flash overall: a package the loader needs one pass for (no drop) still ends at 100",
      P.FlashTracker().feed(("restart", 100)) == 100)
check("flash overall: a fresh tracker starts again from 0", P.FlashTracker().feed(("check", 1)) < 5)

# ---- the streaming runner: lines and carriage-return updates arrive one by one, the full text comes back
with tempfile.TemporaryDirectory() as td:
    script = Path(td) / "emit.py"
    script.write_text(
        "import sys, time\n"
        "sys.stdout.write('first\\n'); sys.stdout.flush()\n"
        "for i in (0, 50, 100):\n"
        "    sys.stdout.write(f'\\rwriting {i:3d}%'); sys.stdout.flush(); time.sleep(0.02)\n"
        "sys.stdout.write('\\nlast\\n'); sys.stdout.flush(); sys.stderr.write('err line\\n'); sys.exit(3)\n")
    seen = []
    rc, text = P.stream([sys.executable, str(script)], on_line=seen.append)
    check("stream: the exit code comes back", rc == 3)
    check("stream: every line and every carriage-return update reached the callback, in order",
          [s for s in seen if s.startswith("writing")] == ["writing   0%", "writing  50%", "writing 100%"]
          and seen[0] == "first" and "last" in seen and "err line" in seen)
    check("stream: the text holds everything (stderr too)", "first" in text and "last" in text and "err line" in text)
    rc, text = P.stream([sys.executable, "-c", "print('quiet')"])
    check("stream: without a callback it still runs and returns the text", rc == 0 and text.strip() == "quiet")
    rc, text = P.stream(["/nonexistent/tool-for-the-progress-test"])
    check("stream: a missing program is a failure with a message, not an exception", rc != 0 and text)
    boom = []
    rc, _ = P.stream([sys.executable, "-c", "print('x')"], on_line=lambda s: boom.append(1) or 1 / 0)
    check("stream: a callback that raises does not kill the run or lose the exit code", rc == 0 and boom)

print("progress: " + ("all passed" if not fails else f"{fails} FAILED"))
sys.exit(1 if fails else 0)
