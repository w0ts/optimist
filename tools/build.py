#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Build Felucca: the app, the update loader and an installable .fwsc package.

  tools/build.py [--release X.Y[-suffix]]

Outputs in build/: felucca.bin (app), loader/ota.bin (update loader),
felucca.fwsc (package). See BUILDING.md for the toolchain and the SDK.

The JieLi toolchain is Linux x86-64 only: natively on Linux x86-64, elsewhere each
tool runs in a linux/amd64 container (or WSL); tools/toolchain.py finds it.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import zipfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
FW = SRC / "firmware"
OUT = SRC / "build"
GEN = OUT / "gen"
# the host tests' headers (tests/run_tests.sh): every sample set, whatever profile build/gen was made for, so one
# tests/golden.txt covers every preset and no profile hides a render (--host-headers)
HOST_GEN = OUT / "gen-host"
HOST_ENV = {"FELUCCA_SAMPLES_SKIP": "", "FELUCCA_SLICE": "0"}
LDR = OUT / "loader"
sys.path.insert(0, str(SRC / "tools"))
import fm1pkg_make  # noqa: E402
import lz4blk  # noqa: E402
import toolchain as TC
sys.path.insert(0, str(SRC / "tools" / "builder"))
import configure  # noqa: E402  (the firmware builder: .config -> build/gen/felucca_config.h)

APP_XIP = 0x02000120                # app.bin offset 0 in the XIP map; the SPL jumps here
APP_SLOT = fm1pkg_make.APP_SLOT
LOADER_LOAD = 0x01C0A800
LOADER_NAME = b"usb_hid_ota.bin"    # the file name the SPL looks for
CFLAGS = ["-Os", "-ffunction-sections", "-fno-builtin", "-Wall", "-Wno-unused-function"]
LINE = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*\t(.*)$")

SDK_SHA256 = TC.SDK_SHA256          # SDK files of AC79NN_SDK_V1.2.1_2023-12-13 (the tested version)
BACKEND = None                      # how the toolchain runs (tools/toolchain.py; main() resolves it)

# package identity (BUILDING.md "Package identity"): Optimist is FM-1_7XY. FM-1_ + three digits is the
# form the stock updater and every installer accept (M-VAVE 0XX, Baud Girl 020-09X, Lunar 5XX, Felucca /
# SLOOP / X0X 9XX); release builds are FM-1_7XY, development builds FM-1_700
PRODUCT = "FM-1_700"
VERSION = None                      # FELUCCA_VERSION for release builds (default: firmware/src/ui/sloop/ui.c)
OPTIMIST_VERSION = (Path(__file__).resolve().parent.parent / "VERSION").read_text().strip()   # the one version number
MEASURE = False                     # --measure: link past the slot and the pool (sizes only, never a package)
CFG_FLAGS = set()                   # the switches build/gen/felucca_config.h sets (the env loop below skips them)
CFG_VALUES = {}                     # ... and their values
RESERVE = {}                        # the configuration (RESERVE_FLASH_KB, RESERVE_UNDO_KB: configure.reserve_*)
# the budgets (bytes): the app slot, main RAM .data+.bss, the pool (and the spare build.py keeps), RAM code, noinit
LIMITS = {"flash": 0x8DFBC, "ram": 96 * 1024, "pool": 0x54000, "pool_spare": 8192, "ramtext": 0x7F00,
          "noinit": 0x3D50}


def backend():
    global BACKEND
    if BACKEND is None:
        try:
            BACKEND = TC.resolve()
        except TC.ToolchainError as e:
            raise SystemExit(f"build: {e}")
    return BACKEND


def tc(tool, *args):
    """run a toolchain binary (pi32v2/bin/..., common/bin/...) with cwd SRC; paths relative to SRC, written with
    / on every host (the tool runs on Linux)"""
    rel = [Path(a).resolve().relative_to(SRC).as_posix() if isinstance(a, Path) else a for a in args]
    if tool == "cc":                # the toolchain's cc wrapper needs python3; call clang directly
        tool, rel = "pi32v2/bin/clang", ["-target", "pi32v2", *rel]
    # (core=0 in every backend: a toolchain crash under emulation, which tools/builder/configure.py retries,
    # must not leave a core file in the source tree)
    cmd = backend().command(tool, rel, SRC)
    for attempt in range(TC.TOOL_TRIES):
        r = subprocess.run(cmd, cwd=SRC, capture_output=True, text=True, encoding="utf-8", errors="replace")
        if not TC.retry_tool(backend(), r.returncode, r.stdout + r.stderr, attempt):
            break
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"build: {tool} failed")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    return r.stdout


def tc_all(*cmds):
    with ThreadPoolExecutor(len(cmds)) as ex:
        return list(ex.map(lambda c: tc(*c), cmds))


