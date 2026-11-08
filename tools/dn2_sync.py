#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Sync a folder of samples with a Digitone II's +Drive library (dn2mk2 v0.31 and later), from a Mac, over USB MIDI.

The user, 2026-11-08: "for dn2 it should check what is already on the device, and sync (delete/update/remove)". It
speaks dn2mk2's sample link through that repo's own tools/dn2samples.py (Link, lib_list, lib_send: the same requests
the dn2mk2 web page sends), its Windows-only MIDI swapped for mido (the Optimist venv's: tools/builder/venv).

    make dn2-samples [LIB=<sorted library>] [SRC=<DN2 folder>] [DRY=1] [YES=1] [REPLACE=1]
    tools/builder/venv/bin/python tools/dn2_sync.py [SRC] [--lib LIB] [--dry-run] [--yes] [--replace] [--port ..]

One command (the user, 2026-11-08: "can you make one command for dn2"): with a library (--lib, LIB=), SRC is made from it
first (tools/sd_sort_samples.py dn2: the 1,900 picked, shortened, named "BD909 01"; a mirror: a changed or renumbered
sample written again, one no longer picked removed), then synced. LIB and SRC are remembered (build/dn2-samples.json):
a later `make dn2-samples` does both again with nothing to type.

SRC holds one folder per type (tools/sd_sort_samples.py dn2 writes it: Kick/BD001 909 kick 01.wav ..). The device's
library is read first (folders, names, lengths), then for each of SRC's folders only (its name upper case, as the
DN2 keeps it):
  - a sample on the device that SRC no longer has: deleted;
  - a sample whose length changed, or whose file changed since this script sent it (SRC/.dn2-sync.json): deleted and
    sent again;
  - a sample SRC has and the device does not: sent.
A sample is known by its folder, its name's first 24 characters (what the library's list returns) and its length in
frames. Folders SRC does not have (INCOMING, RECORDED, your own) are never touched, unless --replace: then every other
folder's samples are deleted too and those folders dropped once empty (the user, 2026-11-08, for the 919 TR-pack samples
an earlier import left: "Replace them"); INCOMING and RECORDED stay whatever is asked. Deleting a library sample does not
touch a project that already loaded it (LOAD TO PROJECT copies it into the project's pool). The library holds 2,048
samples: a sync that would pass it is refused before anything changes.
"""
import argparse
import hashlib
import json
import os
import struct
import sys
import time
import wave
from pathlib import Path

LIB_CAP = 2048                                      # (dn2mk2 csrc/sampler/lib.c LIB_CAP)
REMEMBER = Path(__file__).resolve().parent.parent / "build" / "dn2-samples.json"   # (SRC and LIB last used)
SEND_BPS = 155e3                                    # (measured on the user's DN2, 2026-11-08: 152..173 KB/s a sample)
KEEP = ("INCOMING", "RECORDED")                     # (folders 0 and 1: never emptied, never dropped)
L_TREE = 31                                         # (lib.c: TREE -> LIB_DEPTH, then each folder's parent)
L_FOLDER_DROP = 26                                  # (lib.c smp_lib_rx: DELETE FOLDER, u8 folder, only when empty)
LIST_NAME = 24                                      # (lib_list: the name's bytes in a list reply)
MANIFEST = ".dn2-sync.json"                         # (in SRC: each sample sent, its file's hash: an edit that keeps the
                                                    # length is seen too, LIST has no hash; hidden: never a sample)


class MidoPort:
    """dn2samples.Link's port (winmidi.Port's two calls) over mido: send one SysEx, collect SysEx until a test is met"""

    def __init__(self, want):
        import mido                                 # (here: the venv's, optional; a clear message without it below)
        self.mido = mido
        outs = [n for n in mido.get_output_names() if want.lower() in n.lower()]
        ins = [n for n in mido.get_input_names() if want.lower() in n.lower()]
        if not outs or not ins:
            raise SystemExit(f"no MIDI port with {want!r} in its name: is the Digitone II plugged in, USB CONFIG "
                             f"USB MIDI or Overbridge? (ports: {', '.join(mido.get_output_names()) or 'none'})")
        self.out = mido.open_output(outs[0])
        self.inp = mido.open_input(ins[0])

    def send(self, data):
        self.out.send(self.mido.Message("sysex", data=bytes(data)[1:-1]))

    def receive(self, seconds=1.0, until=None):
        msgs, end = [], time.time() + seconds
        while time.time() < end:
            for m in self.inp.iter_pending():
                if m.type == "sysex":
                    msgs.append(bytes([0xF0]) + bytes(m.data) + bytes([0xF7]))
            if until and until(msgs):
                break
            time.sleep(0.001)
        return msgs

    def close(self):
        self.inp.close()
        self.out.close()


def local_samples(src):
    """{(FOLDER, NAME24): (path, frames)} of SRC's type folders"""
    out = {}
    for d in sorted(p for p in Path(src).iterdir() if p.is_dir() and not p.name.startswith(".")):
        for f in sorted(x for x in d.iterdir() if x.suffix.lower() == ".wav" and not x.name.startswith(".")):
            with wave.open(str(f)) as w:
                frames = w.getnframes()
            key = (d.name.upper()[:31], f.stem.upper()[:LIST_NAME])
            if key in out:
                raise SystemExit(f"{f}: its first {LIST_NAME} characters are another sample's in {d.name}: rename one")
            out[key] = (f, frames)
    return out


