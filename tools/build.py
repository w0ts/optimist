#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Build Felucca: the app, the update loader and an installable .fwsc package.

  tools/build.py [--release X.Y[-suffix]]

Outputs in build/: felucca.bin (app), loader/ota.bin (update loader),
felucca.fwsc (package). See BUILDING.md for the toolchain and the SDK.

The JieLi toolchain is Linux x86-64 only. JIELI_TOOLCHAIN points at it; on
macOS (or with JIELI_DOCKER=1) each tool runs in a linux/amd64 container.
"""
import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
FW = SRC / "firmware"
OUT = SRC / "build"
GEN = OUT / "gen"
LDR = OUT / "loader"
sys.path.insert(0, str(SRC / "tools"))
import fm1pkg_make  # noqa: E402
import lz4blk  # noqa: E402
sys.path.insert(0, str(SRC / "tools" / "builder"))
import configure  # noqa: E402  (the firmware builder: .config -> build/gen/felucca_config.h)

APP_XIP = 0x02000120                # app.bin offset 0 in the XIP map; the SPL jumps here
APP_SLOT = fm1pkg_make.APP_SLOT
LOADER_LOAD = 0x01C0A800
LOADER_NAME = b"usb_hid_ota.bin"    # the file name the SPL looks for
DOCKER_IMAGE = os.environ.get("JIELI_DOCKER_IMAGE", "debian:bookworm-slim")
CFLAGS = ["-Os", "-ffunction-sections", "-fno-builtin", "-Wall", "-Wno-unused-function"]
LINE = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*\t(.*)$")

# SDK files of AC79NN_SDK_V1.2.1_2023-12-13 (the tested version)
SDK_SHA256 = {
    "uboot.boot": "4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4",
    "cfg_tool.bin": "276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b",
    "cfg/eq_cfg_hw.bin": "41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d",
}

# package identity (BUILDING.md "Package identity"): Optimist is FM-1_7XY. FM-1_ + three digits is the
# form the stock updater and every installer accept (M-VAVE 0XX, Baud Girl 020-09X, Lunar 5XX, Felucca /
# SLOOP / X0X 9XX); release builds are FM-1_7XY, development builds FM-1_700
PRODUCT = "FM-1_700"
VERSION = None                      # FELUCCA_VERSION for release builds (default: firmware/src/ui.c)
MEASURE = False                     # --measure: link past the slot and the pool (sizes only, never a package)
CFG_FLAGS = set()                   # the switches build/gen/felucca_config.h sets (the env loop below skips them)
CFG_VALUES = {}                     # ... and their values
# the budgets (bytes): the app slot, main RAM .data+.bss, the pool (and the spare build.py keeps), RAM code, noinit
LIMITS = {"flash": 0x8DFBC, "ram": 96 * 1024, "pool": 0x54000, "pool_spare": 8192, "ramtext": 0x7F00,
          "noinit": 0x3D50}


def toolchain():
    tc = os.environ.get("JIELI_TOOLCHAIN")
    if not tc or not (Path(tc) / "pi32v2" / "bin" / "clang").exists():
        raise SystemExit("JIELI_TOOLCHAIN must point at the JieLi Linux toolchain "
                         "(the directory with pi32v2/ and common/; see tools/get_toolchain.sh)")
    return Path(tc).resolve()


def use_docker():
    native = platform.system() == "Linux" and platform.machine() in ("x86_64", "AMD64")
    return os.environ.get("JIELI_DOCKER", "0" if native else "1") == "1"


def tc(tool, *args):
    """run a toolchain binary (pi32v2/bin/..., common/bin/...) with cwd SRC; paths relative to SRC"""
    rel = [str(Path(a).resolve().relative_to(SRC)) if isinstance(a, Path) else a for a in args]
    if tool == "cc":                # the toolchain's cc wrapper needs python3; call clang directly
        tool, rel = "pi32v2/bin/clang", ["-target", "pi32v2", *rel]
    if use_docker():
        cmd = ["docker", "run", "--rm", "--platform", "linux/amd64", "-v", f"{SRC}:/work",
               "-v", f"{toolchain()}:/opt/jieli:ro", "-w", "/work", DOCKER_IMAGE, f"/opt/jieli/{tool}", *rel]
    else:
        cmd = [str(toolchain() / tool), *rel]
    r = subprocess.run(cmd, cwd=SRC, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"build: {tool} failed")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    return r.stdout


def tc_all(*cmds):
    with ThreadPoolExecutor(len(cmds)) as ex:
        return list(ex.map(lambda c: tc(*c), cmds))


def generate():
    """generated headers (fonts, icons, tables, samples)"""
    GEN.mkdir(parents=True, exist_ok=True)
    tools = SRC / "tools"
    cmds = [[tools / "gen_font.py", GEN / "felucca_font.h"],
            [tools / "gen_icons.py", GEN / "felucca_icons.h"],
            [tools / "gen_tables.py", GEN / "felucca_tables.h"],
            [tools / "gen_samples.py", GEN / "felucca_samples.h"],
            [tools / "gen_drumkits.py", GEN / "felucca_drumkits.h"],
            [tools / "gen_logo.py", GEN / "sloop_logo.h"]]
    procs = [subprocess.Popen([sys.executable, *map(str, c)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True) for c in cmds]
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
# the backported features' switches (firmware/src/backports.h; provenance and costs: tools/backports.json)
BACKPORT_FLAGS = ("FELUCCA_CHANCE", "FELUCCA_KEYLIT", "FELUCCA_QNT_SEQ", "FELUCCA_SPRING", "FELUCCA_BASSPLUS",
                  "FELUCCA_BRIGHT", "FELUCCA_DLY_HALVE", "FELUCCA_MOTION", "FELUCCA_ENG_PHYS", "FELUCCA_ENG_ACID")

def build_app():
    flags = [*CFLAGS, "-Ifirmware/hal", "-Ifirmware/src", "-Ibuild/gen"]
    for flag in ("FELUCCA_FLASH", "FELUCCA_OTA", "FELUCCA_OTA_DRYRUN", "FELUCCA_CDC", "FELUCCA_UART",
                 "FELUCCA_ICONS", "FELUCCA_SLICE", "FELUCCA_FM6_KEYS", "FELUCCA_ANALOG2",
                 "FELUCCA_ASM", "FELUCCA_ASM_CHECK", "FELUCCA_IDLE", "FELUCCA_SPLASH",
                 "FELUCCA_USB_AUDIO", "FELUCCA_SIMD", "FELUCCA_SIMD_CHECK", "FELUCCA_SIMD_PROBE",
                 "FELUCCA_SIMD_PROBE_TEST", "FELUCCA_DRUM_EDIT", "FELUCCA_DRUM_USR", "FELUCCA_DRUM_KITS",
                 "FELUCCA_KNOB_ACCEL", "FELUCCA_LCD_DIRTY", "FELUCCA_UA_RESAMPLE", *BACKPORT_FLAGS):
        v = os.environ.get(flag)    # unset: the default in firmware/src/felucca.c
        if v in ("0", "1") and flag not in CFG_FLAGS:
            flags.append(f"-D{flag}={v}")
    flags += ["-include", "build/gen/felucca_config.h"]   # the builder's configuration (tools/builder)
    v = os.environ.get("FELUCCA_LCD_BAUD")    # LCD SPI clock = 60 MHz / (v + 1); default 1 (lcd.c)
    if v is not None and len(v) == 1 and v in "01234":
        flags.append(f"-DLCD_BAUD={v}u")
    v = os.environ.get("FELUCCA_DLY_LEN")     # the delay line in samples (a power of two; fx.c checks)
    if v and v.isdigit() and "FELUCCA_DLY_LEN" not in CFG_FLAGS:
        flags.append(f"-DFELUCCA_DLY_LEN={v}u")
    for flag, ok in (("FELUCCA_DUAL", "012"), ("FELUCCA_BENCH", "0123"), ("FELUCCA_BENCH_SAVE", "01"),
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
        # the ACID engine's float DSP (firmware/src/acid/, from X0X): its own unit, with X0X's FPU flags (the rest
        # of the firmware stays integer-only), -O2 as X0X builds it
        units.append(("cc", *ACID_CFLAGS, "-c", FW / "src" / "acid" / "acid_dsp.c", "-o", OUT / "acid.o"))
        objs.append(OUT / "acid.o")
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
        ld.write_text((FW / "app.ld").read_text().replace("LENGTH = 0x8DFBC", "LENGTH = 0xADFBC")
                      .replace("LENGTH = 96K", "LENGTH = 128K")             # (RAM, POOL and NOINIT moved up:
                      .replace("ORIGIN = 0x01C20000, LENGTH = 0x54000",    # addresses for sizes only)
                               "ORIGIN = 0x01C28000, LENGTH = 0x80000")
                      .replace("ORIGIN = 0x01C7C000", "ORIGIN = 0x01CA8000"))
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
    return {"flash": len(img), "ram": sym("_bss_end") - 0x01C08000, "pool": sym("_pool_end") - sym("_pool_start"),
            "ramtext": sym("_rt_end") - sym("_rt_start") + sym("_rh_end") - sym("_rh_start"),
            "noinit": sec.get(".noinit", 0), "limits": LIMITS}


OVER = []


def check(img, syms, dis, rt):
    errors, notes = [], []
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
    bss = sym("_bss_end") - 0x01C08000
    pool = sym("_pool_end") - sym("_pool_start")
    notes.append(f"image {len(img)} B; RAM .data+.bss {bss} B of 98304; pool {pool} B of {0x54000}")
    notes.append(f"RAM code: .ram_text {sym('_rt_end') - sym('_rt_start')} B + .ram_hot "
                 f"{sym('_rh_end') - sym('_rh_start')} B of {0x7F00}; .ram_hot2 "
                 f"{sym('_rh2_end') - sym('_rh2_start')} B in RAM (counted in .data+.bss)")
    if bss > 96 * 1024:
        over.append("RAM region overflow")
    if 0x54000 - pool < 8192:                     # keep >= 8 KiB of the pool spare
        over.append(f"pool headroom {0x54000 - pool} B < 8192 B")
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
    for f in sorted([*(FW / "src").glob("*.[ch]"), *(FW / "loader").glob("*.c")]):
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
    os.environ.update(env)          # gen_samples.py: the sets, PERC, SLICE's BREAK
    print(f"config   {name}: hash {configure.cfg_hash(cfg):08x}")
    return cfg, name


def main():
    global PRODUCT, VERSION, MEASURE
    ap = argparse.ArgumentParser()
    ap.add_argument("--release", metavar="X.Y", help="release build: identity FM-1_7XY, version string X.Y")
    ap.add_argument("--sdk", type=Path, help="JieLi AC79 SDK checkout (default: $AC79_SDK)")
    ap.add_argument("--config", type=Path, help="a firmware builder .config (tools/menuconfig; default: every "
                    "default, with the FELUCCA_* environment switches applied)")
    ap.add_argument("--measure", action="store_true",
                    help="measurement build: links past the app slot and the pool, writes build/sizes.json, no package")
    a = ap.parse_args()
    MEASURE = a.measure
    setup_config(a.config)
    name = "felucca.fwsc"
    if a.release:                   # one digit each: the identity has room for two
        m = re.fullmatch(r"(\d)\.(\d)(-[A-Za-z0-9]+)?", a.release)
        if not m:
            raise SystemExit(f"--release {a.release}: use X.Y or X.Y-suffix, one digit each")
        PRODUCT = "FM-1_7" + m[1] + m[2]
        VERSION = a.release.upper() if "BETA" in a.release.upper() else a.release.upper() + " BETA"
        name = f"felucca-{a.release}.fwsc"
    fm1pkg_make.SDK = a.sdk
    for rel, sha in SDK_SHA256.items():          # fail early without the SDK
        if hashlib.sha256(fm1pkg_make.sdk_file(rel)).hexdigest() != sha:
            print(f"warning: SDK {rel} differs from AC79NN_SDK_V1.2.1; the package will not match the reference")
    OUT.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(2) as ex:
        gen, ldr = ex.submit(generate), ex.submit(build_loader)
        gen.result()
        ota = ldr.result()
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
    return 0


if __name__ == "__main__":
    sys.exit(main())