def generate(gen=GEN, env=None):
    """generated headers (fonts, icons, tables, samples) into gen; env: the generators' environment over ours
    (the build: setup_config() has set it; the host tests: HOST_ENV)"""
    gen.mkdir(parents=True, exist_ok=True)
    tools = SRC / "tools"
    cmds = [[tools / "gen_font.py", gen / "felucca_font.h"],
            [tools / "gen_icons.py", gen / "felucca_icons.h"],
            [tools / "gen_colors.py", gen / "felucca_colors.h"],
            [tools / "gen_tables.py", gen / "felucca_tables.h"],
            [tools / "gen_samples.py", gen / "felucca_samples.h"],
            [tools / "gen_drumkits.py", gen / "felucca_drumkits.h"],
            [tools / "gen_x0x_drums.py", gen / "x0x_drum_samples.h"],
            [tools / "gen_param_help.py", gen / "felucca_param_help.h"]]   # (+ web/editor.html's copy)
    penv = {**os.environ, **(env or {})}
    procs = [subprocess.Popen([sys.executable, *map(str, c)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, env=penv) for c in cmds]
    failed = []
    for c, p in zip(cmds, procs):
        sys.stdout.write(p.communicate()[0])
        if p.returncode:
            failed.append(c[0].name)
    if failed:
        raise SystemExit(f"build: {', '.join(failed)} failed")


# ---- update loader

def crc16(d, c=0):
    for b in d:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else c << 1
        c &= 0xFFFF
    return c


def ldr_head(dcrc, a, b, attr, name):
    body = struct.pack("<HIIBBH16s", dcrc, a, b, attr, 0, 0, name)
    return struct.pack("<H", crc16(body)) + body


def ldr_wrap(image):
    """ota.bin = outer JLFS head (32 B) + inner head (32 B, load address) +
    blocks [clen u32][dlen u32][LZ4 block], dlen 4096 except the last"""
    blocks = bytearray()
    for i in range(0, len(image), 4096):
        raw = image[i:i + 4096]
        c = lz4blk.compress(raw)
        if len(c) > 4096 or lz4blk.decompress(c) != raw:
            raise SystemExit(f"loader: block {i // 4096} does not compress below 4096 B")
        blocks += struct.pack("<II", len(c), len(raw)) + c
    inner = ldr_head(crc16(image), len(image), LOADER_LOAD, 0, LOADER_NAME)
    body = inner + bytes(blocks)
    return ldr_head(crc16(body), 0x20, len(body), 0x41, LOADER_NAME) + body


def build_loader():
    LDR.mkdir(parents=True, exist_ok=True)
    src = FW / "loader"
    flags = [*CFLAGS, "-Ifirmware/hal", "-Ifirmware/src"]
    tc_all(("cc", "-c", src / "crt0_ldr.S", "-o", LDR / "crt0_ldr.o"),
           ("cc", *flags, "-c", src / "loader.c", "-o", LDR / "loader.o"))
    elf = LDR / "loader.elf"
    tc("pi32v2/bin/ld", "--gc-sections", "-e", "_start", "-T", src / "loader.ld",
       LDR / "crt0_ldr.o", LDR / "loader.o", "-o", elf)
    _, dis, hdr, syms = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, LDR / "loader.bin"),
                               ("common/bin/objdump", "-d", elf),
                               ("common/bin/objdump", "-h", elf),
                               ("common/bin/objdump", "-t", elf))
    (LDR / "loader.dis").write_text(dis)
    for ln in hdr.splitlines():     # the loader rewrites the flash: nothing may run from XIP
        p = ln.split()
        if len(p) > 4 and p[1].startswith(".") and int(p[3], 16) >= 0x02000000 and int(p[2], 16):
            raise SystemExit(f"loader: section {p[1]} at {p[3]} is outside RAM")
    image = (LDR / "loader.bin").read_bytes()
    if not image or len(image) > 0x14000:
        raise SystemExit("loader: image empty or too big")
    ota = ldr_wrap(image)
    (LDR / "ota.bin").write_bytes(ota)
    bss = [int(ln.split()[0], 16) for ln in syms.splitlines() if ln.rstrip().endswith("_bss_end")]
    print(f"loader: image {len(image)} B at {LOADER_LOAD:#x}" + (f", bss end {bss[0]:#x}" if bss else ""))
    print(f"loader: ota.bin {len(ota)} B")
    return ota


# ---- app

# the ACID engine's float unit (FELUCCA_ENG_ACID): X0X's flags for the AC79's single-precision FPU, no fused
# multiply-add (charlesvestal/fm1-x0x tools/build.py FPU), -O2
ACID_CFLAGS = ["-O2", "-ffunction-sections", "-fno-builtin", "-Wall", "-Wno-unused-function", "-mcpu=r3", "-mfprev1",
               "-ffp-contract=off"]
# the backported features' switches (firmware/src/core/backports.h; provenance and costs: tools/backports.json)
BACKPORT_FLAGS = ("FELUCCA_CHANCE", "FELUCCA_KEYLIT", "FELUCCA_QNT_SEQ", "FELUCCA_SPRING", "FELUCCA_BASSPLUS",
                  "FELUCCA_BRIGHT", "FELUCCA_DLY_HALVE", "FELUCCA_MOTION", "FELUCCA_ENG_PHYS", "FELUCCA_ENG_ACID", "FELUCCA_ENG_CZ",
                  "FELUCCA_DRUM_X909", "FELUCCA_DRUM_X808", "FELUCCA_X909_CYM")

