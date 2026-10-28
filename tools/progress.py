# SPDX-License-Identifier: GPL-3.0-only
"""Progress for building and flashing, shared by the builder menu (tools/builder/menu.py) and the CLI.

  stage_line / parse_stage   the line tools/build.py prints at the start of each build stage, and its reader
  parse_flash / FlashTracker fm1_install.py's progress output -> one overall percentage that only rises
  stream                     run a program and hand its output on as it arrives (lines and \\r updates)

Nothing here touches a device or a toolchain."""
import codecs
import os
import queue
import re
import signal
import subprocess
import threading
import time

# the stages of a build, in order (build.py prints one stage line before each; a measurement build stops after
# the checks). The labels avoid the words the menu's output filter keeps (package, error, FAIL, over).
BUILD_STAGES = ("toolchain and config", "generators and loader", "app", "checks", "pack")

# how much of the bar each stage takes: the share of a build's time (measured 2026-10-15, user-default through the
# Docker toolchain: 0.1 s, 2.5 s, 10 s, 0.9 s, 0.3 s of 13.8 s; the app's compile and link is most of it)
BUILD_WEIGHTS = (1, 18, 72, 7, 2)

STAGE_RE = re.compile(r"^==> \[(\d+)/(\d+)\] (.+)$")
ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
CHECK_RE = re.compile(r"^checking the package \((\d+) reads\)$")
WRITE_RE = re.compile(r"^writing +(\d{1,3})%$")


def stage_line(n, total, label):
    return f"==> [{n}/{total}] {label}"


def parse_stage(line):
    """-> (n, total, label) for a stage line, else None"""
    m = STAGE_RE.match(line)
    if not m:
        return None
    n, total = int(m[1]), int(m[2])
    return (n, total, m[3]) if 1 <= n <= total else None


