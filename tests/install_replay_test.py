#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Step 2 of an install replayed end to end (docs/INSTALL-SPEED.md): tools/fm1_install.py's Updater.write against the
real update loader code (tests/install_replay.c: ldr_core.c and ota.c on stdin / stdout), the old protocol and the fast
one, and a cut-off install resumed. Run from the repo root:
  python3 tests/install_replay_test.py OLD.fwsc NEW.fwsc [--bsp] [--app-end 0xCE000] [--measure]
--bsp: fm1-base's loader (fm1base/bsp, what the release packages carry; --app-end: its slot), else Optimist's own.
--measure: today's pause before each answer kept (DELAY["reply"]), and the host's time per case printed."""
import argparse
import json
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fm1_install as I  # noqa: E402

failed = 0


def ok(cond, what):
    global failed
    print(f"{what:<78} {'ok' if cond else 'FAIL'}")
    failed += not cond


class PipeLink:
    """the "FM-1" over its stdin / stdout, as MidoLink: whole SysEx messages, counted"""

    def __init__(self, proc):
        self.proc, self.q = proc, queue.Queue()
        self.sent = self.sent_bytes = self.got = self.got_bytes = 0
        threading.Thread(target=self._rx, daemon=True).start()

    def _rx(self):
        buf = bytearray()
        while True:
            b = self.proc.stdout.read1(65536) if hasattr(self.proc.stdout, "read1") else self.proc.stdout.read(1)
            if not b:
                return
            buf += b
            while (end := buf.find(0xF7)) >= 0:
                start = buf.find(0xF0)
                if 0 <= start < end:
                    self.got += 1
                    self.got_bytes += end - start + 1
                    self.q.put(bytes(buf[start:end + 1]))
                del buf[:end + 1]

    @property
    def lost(self):
        return self.proc.poll() is not None and self.q.empty()

    def send(self, pkt):
        try:
            self.proc.stdin.write(pkt)
            self.proc.stdin.flush()
        except (BrokenPipeError, OSError):
            return False
        self.sent += 1
        self.sent_bytes += len(pkt)
        return True

    def read(self, timeout):
        try:
            return self.q.get(timeout=max(timeout, 0))
        except queue.Empty:
            return None

    def drain(self):
        while not self.q.empty():
            self.q.get_nowait()

    def close(self):
        pass


def build(tmp, bsp, fast, app_end):
    exe = tmp / f"replay_{'bsp' if bsp else 'own'}_{'fast' if fast else 'old'}"
    flags = ["-w", "-O1"]
    if bsp:
        flags += ["-DREPLAY_BSP", f"-DFL_APP_END={app_end:#x}u"] + (["-DLDR_FAST_CHUNK=4096u"] if fast else [])
    elif fast:
        flags += ["-DOTA_FAST_CHUNK=4096u"]
    if fast:
        flags += ["-DOTA_FRAME_MAX=4200u"]
    subprocess.run(["cc", *flags, "-o", str(exe), str(ROOT / "tests/install_replay.c")], check=True)
    return exe


def replay(exe, old, new, flash, image, *, hello=True, cut=0):
    """one step 2 -> (finished, the loader's counts, the link, host seconds)"""
    proc = subprocess.Popen([str(exe), str(old), str(new), str(flash), *([str(cut)] if cut else [])],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    link = PipeLink(proc)
    up = I.Updater(None)
    if not hello:
        up.fast_hello = lambda _link: False            # (a host from before the fast install)
    t0 = time.monotonic()
    try:
        up.write(I.Device(link, I.Identity("ota-FM-1_700", "ota-FM-1", 700), "replay"), image, lambda *a: None)
        finished = True
    except I.InstallError:
        finished = False
    secs = time.monotonic() - t0
    try:
        proc.stdin.close()
    except OSError:
        pass
    proc.wait(timeout=30)
    err = proc.stderr.read().decode().strip().splitlines()
    stats = json.loads(err[-1]) if err and err[-1].startswith("{") else {}
    return finished, stats, link, secs


class ReplayFM1:
    """a MIDI backend whose one port is the replayed loader (Optimist Update): gone while its process has exited,
    back (the same flash) when replug() starts it again, as the cable going and coming"""

    def __init__(self, exe, old, new, flash, cut):
        self.args, self.procs, self.links = [str(exe), str(old), str(new), str(flash)], [], []
        self.replug(cut)

    def replug(self, cut=0):
        self.procs.append(subprocess.Popen(self.args + ([str(cut)] if cut else []), stdin=subprocess.PIPE,
                                           stdout=subprocess.PIPE, stderr=subprocess.PIPE))
        self.links.append(PipeLink(self.procs[-1]))

    def input_names(self):
        return [] if self.links[-1].lost else ["Optimist Update"]

    output_names = input_names

    def open(self, in_name, out_name):
        if in_name not in self.input_names():
            raise IOError("no such port")
        return self.links[-1]

    def stats(self):
        out = []
        for p in self.procs:
            try:
                p.stdin.close()
            except OSError:
                pass
            p.wait(timeout=30)
            err = p.stderr.read().decode().strip().splitlines()
            out.append(json.loads(err[-1]) if err and err[-1].startswith("{") else {})
        return out


def dropped(exe, old, new, flash, image, full):
    """the cable dropped mid step 2 -> install_resumable looks for the FM-1 by itself, finds the loader again (the cable
    back, no key pressed) and writes on: only the sectors left, twice over; the bar never goes back within a session and
    reaches 100; then cancelled after a drop"""
    I.DELAY.update(resume_poll=0.1, retry=0.1)
    dev = ReplayFM1(exe, old, new, flash, cut=20)
    up = I.Updater(dev)
    up.verify = lambda product, step: product          # (the replayed loader is all there is: no firmware comes back)
    drops = []

    def on_drop(err):
        drops.append((err.code, None))
        threading.Timer(0.4, dev.replug, (15 if len(drops) == 1 else 0,)).start()   # (the cable back a moment later,
        # by itself: nothing is pressed; a second drop, then none)
    steps = []
    first = up.find()
    got = up.install_resumable(first, image, "FM-1_700", lambda k, a=None: steps.append((k, a)), on_drop)
    st = dev.stats()
    ok(got == "FM-1_700" and [d[0] for d in drops] == ["lost", "lost"] and [s.get("rc") for s in st] == [4, 4, 0],
       "drop mid step 2 twice: said each time, resumed by itself on the loader, finished")
    runs, cur = [], []
    for k, a in steps:
        if k == "checking" and a == 0:
            runs.append(cur)
            cur = []
        if k in ("checking", "write") and a is not None and (k, a) != ("write", 0):   # (write() says "write 0" first)
            cur.append(a)
    runs.append(cur)
    ok(len([r for r in runs if r]) == 3 and all(r == sorted(r) for r in runs) and runs[-1][-1] == 100,
       f"... the bar: checking, then the write, never back in each of the 3 sessions (last: {runs[-1][:6]}...)")
    ok(sum(s.get("answers", 0) for s in st) <= full + 2 * 8 and st[2]["answers"] < full,
       f"... only the sectors left each time ({[s.get('answers') for s in st]} answers, one whole write: {full})")
    flash2 = Path(str(flash) + ".cancel")
    dev = ReplayFM1(exe, old, new, flash2, cut=20)
    up = I.Updater(dev)
    try:
        up.install_resumable(up.find(), image, "FM-1_700", lambda *a: None, lambda e: None, lambda: True)
        code = None
    except I.InstallError as e:
        code = e.code
    st = dev.stats()
    ok(code == "cancel" and len(st) == 1 and st[0].get("rc") == 4, "drop, then cancelled: nothing more sent, the loader stays")
    return got


class GapLink(PipeLink):
    """the cable dropped on the host's side only: after `cut` answers the link is gone (what the loader sends is lost, the
    answer in flight too) while the loader process runs on in its session; replug() brings the port back"""

    def __init__(self, proc, cut):
        self.gone, self.cut, self.answers = False, cut, 0
        super().__init__(proc)

    @property
    def lost(self):
        return self.gone or super().lost

    def send(self, pkt):
        if self.gone:
            return False
        if pkt[:4] == I.response(0, b"")[:4] and self.cut:   # (an answer to a read request)
            self.answers += 1
            if self.answers >= self.cut:
                self.gone, self.cut = True, 0
                return False
        return super().send(pkt)

    def read(self, timeout):
        if self.gone:
            time.sleep(min(timeout, 0.05))
            return None
        return super().read(timeout)

    def replug(self):
        """the cable back: what the loader sent meanwhile was lost"""
        self.drain()
        self.gone = False


class KeptFM1:
    """one loader process for the whole install: a drop on the host's side only, so the loader stays in its fast session
    (as the FM-1 did on 2026-11-01): the new hello mid-session goes unheard, it asks its read again after 5 s"""

    def __init__(self, exe, old, new, flash, cut):
        self.proc = subprocess.Popen([str(exe), str(old), str(new), str(flash)], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.link = GapLink(self.proc, cut)

    def input_names(self):
        return [] if self.link.lost else ["Optimist Update"]

    output_names = input_names

    def open(self, in_name, out_name):
        if in_name not in self.input_names():
            raise IOError("no such port")
        return self.link

    def stats(self):
        try:
            self.proc.stdin.close()
        except OSError:
            pass
        self.proc.wait(timeout=30)
        err = self.proc.stderr.read().decode().strip().splitlines()
        return json.loads(err[-1]) if err and err[-1].startswith("{") else {}


def kept(exe, old, new, flash, image, want, lo, hi):
    """the FM-1's case of 2026-11-01: replugged in step 2, the loader still in its fast session never answers the new
    hello; the host must serve its 4 KiB reads on (not refuse them as today's protocol)"""
    dev = KeptFM1(exe, old, new, flash, cut=12)
    up = I.Updater(dev)
    up.verify = lambda product, step: product
    steps = []

    def on_drop(err):
        threading.Timer(0.5, dev.link.replug).start()   # (the cable back)
    got = None
    try:
        got = up.install_resumable(up.find(), image, "FM-1_700", lambda k, a=None: steps.append(k), on_drop)
    except I.InstallError as e:
        print(f"  ({e.code}: {e})")
    st = dev.stats()
    ok(got == "FM-1_700" and "kept" in steps and st.get("rc") == 0 and app_of(flash, lo, hi) == want,
       "drop in step 2, the loader kept its fast session: the unheard hello, its 4 KiB reads served on, finished")


def app_of(flash, lo, hi):
    return Path(flash).read_bytes()[lo:hi]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("old")
    ap.add_argument("new")
    ap.add_argument("--bsp", action="store_true")
    ap.add_argument("--app-end", type=lambda s: int(s, 0), default=0x93000)
    ap.add_argument("--measure", action="store_true")
    a = ap.parse_args()
    I.DELAY.update(start=0.05, start_fast=0.0, fast=0.5, poll=0.2, idle_write=20)
    if not a.measure:
        I.DELAY.update(reply=0)
    raw = Path(a.new).read_bytes()
    _, image = I.product_of(raw), I.logical_image(raw)
    fl_off, fl_size = flash_entry(image)
    end = a.app_end if a.bsp else fl_size
    want = image[fl_off + 0x4000:fl_off + end]
    tmp = Path(tempfile.mkdtemp(prefix="replay-", dir=ROOT / "build"))
    try:
        new_ldr, old_ldr = build(tmp, a.bsp, True, a.app_end), build(tmp, a.bsp, False, a.app_end)
        rows = []
        for name, exe, hello in (("today's protocol (an older loader)", old_ldr, True),
                                 ("an older host, the new loader", new_ldr, False),
                                 ("the fast install", new_ldr, True)):
            flash = tmp / f"flash-{len(rows)}.bin"
            done, st, link, secs = replay(exe, a.old, a.new, flash, image, hello=hello)
            rows.append((name, st, link, secs))
            ok(done and st.get("rc") == 0 and app_of(flash, 0x4000, end) == want,
               f"{name}: finished, the app area is the package's")
        fast, classic = rows[2][1], rows[0][1]
        ok(abs(rows[1][1].get("answers", 0) - classic.get("answers", 0)) <= 1,   # (the fast word: one frame more to an old loader)
            "an older host gets today's protocol from the new loader")
        ok(fast["bytes_in"] * 2 < classic["bytes_in"], "the fast install sends less than half the bytes")
        # a cut-off fast install, resumed: the rest only
        flash = tmp / "flash-cut.bin"
        done, st, _, _ = replay(new_ldr, a.old, a.new, flash, image, cut=30)
        ok(not done and st.get("rc") == 4, "fast: cut off after 30 answers (the FM-1 gone)")
        done, st2, _, _ = replay(new_ldr, a.old, a.new, flash, image)
        ok(done and st2.get("rc") == 0 and app_of(flash, 0x4000, end) == want and st2["answers"] < fast["answers"],
           f"fast: resumed, finished, only the rest sent ({st2['answers']} of {fast['answers']} answers)")
        done, st3, _, _ = replay(new_ldr, a.old, a.new, flash, image)
        ok(done and st3["erases"] == 0 and st3["answers"] <= 6, "fast: the same package again sends no sector")
        flash = tmp / "flash-drop.bin"
        kept(new_ldr, a.old, a.new, tmp / "flash-kept.bin", image, want, 0x4000, end)
        dropped(new_ldr, a.old, a.new, flash, image, fast["answers"])
        ok(app_of(flash, 0x4000, end) == want, "drop and resume: the app area is the package's")
        for name, st, link, secs in rows:
            print(f"  {name}: {st.get('answers')} answers, {link.sent_bytes} B to the FM-1, {link.got_bytes} B back, "
                  f"{st.get('erases')} erases, {st.get('progs')} page programs, host {secs:.2f} s")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("install replay test", "FAILED" if failed else "passed")
    return 1 if failed else 0


def flash_entry(image):
    hdr = I._dec(image[0:0x40])
    for i in range(int.from_bytes(hdr[8:10], "little")):
        e = I._dec(image[0x40 + 0x50 * i:0x40 + 0x50 * (i + 1)])
        if e[0x40:0x50].split(b"\0")[0] == b"flash.bin":
            return int.from_bytes(e[8:12], "little"), int.from_bytes(e[12:16], "little")
    raise SystemExit("no flash.bin in the package")


if __name__ == "__main__":
    sys.exit(main())