def build_app():
    flags = [*CFLAGS, "-Ifirmware/hal", "-Ifirmware/src", "-Ibuild/gen"]
    for flag in ("FELUCCA_FLASH", "FELUCCA_OTA", "FELUCCA_OTA_DRYRUN", "FELUCCA_CDC", "FELUCCA_UART",
                 "FELUCCA_ICONS", "FELUCCA_SLICE", "FELUCCA_FM6_KEYS", "FELUCCA_ANALOG2",
                 "FELUCCA_ASM", "FELUCCA_ASM_CHECK", "FELUCCA_IDLE", "FELUCCA_SPLASH",
                 "FELUCCA_USB_AUDIO", "FELUCCA_SIMD", "FELUCCA_SIMD_CHECK", "FELUCCA_SIMD_PROBE",
                 "FELUCCA_SIMD_PROBE_TEST", "FELUCCA_DRUM_EDIT", "FELUCCA_DRUM_USR", "FELUCCA_DRUM_KITS",
                 "FELUCCA_KNOB_ACCEL", "FELUCCA_LCD_DIRTY", "FELUCCA_UA_RESAMPLE", "FELUCCA_UNDO_HISTORY",
                 "FELUCCA_REV_PROFILE", "FELUCCA_BLE",
                 *BACKPORT_FLAGS):
        v = os.environ.get(flag)    # unset: the default in firmware/src/felucca.c
        if v in ("0", "1") and flag not in CFG_FLAGS:
            flags.append(f"-D{flag}={v}")
    flags += ["-include", "build/gen/felucca_config.h"]   # the builder's configuration (tools/builder)
    v = os.environ.get("FELUCCA_LCD_BAUD")    # LCD SPI clock = 60 MHz / (v + 1); default 1 (lcd.c)
    if v is not None and len(v) == 1 and v in "01234":
        flags.append(f"-DLCD_BAUD={v}u")
    v = os.environ.get("FELUCCA_UNDO_CAP")    # the undo history's ring at most this many bytes (undo.c)
    if v and v.isdigit():
        flags.append(f"-DFELUCCA_UNDO_CAP={v}u")
    v = os.environ.get("FELUCCA_DLY_LEN")     # the delay line in samples (a power of two; fx.c checks)
    if v and v.isdigit() and "FELUCCA_DLY_LEN" not in CFG_FLAGS:
        flags.append(f"-DFELUCCA_DLY_LEN={v}u")
    for flag in ("FELUCCA_BENCH_KIT", "FELUCCA_BENCH_NOTE"):   # (benches: the kit UID; scenarios 8, 9: one GM note)
        v = os.environ.get(flag)
        if v and v.isdigit() and int(v) < 128 and os.environ.get("FELUCCA_BENCH", "0") != "0":
            flags.append(f"-D{flag}={v}")
    if os.environ.get("FELUCCA_BENCH") in ("10", "11", "12", "13"):   # (bench.c scenarios 10..13: two digits, the loop below takes one)
        flags.append(f"-DFELUCCA_BENCH={os.environ['FELUCCA_BENCH']}")
    for flag, ok in (("FELUCCA_DUAL", "012"), ("FELUCCA_BENCH", "0123456789"), ("FELUCCA_BENCH_SAVE", "01"),
                     ("FELUCCA_BENCH_MIX", "01"), ("FELUCCA_BENCH_COMP", "01"),
                     ("FELUCCA_DUAL_IDLE", "01"), ("DUAL_PARTS", "01234567"), ("DUAL_FAILTEST", "0123")):
        v = os.environ.get(flag)    # EXPERIMENTAL second core / emulator scenarios (docs/DUAL-CORE.md)
        if v is not None and len(v) == 1 and v in ok and flag not in CFG_FLAGS:
            flags.append(f"-D{flag}={v}")
    flags.append(f'-DFELUCCA_ID="{PRODUCT}"')
    if VERSION:
        flags.append(f'-DFELUCCA_VERSION="{VERSION}"')
    # Felucca 1.0.1 (20c275e): felucca.c goes to LLVM IR without the optimizer, the main-loop functions
    # (UI, stores, editor) are marked minsize (tools/size_fns.py), then the IR is compiled at -Os.
    # FELUCCA_SIZE=0: -Os everywhere; FELUCCA_SIZE=ir: the IR round trip without marks (a check)
    size = os.environ.get("FELUCCA_SIZE", "1")
    cmain = (("cc", *flags, "-c", FW / "src" / "felucca.c", "-o", OUT / "felucca.o") if size == "0" else
             ("cc", *flags, "-S", "-emit-llvm", "-Xclang", "-disable-llvm-optzns", "-c",
              FW / "src" / "felucca.c", "-o", OUT / "felucca.ll"))
    units = [("cc", "-c", FW / "crt0.S", "-o", OUT / "crt0.o"),
             ("cc", "-c", FW / "hal" / "fm1_vec.S", "-o", OUT / "fm1_vec.o"),
             ("cc", "-c", FW / "hal" / "fm1_isr.S", "-o", OUT / "fm1_isr.o"), cmain]
    objs = [OUT / "crt0.o", OUT / "fm1_vec.o", OUT / "fm1_isr.o", OUT / "felucca.o"]
    (OUT / "acid.o").unlink(missing_ok=True)
    if CFG_VALUES.get("FELUCCA_ENG_ACID", 0) == 1 or os.environ.get("FELUCCA_ENG_ACID") == "1":
        # the ACID engine's float DSP (firmware/src/engines/acid/, from X0X): its own unit, with X0X's FPU flags (the rest
        # of the firmware stays integer-only), -O2 as X0X builds it
        units.append(("cc", *ACID_CFLAGS, *(["-DFELUCCA_CPU_GUARD=1"] if CFG_VALUES.get("FELUCCA_CPU_GUARD") == 1 else []),
                      "-c", FW / "src" / "engines" / "acid" / "acid_dsp.c", "-o", OUT / "acid.o"))
        objs.append(OUT / "acid.o")
    (OUT / "x0x.o").unlink(missing_ok=True)
    x0x = {f: CFG_VALUES.get(f, int(os.environ.get(f, d))) for f, d in
           (("FELUCCA_DRUM_X909", "0"), ("FELUCCA_DRUM_X808", "0"), ("FELUCCA_X909_CYM", "1"))}
    if x0x["FELUCCA_DRUM_X909"] == 1 or x0x["FELUCCA_DRUM_X808"] == 1:
        # the X0X drum kits' float models (firmware/src/drums/x0x/, from X0X): their own unit with X0X's FPU flags, as
        # ACID's; the data headers from tools/gen_x0x_drums.py
        units.append(("cc", *ACID_CFLAGS, "-Ibuild/gen", *(f"-D{k}={v}" for k, v in x0x.items()),
                      *(["-DFELUCCA_CPU_GUARD=1"] if CFG_VALUES.get("FELUCCA_CPU_GUARD") == 1 else []), "-c",
                      FW / "src" / "drums" / "x0x" / "x0x_drums.c", "-o", OUT / "x0x.o"))
        objs.append(OUT / "x0x.o")
    tc_all(*units)
    if size != "0":
        subprocess.run([sys.executable, SRC / "tools" / "size_fns.py", *(["--none"] if size == "ir" else []),
                        OUT / "felucca.ll", OUT / "felucca_size.ll"], check=True)
        ir = [f for f in flags if not f.startswith(("-I", "-D", "-W"))]
        if "-include" in ir:                # (the IR is preprocessed already: no header to include)
            del ir[ir.index("-include"):ir.index("-include") + 2]
        tc("cc", *ir, "-c",
           OUT / "felucca_size.ll", "-o", OUT / "felucca.o")
    elf = OUT / "felucca.elf"
    ld = FW / "app.ld"
    if MEASURE:                     # a measurement link: XIP and POOL larger than the chip has (not flashable)
        ld = OUT / "app_measure.ld"
        ld.write_text((FW / "app.ld").read_text().replace("LENGTH = 0x8DFBC", "LENGTH = 0xEDFBC")
                      .replace("ORIGIN = 0x01C00000, LENGTH = 0x7F00", "ORIGIN = 0x01C00000, LENGTH = 0xFF00")
                      .replace("ORIGIN = 0x01C08000, LENGTH = 96K", "ORIGIN = 0x01C10000, LENGTH = 96K")
                      .replace("LENGTH = 96K", "LENGTH = 128K")             # (RAM, POOL and NOINIT moved up:
                      .replace("ORIGIN = 0x01C20000, LENGTH = 0x54000",    # addresses for sizes only)
                               "ORIGIN = 0x01C30000, LENGTH = 0x80000")
                      .replace("ORIGIN = 0x01C7C000", "ORIGIN = 0x01CB0000"))
    tc("pi32v2/bin/ld", "-T", ld, *objs, "-o", elf)
    for sect in ("text.bin", "data.bin", "ramtext.bin", "ramhot.bin", "ramhot2.bin"):
        (OUT / sect).unlink(missing_ok=True)
    *_, syms, dis, rt, hdr = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, OUT / "text.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".data", elf, OUT / "data.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".ram_text", elf, OUT / "ramtext.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".ram_hot", elf, OUT / "ramhot.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".ram_hot2", elf, OUT / "ramhot2.bin"),
                               ("common/bin/objdump", "-t", elf),
                               ("common/bin/objdump", "-d", elf),
                               ("common/bin/objdump", "-d", "-j", ".ram_text", elf),
                               ("common/bin/objdump", "-h", elf))
    (OUT / "felucca.dis").write_text(dis)

    def symv(name):
        return int(re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M).group(1), 16)
    img = bytearray((OUT / "text.bin").read_bytes())
    # .ram_text, .ram_hot, .ram_hot2 and .data follow .text at their load addresses; main.c copies them by words
    for sect, lname in (("ramtext.bin", "_rt_load"), ("ramhot.bin", "_rh_load"), ("ramhot2.bin", "_rh2_load"),
                        ("data.bin", "_data_load")):
        load = symv(lname)
        if load % 4:
            raise SystemExit(f"{lname} {load:#x} is not word aligned")
        blob = (OUT / sect).read_bytes() if (OUT / sect).exists() else b""
        if blob:
            if load - APP_XIP < len(img):
                raise SystemExit(f"{lname} overlaps the image")
            img += b"\xff" * (load - APP_XIP - len(img))
            img += blob
    img += b"\xff" * (-len(img) % 4)
    (OUT / "felucca.bin").write_bytes(img)
    return bytes(img), syms, dis, rt, hdr