def file_hash(p):
    return hashlib.sha1(Path(p).read_bytes()).hexdigest()


def mkey(key):
    return key[0] + "/" + key[1]


def make_plan(local, device, sent=None, replace=False):
    """-> (entries to delete, [(folder, path)] to send, unchanged count): device is lib_list's (idx, folder, name, frames);
    sent: the manifest ({"FOLDER/NAME24": the file's hash when it was sent})"""
    sent = sent or {}
    folders = {k[0] for k in local}
    have, delete = {}, []
    for idx, folder, name, frames in device:
        key = (folder.upper(), name.upper()[:LIST_NAME])
        if folder.upper() not in folders:
            if replace and folder.upper() not in KEEP:
                delete.append((idx, folder, name))  # (--replace: another folder's sample)
            continue                                # (not SRC's folder: never touched)
        edited = key in local and mkey(key) in sent and sent[mkey(key)] != file_hash(local[key][0])
        if key not in local or local[key][1] != frames or key in have or edited:
            delete.append((idx, folder, name))      # (gone, changed, or a second copy)
        else:
            have[key] = idx
    send = [(k[0], p) for k, (p, _) in sorted(local.items()) if k not in have]
    return delete, send, len(have)


def drop_folders(D, link, keep):
    """--replace, the deletes done: every folder but INCOMING, RECORDED and keep (SRC's, upper case) dropped. DELETE
    FOLDER refuses one still holding a sample or a folder, so the pass runs again while it drops some (inner first)"""
    names = {}
    try:                                            # (TREE, v0.40: each folder's parent, 0xFF the top; only a TOP-LEVEL
        par = link.call(L_TREE)[1:]                 # folder of SRC's name is SRC's: a pack's own KICK inside it is not,
    except (ValueError, TimeoutError):              # the DN2 session found 2026-11-08; L_FOLDER finds "KICK" from the top)
        par = b""
    for f0 in range(0, 128, 4):                     # (128 folders from v0.43; an older build refuses past its 64)
        try:
            p = link.call(D.L_FOLDERS, bytes([f0]))
        except (ValueError, TimeoutError):
            break
        for j in range(4):
            nm = p[24 * j:24 * j + 24].split(b"\0")[0].decode("ascii", "replace")
            top = f0 + j >= len(par) or par[f0 + j] == 0xFF
            if nm and f0 + j >= 2 and not (top and nm.upper() in keep) and nm.upper() not in KEEP:
                names[f0 + j] = nm
    for _ in range(7):                              # (6 levels deep at most: lib.c LIB_DEPTH)
        dropped = []
        for f, nm in sorted(names.items()):
            try:
                link.call(L_FOLDER_DROP, bytes([f]))
                dropped.append(f)
                print(f"  folder {nm} dropped")
            except ValueError:
                pass
        for f in dropped:
            del names[f]
        if not dropped:
            break
    for nm in sorted(names.values()):
        print(f"  folder {nm} kept (not empty)")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src", nargs="?", help="the folder of type folders (e.g. ~/Documents/Musique/Samples DN2); "
                                           "remembered")
    ap.add_argument("--lib", help="the sorted library (tools/sd_sort_samples.py sort): SRC made from it first; remembered")
    ap.add_argument("--no-rebuild", action="store_true", help="sync SRC as it is (the remembered library not used)")
    ap.add_argument("--dry-run", action="store_true", help="read the device and show the plan, change nothing")
    ap.add_argument("--yes", action="store_true", help="no question")
    ap.add_argument("--replace", action="store_true", help="also delete every other folder's samples (never INCOMING, "
                                                          "RECORDED) and drop those folders")
    ap.add_argument("--port", default="Digitone II", help="a part of the DN2's MIDI port name")
    ap.add_argument("--chunk", type=int, default=512, help="bytes a message (16..512)")
    ap.add_argument("--dn2-repo", default=os.path.expanduser("~/GitHub/dn2mk2-firmware"),
                    help="the dn2mk2 checkout (its tools/dn2samples.py speaks the link)")
    a = ap.parse_args(argv)
    mem = json.loads(REMEMBER.read_text()) if REMEMBER.is_file() else {}
    a.src = a.src or mem.get("src")
    lib = a.lib or (None if a.no_rebuild else mem.get("lib"))
    if not a.src:
        raise SystemExit("which folder? make dn2-samples SRC=<folder> [LIB=<sorted library>] (remembered after)")
    if lib:
        import sd_sort_samples as SS                # (tools/: beside this file; only when a library is given)
        print(f"making {a.src} from {lib} ...")
        ns = argparse.Namespace(lib=os.path.expanduser(lib), out=os.path.expanduser(a.src), dry_run=False,
                                jobs=os.cpu_count() or 4)
        if SS.cmd_dn2(ns):
            raise SystemExit("the DN2 folder could not be made: nothing sent")
    REMEMBER.parent.mkdir(parents=True, exist_ok=True)
    REMEMBER.write_text(json.dumps({"src": os.path.abspath(os.path.expanduser(a.src)),
                                    "lib": os.path.abspath(os.path.expanduser(lib)) if lib else mem.get("lib")}))
    tools = Path(a.dn2_repo) / "tools"
    if not (tools / "dn2samples.py").is_file():
        raise SystemExit(f"no dn2samples.py in {tools}: --dn2-repo names the dn2mk2 checkout")
    sys.path.insert(0, str(tools))
    import dn2samples as D                          # (the dn2mk2 checkout's, on the path above)
    src = Path(os.path.expanduser(a.src))
    local = local_samples(src)
    man = src / MANIFEST
    sent = json.loads(man.read_text()) if man.is_file() else {}
    try:
        port = MidoPort(a.port)
    except ImportError:
        raise SystemExit("needs mido and python-rtmidi: run it with tools/builder/venv/bin/python (make setup)")
    try:
        link = D.Link(port)
        hello = link.call(D.HELLO)
        link.proto = struct.unpack(">H", hello[:2])[0]
        if link.proto < 5:
            raise SystemExit(f"sample link {link.proto}: the +Drive library needs dn2mk2 v0.31 or later")
        print("reading the DN2's library ...")
        device = D.lib_list(link)
        delete, send, same = make_plan(local, device, sent, a.replace)
        others = sorted({f for _, f, _ in delete if f.upper() not in {k[0] for k in local}})
        if others:
            print(f"--replace: {sum(1 for _, f, _ in delete if f in others)} samples in {len(others)} other folders "
                  f"deleted: {', '.join(others)}")
        after = len(device) - len(delete) + len(send)
        mb = sum(p.stat().st_size for _, p in send) / 1e6
        print(f"DN2: {len(device)} samples; {a.src}: {len(local)} in {len({k[0] for k in local})} folders")
        print(f"plan: {len(delete)} to delete, {len(send)} to send ({mb:,.0f} MB, about "
              f"{mb * 1e6 / SEND_BPS / 60:,.0f} min), {same} unchanged; {after} in the library after")
        if after > LIB_CAP:
            raise SystemExit(f"{after} samples: the library holds {LIB_CAP}: nothing changed")
        if a.dry_run:
            for idx, folder, name in delete:
                print(f"  delete  {folder}/{name} (entry {idx})")
            for folder, p in send:
                print(f"  send    {folder}/{p.stem}")
            print("dry run: nothing changed")
            return 0
        if not (delete or send):
            print("in sync: nothing to do")
            return 0
        if not a.yes and input("Go? [y/N] ").strip().lower() not in ("y", "yes"):
            print("cancelled: nothing changed")
            return 1
        for idx, folder, name in delete:
            link.call(D.L_DELETE, struct.pack("<H", idx))
            sent.pop(mkey((folder.upper(), name.upper()[:LIST_NAME])), None)
            print(f"  deleted {folder}/{name}")
        man.write_text(json.dumps(sent, indent=0, sort_keys=True))
        if a.replace:
            drop_folders(D, link, {k[0] for k in local})
        for folder, p in send:                      # (one at a time: the manifest written after each one sent whole)
            D.lib_send(link, [str(p)], folder, a.chunk)
            sent[mkey((folder, p.stem.upper()[:LIST_NAME]))] = file_hash(p)
            man.write_text(json.dumps(sent, indent=0, sort_keys=True))
        print("done: the samples are in the DN2's +DRIVE (LOAD TO PROJECT puts one in a project)")
        return 0
    finally:
        port.close()


if __name__ == "__main__":
    sys.exit(main())
