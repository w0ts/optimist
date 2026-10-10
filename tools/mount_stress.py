#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Stress the container's view of a file the host has just rewritten (BUILDING.md, "How the toolchain runs").

Each round rewrites build/mount-stress/gen.h on the host (one byte longer or shorter than the last), then reads it
in a container at once, and counts the rounds where the container saw a stale size or content.

  python3 tools/mount_stress.py <tree> [--mode rename|inplace|same-rename|same-inplace] [--n N] [--delay S]
                                [--runner exec|run] [--sync none|fsync] [--probe stat|cc] [--fix]

--probe cc compiles the header with the toolchain image; --fix runs toolchain.sync_view() after each write.
"""
import argparse
import hashlib
import os
import subprocess
import sys
import time

IMAGE = "optimist-toolchain:20250324.1"


def content(i, mode):
    same = mode.startswith("same")
    n = 8304 if (same or i % 2) else 8305          # alternate shrink/grow by one byte (as seen)
    head = f"/* iteration {i:08d} */\nstatic const char d[] = {{\n".encode()
    tail = b"};\nint last(void) { return d[0]; }\n"
    fill = n - len(head) - len(tail)
    body = (b"1,2,3,4,5,6,7,\n" * 1000)[:fill - 1] + b"\n"
    body = body[:-2] + b" \n" if body[-2:-1] == b"," else body
    return head + body + tail


def write(path, data, mode, sync):
    if mode.endswith("rename"):
        tmp = path + ".tmp"
        with open(tmp, "wb") as f:
            f.write(data)
            if sync == "fsync":
                f.flush(); os.fsync(f.fileno())
        os.replace(tmp, path)
    else:
        with open(path, "r+b" if os.path.exists(path) else "wb") as f:
            f.truncate(0)
            f.write(data)
            if sync == "fsync":
                f.flush(); os.fsync(f.fileno())
    if sync == "fsync":
        fd = os.open(os.path.dirname(path), os.O_RDONLY)
        os.fsync(fd); os.close(fd)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tree")
    ap.add_argument("--mode", default="rename")
    ap.add_argument("--n", type=int, default=100)
    ap.add_argument("--delay", type=float, default=0.0)
    ap.add_argument("--runner", default="exec")
    ap.add_argument("--sync", default="none")
    ap.add_argument("--name", default="optimist-mount-stress")
    ap.add_argument("--probe", default="stat", help="stat (size + sha256) or cc (compile it with the toolchain)")
    ap.add_argument("--fix", action="store_true", help="toolchain.sync_view() after each write")
    ap.add_argument("--pre", default="", help="a shell command run in the container before the probe ({f} = file)")
    a = ap.parse_args()
    d = os.path.join(a.tree, "build", "mount-stress")
    os.makedirs(d, exist_ok=True)
    path = os.path.join(d, "gen.h")
    rel = "build/mount-stress/gen.h"
    probe = ("{ " + a.pre.format(f=rel, d=os.path.dirname(rel)) + "; } >/dev/null 2>&1; " if a.pre else "") + \
        f"stat -c %s {rel}; sha256sum < {rel} | cut -c1-64"
    if a.probe == "cc":
        probe = (f"/opt/jieli/pi32v2/bin/clang -target pi32v2 -fsyntax-only -Werror -x c -include {rel} /dev/null "
                 "2>&1 | head -3; echo rc=$?")
    sys.path.insert(0, os.path.join(a.tree, "tools"))
    import toolchain as TC
    backend = TC.Backend("image", image=IMAGE)
    fix_s = 0.0
    if a.runner == "exec":
        subprocess.run(["docker", "rm", "-f", a.name], capture_output=True)
        subprocess.run(["docker", "run", "-d", "--name", a.name, "--platform", "linux/amd64", "-v", f"{a.tree}:/work",
                        "-w", "/work", IMAGE, "sleep", "100000"], check=True, capture_output=True)
    bad = {"size": 0, "content": 0, "both": 0}
    t0 = time.time()
    try:
        for i in range(a.n):
            data = content(i, a.mode)
            write(path, data, a.mode, a.sync)
            if a.fix:
                t1 = time.time()
                TC.sync_view(backend, [path], a.tree)
                fix_s += time.time() - t1
            if a.delay:
                time.sleep(a.delay)
            if a.runner == "exec":
                cmd = ["docker", "exec", "-w", "/work", a.name, "sh", "-c", probe]
            else:
                cmd = ["docker", "run", "--rm", "--name", f"{a.name}-{i}", "--platform", "linux/amd64",
                       "-v", f"{a.tree}:/work", "-w", "/work", IMAGE, "sh", "-c", probe]
            r = subprocess.run(cmd, capture_output=True, text=True); out = r.stdout.split()
            if a.probe == "cc":
                if r.stdout.strip() != "rc=0":
                    bad["content"] += 1
                    if os.environ.get("MOUNT_STRESS_DEBUG"):
                        print(i, len(data), r.stdout.strip()[:300], file=sys.stderr)
                continue
            size_ok = out[:1] == [str(len(data))]
            sha_ok = out[1:2] == [hashlib.sha256(data).hexdigest()]
            if os.environ.get("MOUNT_STRESS_DEBUG") and not (size_ok and sha_ok):
                print(i, len(data), out, r.stderr[-300:], cmd[-1], file=sys.stderr)
            if not size_ok and not sha_ok:
                bad["both"] += 1
            elif not size_ok:
                bad["size"] += 1
            elif not sha_ok:
                bad["content"] += 1
    finally:
        if a.runner == "exec":
            subprocess.run(["docker", "rm", "-f", a.name], capture_output=True)
    fails = sum(bad.values())
    print(f"probe={a.probe} fix={a.fix} pre={a.pre!r} mode={a.mode} sync={a.sync} delay={a.delay} runner={a.runner} n={a.n} fails={fails} {bad} "
          f"({time.time() - t0:.0f} s, sync_view {fix_s / max(a.n, 1) * 1000:.0f} ms/run)")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