def section_sizes(hdr):
    """objdump -h -> {section: size}"""
    out = {}
    for ln in hdr.splitlines():
        p = ln.split()
        if len(p) > 3 and p[0].isdigit() and p[1].startswith("."):
            out[p[1]] = int(p[2], 16)
    return out


def sizes(img, syms, hdr):
    """the five budgets of this build (build/sizes.json; tools/builder reads it)"""
    def sym(name):
        mm = re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M)
        return int(mm.group(1), 16) if mm else 0
    sec = section_sizes(hdr)
    return {"flash": len(img), "ram": sym("_bss_end") - (0x01C10000 if MEASURE else 0x01C08000), "pool": sym("_pool_end") - sym("_pool_start"),
            "ramtext": sym("_rt_end") - sym("_rt_start") + sym("_rh_end") - sym("_rh_start"),
            "noinit": sec.get(".noinit", 0), "limits": LIMITS}


OVER = []


IDLE_WAKE_SLOTS = 4     # a core woken from `idle` runs 4 issue slots before the interrupt enters (FM-1_996)


def idle_rewake(dis):
    """the `idle` instructions (addresses) from which an `idle` is reached again within IDLE_WAKE_SLOTS
    instructions: the core goes back to sleep before the interrupt that woke it enters, and never takes it
    (hal/fm1_dual.h fm1_dual_sleep_ram). Both arms of a conditional goto are followed; a call or a return
    ends a path (far longer than the window)."""
    ins = []
    for ln in dis.splitlines():
        mm = LINE.match(ln)
        if mm:
            ins.append((int(mm.group(1), 16), mm.group(3).strip()))
    at = {a: i for i, (a, _) in enumerate(ins)}

    def reaches(i, left):
        if left == 0 or i >= len(ins):
            return False
        t = ins[i][1]
        if t == "idle":
            return True
        if re.match(r"(rts|rti|rte)\b", t) or re.search(r"\bcall\b", t):
            return False
        if re.search(r"\bgoto\b", t):
            g = re.search(r"\bgoto\b.*: ([0-9a-f]+) >", t)
            j = at.get(int(g.group(1), 16)) if g else None
            if j is not None and reaches(j, left - 1):
                return True
            if not t.startswith("if"):
                return False
        return reaches(i + 1, left - 1)
    return [f"{a:#x}" for i, (a, t) in enumerate(ins) if t == "idle" and reaches(i + 1, IDLE_WAKE_SLOTS)]


