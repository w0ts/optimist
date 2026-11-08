# SPDX-License-Identifier: GPL-3.0-only
"""tools/dn2_sync.py's plan, without a DN2: what is deleted, sent again, sent, and what is never touched.

    python3 tests/dn2_sync_test.py [OUTDIR]"""
import importlib
import shutil
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
S = importlib.import_module("dn2_sync")
fails = 0


def check(what, ok):
    global fails
    print(f"{what:100s} {'ok' if ok else 'FAIL'}")
    fails += not ok


def wav(p, frames):
    p.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(p), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(44100)
        w.writeframes(b"\0\1" * frames)


def main(out):
    out = Path(out).resolve()
    shutil.rmtree(out, ignore_errors=True)
    src = out / "src"
    wav(src / "Kick/BD001 909 kick 01.wav", 100)
    wav(src / "Kick/BD002 909 kick 02.wav", 200)
    wav(src / "HH/HH001 808 ch.wav", 50)
    wav(src / "HH/HH002 808 ch 2.wav", 60)
    local = S.local_samples(src)
    check("SRC read: folder and name upper case, cut to 24, with frames",
          local[("KICK", "BD001 909 KICK 01")][1] == 100 and len(local) == 4)
    device = [(0, "KICK", "BD001 909 KICK 01", 100),       # unchanged
              (1, "KICK", "BD002 909 KICK 02", 999),       # length changed: deleted, sent again
              (2, "KICK", "BD009 OLD", 10),                # gone from SRC: deleted
              (3, "KICK", "BD001 909 KICK 01", 100),       # a second copy: deleted
              (4, "RECORDED", "REC 1", 500),               # not SRC's folder: never touched
              (5, "INCOMING", "BD009 OLD", 10),            # the same name elsewhere: never touched
              (6, "HH", "HH001 808 CH", 50)]               # unchanged, but edited since sent (manifest)
    sent = {"HH/HH001 808 CH": "an older hash"}
    delete, send, same = S.make_plan(local, device, sent)
    check("deleted: the changed length, the one gone, the second copy, the edited one",
          sorted(i for i, _, _ in delete) == [1, 2, 3, 6])
    check("never touched: RECORDED, INCOMING", not any(i in (4, 5) for i, _, _ in delete))
    check("sent: the changed, the edited and the new ones, in their folders",
          sorted((f, p.name) for f, p in send) == [("HH", "HH001 808 ch.wav"), ("HH", "HH002 808 ch 2.wav"),
                                                   ("KICK", "BD002 909 kick 02.wav")])
    check("unchanged counted", same == 1)
    delete, _, _ = S.make_plan(local, device, sent, replace=True)
    check("--replace: other folders' samples deleted too, never INCOMING or RECORDED",
          sorted(i for i, _, _ in delete) == [1, 2, 3, 6] and not any(i in (4, 5) for i, _, _ in delete))
    delete, _, _ = S.make_plan(local, device + [(7, "KICKS", "TR-909 KICK 01", 300)], sent, replace=True)
    check("--replace: a TR pack's folder (KICKS) emptied", 7 in [i for i, _, _ in delete])
    wav(src / "Kick/BD001 909 kick 01 longer name.wav", 10)
    wav(src / "Kick/BD001 909 kick 01 longer other.wav", 10)
    try:
        S.local_samples(src)
        ok = False
    except SystemExit as e:
        ok = "first 24 characters" in str(e)
    check("two names equal in their first 24 characters refused", ok)
    shutil.rmtree(out, ignore_errors=True)
    print("dn2_sync_test:", "FAILED" if fails else "ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else str(ROOT / "build" / "dn2-sync-test")))