def build_percent(n, total):
    """the bar's value (0..99) when stage n of total starts: the weights of the stages before it, as a share of all
    of them (a measurement build has no package stage: its total is one less). Never 100: the build is not done"""
    weights = BUILD_WEIGHTS[:total] if total <= len(BUILD_WEIGHTS) else (1,) * total
    return min(99, sum(weights[:n - 1]) * 100 // sum(weights))


def parse_flash(line):
    """one line of fm1_install.py's output -> (kind, value) or None: ("check", reads) while the package is read
    back, ("switch", 0), ("write", percent 0..100), ("restart", 100)"""
    s = ANSI_RE.sub("", line).strip()
    if s == "switching to update mode":
        return ("switch", 0)
    if s == "restarting":
        return ("restart", 100)
    m = CHECK_RE.match(s)
    if m:
        return ("check", int(m[1]))
    m = WRITE_RE.match(s)
    if m and int(m[1]) <= 100:
        return ("write", int(m[1]))
    return None


class FlashTracker:
    """fm1_install.py events -> one overall percentage that only rises. The Felucca loader reads the package twice
    (its own check, then the write), so the tool's "writing N%" climbs to ~98 and starts again at ~2 (measured
    2026-10-14): the app's check is 0-4, the first pass 5-50, the second 50-98, restarting 100."""
    def __init__(self):
        self.top = 0
        self.last = -1
        self.passno = 0

    def feed(self, event):
        kind, v = event
        if kind == "check":
            now = min(4, v // 4)
        elif kind == "switch":
            now = 5
        elif kind == "write":
            if self.last >= 0 and v < self.last - 10:
                self.passno = min(self.passno + 1, 1)
            self.last = v
            now = 5 + v * 45 // 100 if self.passno == 0 else 50 + v * 48 // 100
        else:
            now = 100
        self.top = max(self.top, now)
        return self.top


def stream(cmd, on_line=None, cwd=None, env=None, cancel=None):
    """run cmd (stderr merged into stdout) -> (exit code, the whole output). on_line(text) gets every line and every
    carriage-return update (ANSI codes stripped) as it arrives; an exception in it is ignored: progress is never
    worth a failed build or flash. A program that cannot start is (127, the reason).
    The run ends when the program ends, not when its pipe closes (a grandchild that outlives it, as a toolchain's
    helper in a container, keeps the pipe open): the output is read for a moment more and left. It has no stdin (a
    program that reads the terminal under a full-screen menu would stop there for good) and its own process group.
    cancel: a threading.Event; set, the program is killed and the result is (130, the output so far + a note)"""
    try:
        p = subprocess.Popen(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0,
                             stdin=subprocess.DEVNULL, **({"start_new_session": True} if os.name == "posix" else {}))
    except OSError as e:
        return 127, f"{cmd[0]}: {e}"
    dec = codecs.getincrementaldecoder("utf-8")(errors="replace")
    chunks, tail = [], ""
    q = queue.Queue()

    def reader():                                       # (a daemon thread: it may stay blocked on a leftover pipe)
        fd = p.stdout.fileno()
        try:
            while True:
                data = os.read(fd, 4096)
                q.put(data)
                if not data:
                    return
        except (OSError, ValueError):
            q.put(b"")
    threading.Thread(target=reader, daemon=True).start()

    def emit(segments):
        for seg in segments:
            seg = ANSI_RE.sub("", seg)
            if seg.strip() and on_line:
                try:
                    on_line(seg)
                except Exception:
                    pass

    def kill():
        try:
            os.killpg(p.pid, signal.SIGKILL) if os.name == "posix" else p.kill()
        except (OSError, ProcessLookupError):
            pass
    cancelled, grace = False, None
    while True:
        try:
            data = q.get(timeout=0.2)
        except queue.Empty:
            if cancel is not None and cancel.is_set():
                cancelled = True
                kill()
                break
            if p.poll() is not None:                    # (ended, its pipe still open: a little more output, then go)
                grace = grace or time.monotonic() + 1.0
                if time.monotonic() > grace:
                    break
            continue
        if not data:
            break
        text = dec.decode(data)
        chunks.append(text)
        parts = re.split(r"[\r\n]", tail + text)
        tail = parts.pop()
        emit(parts)
    rest = dec.decode(b"", final=True)
    chunks.append(rest)
    emit(re.split(r"[\r\n]", tail + rest))
    try:
        p.stdout.close()
    except OSError:
        pass
    if cancelled:
        p.wait()
        return 130, "".join(chunks) + "\nbuild: cancelled"
    return p.wait(), "".join(chunks)


# ---- the release build (fm1-base's build.py prints no stage lines): what it prints, as a label and a verdict
NOISE_RE = re.compile(r"^\s*(warn|note)\b", re.I)
OVERFLOW_RE = re.compile(r"region `(\w+)' overflowed by (\d+) bytes")
ERROR_RE = re.compile(r"\b(error|failed|fail|undefined reference|cannot|no such file)\b", re.I)
EXIT_RE = re.compile(r"^build: exit status (\d+)$", re.M)


def build_label(line):
    """one line of a build's output -> a short label for the bar (the line itself, trimmed), or None for noise"""
    s = ANSI_RE.sub("", line).strip()
    if not s or NOISE_RE.match(s):
        return None
    return s if len(s) <= 90 else s[:89] + "\u2026"


def failure_reason(out):
    """the output of a failed build -> one plain line: the linker's overflow ("does not fit: XIP overflowed by 9,836 B"),
    else the last line that looks like an error, else the last line"""
    text = ANSI_RE.sub("", out or "")
    over = OVERFLOW_RE.findall(text)
    if over:
        return "does not fit: " + ", ".join(f"{r} overflowed by {int(n):,} B" for r, n in dict.fromkeys(over))
    lines = [ln.strip() for ln in text.splitlines() if ln.strip() and not NOISE_RE.match(ln) and not EXIT_RE.match(ln.strip())]
    errs = [ln for ln in lines if ERROR_RE.search(ln)]
    pick = (errs or lines or ["the build left no output"])[-1]
    return pick if len(pick) <= 160 else pick[:159] + "\u2026"


def exit_status(out):
    """-> the exit status build() noted at the end of a failed build's output, or None"""
    m = EXIT_RE.findall(out or "")
    return int(m[-1]) if m else None