def reserve_check(img_len, ring):
    """-> [message]: the build leaves less app flash free, or a smaller undo ring, than the configuration keeps
    (the builder's Reserve items, RESERVE); ring None: no undo history in this build, nothing to keep"""
    return configure.reserve_errors(RESERVE, APP_SLOT - img_len, ring)


def check(img, syms, dis, rt):
    errors, notes = [], []
    rewake = idle_rewake(dis)
    if rewake:                      # a sleep loop that never takes the interrupt that wakes it
        errors.append(f"idle reached again within {IDLE_WAKE_SLOTS} instructions of an idle at {rewake[:4]}")
    m = re.search(r"^([0-9a-f]+) .*\s_start$", syms, re.M)
    if not m or int(m.group(1), 16) != APP_XIP:
        errors.append(f"_start is not at {APP_XIP:#x}")
    if img[:4] != bytes.fromhex("04818000"):
        errors.append(f"image starts with {img[:4].hex()}, not the entry stub")
    rt_calls = [ln for ln in rt.splitlines() if re.search(r"\bcall\b", ln)]
    if rt_calls:                    # RAM code runs with the flash off: no calls into XIP
        errors.append(f".ram_text contains calls: {rt_calls[:3]}")
    else:
        notes.append(f".ram_text: {len([ln for ln in rt.splitlines() if LINE.match(ln)])} insns, no calls")
    far = []
    for ln in dis.splitlines():     # nothing may call or load an address in the chip ROM
        mm = LINE.match(ln)
        if not mm:
            continue
        for v in re.findall(r"= (-?\d+) <|call -?\d+ <[^:>]*: ([0-9a-f]+) >", mm.group(3)):
            val = (int(v[0]) & 0xFFFFFFFF) if v[0] else int(v[1], 16) & 0xFFFFFFFF
            if 0xFFC00000 <= val < 0xFFD00000:
                errors.append(f"reference to ROM address {val:#010x}")
        # a direct call between RAM code and XIP is out of reach (core.h HOT, FAR): the linker should
        # refuse it; this catches one that wrapped instead. Target = pc + offset + 4 (also for the
        # conditional form "if (..) { call N }", whose label is printed on the next line)
        c = re.search(r"\bcall (-?\d+)\b", mm.group(3))
        if c:
            pc = int(mm.group(1), 16)
            if (pc >= 0x02000000) != ((pc + int(c.group(1)) + 4) & 0xFFFFFFFF >= 0x02000000):
                far.append(f"{pc:#x} -> {(pc + int(c.group(1)) + 4) & 0xFFFFFFFF:#x}")
    if far:
        errors.append(f"direct calls between RAM and XIP code: {far[:4]}")
    over = errors if not MEASURE else OVER       # a measurement build reports the overflow, it never ships
    if len(img) > APP_SLOT:
        over.append(f"image {len(img)} B exceeds the app slot")

    def sym(name):
        mm = re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M)
        return int(mm.group(1), 16) if mm else 0
    bss = sym("_bss_end") - (0x01C10000 if MEASURE else 0x01C08000)
    pool = sym("_pool_end") - sym("_pool_start")
    notes.append(f"image {len(img)} B; RAM .data+.bss {bss} B of 98304; pool {pool} B of {0x54000}")
    notes.append(f"RAM code: .ram_text {sym('_rt_end') - sym('_rt_start')} B + .ram_hot "
                 f"{sym('_rh_end') - sym('_rh_start')} B of {0x7F00}; .ram_hot2 "
                 f"{sym('_rh2_end') - sym('_rh2_start')} B in RAM (counted in .data+.bss)")
    rtext = sym("_rt_end") - sym("_rt_start") + sym("_rh_end") - sym("_rh_start")
    if MEASURE and rtext > 0x7F00:                 # (a measurement link has a larger RAMTEXT)
        over.append(f"RAMTEXT {rtext} B > {0x7F00} B")
    if bss > 96 * 1024:
        over.append("RAM region overflow")
    burst = re.search(r"^([0-9a-f]+) .*\sfm1_rf_burst_run$", syms, re.M)   # a BLE build's radio start-up
    if burst and not sym("_rt_start") <= int(burst.group(1), 16) < sym("_rt_end"):
        errors.append("fm1_rf_burst_run is not in .ram_text: rf_init's group-5 burst must not fetch from flash "
                      "(hal/fm1_ble_rf.h FM1_RF_BURST_GROUPS)")
    if 0x54000 - pool < 8192:                     # keep >= 8 KiB of the pool spare
        over.append(f"pool headroom {0x54000 - pool} B < 8192 B")
    if sym("_undo_pool_lo") and re.search(r"\sundo_h(\.\S+)?$", syms, re.M):   # the undo history (undo.c)
        upool = max(0, sym("_undo_pool_hi") - sym("_undo_pool_lo"))
        uram = max(0, sym("_undo_ram_hi") - sym("_undo_ram_lo"))
        cap = os.environ.get("FELUCCA_UNDO_CAP", "")
        ring = min(upool + uram, int(cap)) if cap.isdigit() and int(cap) else upool + uram
        notes.append(f"undo history ring {ring} B (pool {upool} B after the 8 KiB spare + main RAM {uram} B"
                     + (f", cap {cap} B" if cap.isdigit() and int(cap) else "") + ")")
        if ring < 1024:
            over.append(f"undo history ring {ring} B < 1024 B (FELUCCA_UNDO_HISTORY=0: the single level)")
        undo_ring = ring
    else:
        undo_ring = None
    if not MEASURE:                 # the reserve the user asked for (a measurement build never ships: it keeps none)
        errors += reserve_check(len(img), undo_ring)
    return errors, notes


