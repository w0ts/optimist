# SPDX-License-Identifier: GPL-3.0-only
"""Progress for building and flashing, shared by the builder menu (tools/builder/menu.py) and the CLI.

  stage_line / parse_stage   the line tools/build.py prints at the start of each build stage, and its reader
  parse_flash / FlashTracker fm1_install.py's progress output -> one overall percentage that only rises
  stream                     run a program and hand its output on as it arrives (lines and \\r updates)

Nothing here touches a device or a toolchain."""
import codecs
import os
import re
import subprocess

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


def stream(cmd, on_line=None, cwd=None, env=None):
    """run cmd (stderr merged into stdout) -> (exit code, the whole output). on_line(text) gets every line and every
    carriage-return update (ANSI codes stripped) as it arrives; an exception in it is ignored: progress is never
    worth a failed build or flash. A program that cannot start is (127, the reason)"""
    try:
        p = subprocess.Popen(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
    except OSError as e:
        return 127, f"{cmd[0]}: {e}"
    dec = codecs.getincrementaldecoder("utf-8")(errors="replace")
    chunks, tail = [], ""

    def emit(segments):
        for seg in segments:
            seg = ANSI_RE.sub("", seg)
            if seg.strip() and on_line:
                try:
                    on_line(seg)
                except Exception:
                    pass

    fd = p.stdout.fileno()
    while True:
        data = os.read(fd, 4096)
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
    p.stdout.close()
    return p.wait(), "".join(chunks)