# Every hardware register access lives in hal/. In src/ and loader/ (comments stripped):
#  - no volatile pointer cast, except of a C object's address (`*(volatile T *)&x`: a read-once
#    of a RAM flag shared with an ISR, e.g. usb.c ota_wire_send);
#  - no literal in a register or reserved window (core SFRs 0x10000-0x13FFF, SFC 0x40000-0x43FFF,
#    GPIO/IOMAP 0x50000-0x51FFF, CPU 0x1EE0000-0x1EEFFFF, RAM top 0x01C7F000- (boot info,
#    mailbox, vectors), XIP 0x02000000-0x020FFFFF);
#  - no inline asm, except the empty compiler barrier RING_PUBLISH() (emits no instruction);
#  - no HAL register macro (a hal/ #define that is, or expands to, a volatile access).
# Linker symbols (_bss_start[], _rt_load[], ...) are plain C objects and pass.
MMIO_LIT = re.compile(r"\b0x0*(1[0-3][0-9a-f]{3}|4[0-3][0-9a-f]{3}|5[01][0-9a-f]{3}|1ee[0-9a-f]{4}|"
                      r"1c7f[0-9a-f]{3}|20[0-9a-f]{5})u?l?\b", re.I)
MMIO_CAST = re.compile(r"\(\s*(?:const\s+)?volatile\b[^()]*\*\s*\)(?!\s*&)")
MMIO_ASM = re.compile(r"\b(?:__asm__|asm)\b(?!\s+volatile\s*\(\s*\"\"\s*:::\s*\"memory\"\s*\))")


def mmio_check():
    def strip(s):
        s = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), s, flags=re.S)
        return re.sub(r"//[^\n]*", "", s)
    defs = {}
    for h in sorted((FW / "hal").glob("*.h")):
        for m in re.finditer(r"^#define\s+(\w+)(?:\([^)]*\))?\s+(.*)$", strip(h.read_text()), re.M):
            defs[m.group(1)] = m.group(2)
    regs = {n for n, b in defs.items() if re.search(r"\bvolatile\b", b)}
    while True:                                   # macros built on register macros (FM1_WR_LIMIT_H -> FM1_X2)
        more = {n for n, b in defs.items() if n not in regs and set(re.findall(r"\w+", b)) & regs}
        if not more:
            break
        regs |= more
    errors = []
    for f in sorted([*(FW / "src").rglob("*.[ch]"), *(FW / "loader").glob("*.c")]):
        for no, ln in enumerate(strip(f.read_text()).splitlines(), 1):
            where = f"{f.relative_to(FW)}:{no}"
            for rx, what in ((MMIO_LIT, "register/window address"), (MMIO_CAST, "volatile pointer cast"),
                             (MMIO_ASM, "inline asm")):
                m = rx.search(ln)
                if m:
                    errors.append(f"{where}: {what} outside hal/ ({m.group(0).strip()}); add a hal/ helper")
            used = set(re.findall(r"\b[A-Z_][A-Z0-9_]*\b", ln)) & regs
            if used:
                errors.append(f"{where}: HAL register macro {sorted(used)[0]} outside hal/; use a hal/ helper")
    return errors


def setup_config(path):
    """the configuration: build/gen/felucca_config.h (the C switches) and the generators' environment"""
    global CFG_FLAGS
    cfg, name = configure.load(path) if path else (configure.defaults(), "default")
    cfg = configure.apply_env(cfg, os.environ)
    err, warn, note = configure.validate(cfg)
    for w in warn:
        print(f"  warn  {w}")
    for n in note:
        print(f"  NOTE  {n}")
    if err:
        raise SystemExit("build: configuration: " + "; ".join(err))
    flags, env = configure.flags(cfg)
    configure.write_header(cfg, name, GEN / "felucca_config.h")
    CFG_FLAGS = set(flags)
    CFG_VALUES.update(flags)
    RESERVE.update({k: cfg[k] for k in ("RESERVE_FLASH_KB", "RESERVE_UNDO_KB")})
    os.environ.update(env)          # gen_samples.py: the sets, PERC, SLICE's BREAK
    print(f"config   {name}: hash {configure.cfg_hash(cfg):08x}")
    return cfg, name


def ble_rf_tables():
    """a BLE build (FELUCCA_BLE=1) needs build/gen/ble_rf_tables.h: the radio's start-up as the user's own stock V15
    performs it in the emulator (tools/ble_rf_capture.py; docs/BLE-STACK.md §12). The repository carries no vendor
    table, so the header is made on this machine: here, when it is missing or another capture's, if the stock
    firmware and the emulator are found; otherwise the build stops and says how (no BLE build without it)"""
    v = CFG_VALUES.get("FELUCCA_BLE", os.environ.get("FELUCCA_BLE", "0"))
    if str(v) != "1":
        return
    import ble_rf_capture as cap
    hdr = GEN / "ble_rf_tables.h"
    text = hdr.read_text() if hdr.exists() else ""
    if f"#define BLE_RF_TABLES_FORMAT {cap.FORMAT}\n" in text and f'#define BLE_RF_SHA256 "{cap.PINNED}"' in text:
        print(f"ble      {hdr.relative_to(SRC)} (captured {cap.PINNED[:12]})")
        return
    try:
        cap.find_stock(None)
        cap.find_diagnose(None)
    except cap.CaptureError as e:
        raise SystemExit(f"build: a BLE build needs {hdr.relative_to(SRC)}, the radio's start-up tables captured from "
                         f"your own stock V15 in the FM-1 emulator, and they cannot be made here: {e}\n"
                         f"  make them with: python3 tools/ble_rf_capture.py --stock PATH/FM-1.fwsc "
                         f"[--diagnose PATH/diagnose]   (docs/BLE-STACK.md §12)")
    print(f"ble      capturing the radio's start-up tables into {hdr.relative_to(SRC)} (stock V15 in the emulator, "
          "about 30 s)")
    if cap.main(["--out", str(hdr)]):
        raise SystemExit("build: tools/ble_rf_capture.py failed (above)")


NOTICE_FILES = ("LICENSE", "LICENSING.md", "LICENSES/Apache-2.0.txt")   # in every -ui.zip, next to every package


def ui_sidecar(fwsc):
    """FIRMWARE-ui.zip next to the package: the web editor as this tree has it (index.html = web/editor.html, its
    font and licence) and SOURCE.txt (the commit, whether the tree had changes, the configuration's hash): the
    emulator's fm1-ui serves it beside the firmware (as rust-emulator/scripts/make-ui-sidecar.sh did by hand)"""
    out = fwsc.with_name(fwsc.stem + "-ui.zip")
    web = SRC / "web"
    def git(*a):
        try:                            # (no git: a tree from a zip, or Windows without git)
            p = subprocess.run(["git", "-C", str(SRC), *a], capture_output=True, text=True)
        except OSError:
            return ""
        return p.stdout.strip() if p.returncode == 0 else ""
    commit, dirty = git("rev-parse", "HEAD") or "unknown", git("status", "--porcelain", "--", "web", "firmware")
    cfg = (GEN / "felucca_config.h").read_text() if (GEN / "felucca_config.h").exists() else ""
    m = re.search(r'FELUCCA_CFG_NAME "([^"]*)"', cfg)
    h = re.search(r"FELUCCA_CFG_HASH (\d+)u", cfg)
    source = (f"Web editor for {fwsc.name}\nfrom {SRC} at {commit}{' (with uncommitted changes)' if dirty else ''}, "
              f"web/editor.html (GPL-3.0-only)\nbuild configuration: {m.group(1) if m else 'default'}"
              f"{' hash %08x' % int(h.group(1)) if h else ''}\n")
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        def add(name, data):
            zi = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            zi.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(zi, data)
        add("index.html", (web / "editor.html").read_bytes())
        for f in ("fukiai.ttf", "FUKIAI-LICENSE.txt", "TERMINUS-LICENSE.txt"):
            if (web / f).exists():
                add(f, (web / f).read_bytes())
        for f in NOTICE_FILES:                  # the package holds JieLi SDK files (Apache-2.0): the licence travels
            add(f, (SRC / f).read_bytes())
        add("SOURCE.txt", source)
    return out


def main():
    global PRODUCT, VERSION, MEASURE, BACKEND
    ap = argparse.ArgumentParser()
    ap.add_argument("--release", metavar="X.Y", help="release build: identity FM-1_7XY, version string X.Y")
    ap.add_argument("--sdk", type=Path, help="JieLi AC79 SDK checkout (default: $AC79_SDK)")
    ap.add_argument("--config", type=Path, help="a firmware builder .config (tools/menuconfig; default: every "
                    "default, with the FELUCCA_* environment switches applied)")
    ap.add_argument("--measure", action="store_true",
                    help="measurement build: links past the app slot and the pool, writes build/sizes.json, no package")
    ap.add_argument("--host-headers", action="store_true",
                    help="only the host tests' headers: build/gen-host with every sample set (no toolchain; "
                    "tests/run_tests.sh runs it)")
    a = ap.parse_args()
    if a.host_headers:
        generate(HOST_GEN, HOST_ENV)
        return 0
    MEASURE = a.measure
    env = dict(os.environ, AC79_SDK=str(a.sdk)) if a.sdk else os.environ
    BACKEND, sdk = TC.preflight(env)     # Pillow, the toolchain, the SDK files: what is missing and how to get it
    print(f"toolchain {BACKEND.describe()}")
    setup_config(a.config)
    VERSION = f"OPTIMIST {OPTIMIST_VERSION}"     # (the screen's version: ui.c's default says the same)
    name = "felucca.fwsc"                       # (the internal name tests and tools read; see release_name)
    if a.release:                   # one digit each: the identity has room for two
        m = re.fullmatch(r"(\d)\.(\d)(-[A-Za-z0-9]+)?", a.release)
        if not m:
            raise SystemExit(f"--release {a.release}: use X.Y or X.Y-suffix, one digit each")
        PRODUCT = "FM-1_7" + m[1] + m[2]
        VERSION = a.release.upper() if "BETA" in a.release.upper() else a.release.upper() + " BETA"
        if a.release.split("-")[0] != OPTIMIST_VERSION:
            raise SystemExit(f"--release {a.release}: VERSION says {OPTIMIST_VERSION}; update VERSION first")
    fm1pkg_make.SDK = sdk
    for rel, sha in SDK_SHA256.items():          # fail early without the SDK
        if hashlib.sha256(fm1pkg_make.sdk_file(rel)).hexdigest() != sha:
            print(f"warning: SDK {rel} differs from AC79NN_SDK_V1.2.1; the package will not match the reference")
    OUT.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(2) as ex:
        gen, ldr = ex.submit(generate), ex.submit(build_loader)
        gen.result()
        ota = ldr.result()
    ble_rf_tables()
    img, syms, dis, rt, hdr = build_app()
    (OUT / "sizes.json").write_text(json.dumps(sizes(img, syms, hdr), indent=1) + "\n")
    errors, notes = check(img, syms, dis, rt)
    hal_err = mmio_check()
    errors += hal_err
    if not hal_err:
        notes.append("register access: hal/ only (src/, loader/ clean)")
    for n in notes:
        print("  ok   ", n)
    for n in OVER:
        print("  over ", n)
    for e in errors:
        print("  FAIL ", e)
    if errors:
        raise SystemExit("build: checks failed")
    if MEASURE:
        print(f"measure  {OUT / 'sizes.json'} (no package: a measurement build is not flashable)")
        return 0
    pkg = fm1pkg_make.ufw(fm1pkg_make.flash_image(img, fm1pkg_make.KEY), ota, PRODUCT)
    (OUT / name).write_bytes(pkg)
    att = SRC / "assets" / "samples-cc0" / "ATTRIBUTION.txt"
    if att.exists():
        shutil.copy(att, OUT / "ATTRIBUTION.txt")
    print(f"app      {OUT / 'felucca.bin'}  {len(img)} B")
    print(f"loader   {LDR / 'ota.bin'}  {len(ota)} B")
    print(f"package  {OUT / name}  {len(pkg)} B, identity {PRODUCT}")
    ui = ui_sidecar(OUT / name)
    print(f"ui       {ui}  ({', '.join(zipfile.ZipFile(ui).namelist())})")
    named = OUT / release_name(a.release)       # the same package under its user-facing name
    for old in list(OUT.glob("optimist-*.fwsc")) + list(OUT.glob("optimist-*-ui.zip")):
        old.unlink()
    shutil.copy(OUT / name, named)
    shutil.copy(ui, named.with_name(named.stem + "-ui.zip"))
    print(f"named    {named}  (+ {named.stem}-ui.zip)")
    return 0


def release_name(release=None):
    """optimist-X.Y.fwsc for a release build, else optimist-X.Y-dev-<commit>.fwsc (-modified: uncommitted changes)"""
    if release:
        return f"optimist-{release}.fwsc"
    def git(*args):
        try:                            # (no git: a tree from a zip, or Windows without git)
            r = subprocess.run(["git", "-C", str(SRC), *args], capture_output=True, text=True)
        except OSError:
            return ""
        return r.stdout.strip() if r.returncode == 0 else ""
    commit = git("rev-parse", "--short", "HEAD") or "local"
    changed = git("status", "--porcelain", "--", "firmware", "tools", "web")
    return f"optimist-{OPTIMIST_VERSION}-dev-{commit}{'-modified' if changed else ''}.fwsc"


if __name__ == "__main__":
    sys.exit(main())
