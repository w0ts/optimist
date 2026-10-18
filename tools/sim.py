#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The simulator: a configuration's firmware built for the host (a test) or for the browser (WebAssembly).

  python3 tools/sim.py build [--config FILE] [--out DIR] [--wasm]

The firmware is the device's own single compilation unit (firmware/src/felucca.c), with firmware/sim/hal/ in front of
firmware/hal/: the headers that touch the chip are replaced by host versions (time is simulated, the flash is a RAM
image, the screen a 240x240 buffer, the keys and knobs set from outside, USB MIDI a pair of queues). firmware/sim/sim.c
is the platform: what the browser (web/sim/) or a host test calls. What cannot run there is switched off whatever the
configuration says (SIM_FORCE): the second core, Bluetooth, the cache RAM, the key scan's DMA, USB audio / serial.
--wasm (emcc, Emscripten): build/sim/sim.wasm, a standalone module with no imports (tests/sim_test.mjs). Without it, an
attempt at a host executable with $CC (the sources carry ELF section names: macOS's cc refuses them). docs/SIMULATOR.md.
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "builder"))
import configure  # noqa: E402  (.config -> felucca_config.h, as tools/build.py)

SIM_FORCE = {"DUAL": 0, "BLE": 0, "CACHE_RAM": 0, "KEYS_DMA": 0, "BT_INQ_PROBE": 0, "USB_MODE": 0}
SIM_FLAGS = ["-DFELUCCA_SIM=1", "-DFELUCCA_SIMD=0", "-DFELUCCA_SIMD_PROBE=0", "-DFELUCCA_ASM=0", "-DFELUCCA_BENCH=0",
             "-DFELUCCA_STOCK_SUPPLY=0",
             # the firmware's own memset / memcpy (system/libc.c, byte loops) renamed, as tests/hostsim.c: the
             # compiler's own calls (struct copies) and the loops it recognises go to the C library's
             "-Dmemset=felucca_memset", "-Dmemcpy=felucca_memcpy", "-Dmemmove=felucca_memmove", "-Dmemcmp=felucca_memcmp"]
# the HAL headers src/ includes by a relative path (usb.c, midi_uart.c, eng_sample.c): the host version instead (a clang
# VFS overlay; the other headers are found first in firmware/sim/hal through -I)
SIM_OVERLAY = ["fm1_usb.h", "fm1_uart.h", "fm1_xip.h"]
CFLAGS = ["-std=gnu11", "-O2", "-fwrapv", "-fno-strict-aliasing", "-w", "-ferror-limit=0"]
# a standalone reactor: no imports, its memory exported, no growth (the page's AudioWorklet instantiates it with {} and
# keeps typed-array views); the exports are firmware/sim/sim.c SIM_EXPORT. sim_stop's longjmp: wasm exceptions
WASM_CFLAGS = ["-sSUPPORT_LONGJMP=wasm"]
WASM_LINK = ["-sSTANDALONE_WASM=1", "--no-entry", "-sALLOW_MEMORY_GROWTH=0",
             "-sINITIAL_MEMORY=16MB", "-sSTACK_SIZE=262144", "-sFILESYSTEM=0", "-sSUPPORT_LONGJMP=wasm"]


def sim_config(path):
    """-> (cfg, name, notes): the configuration with SIM_FORCE applied, validated by the builder's own rules"""
    cfg, name = configure.load(path) if path else (configure.defaults(), "default")
    forced = [f"{k}={v}" for k, v in SIM_FORCE.items() if k in cfg and cfg[k] != v]
    cfg = dict(cfg, **{k: v for k, v in SIM_FORCE.items() if k in cfg})
    err, _warn, _note = configure.validate(cfg)
    if err:
        raise SystemExit("sim: configuration: " + "; ".join(err))
    return cfg, name, forced


def generate(cfg, name, gen):
    """felucca_config.h and the generated headers for this configuration into gen"""
    sys.path.insert(0, str(ROOT / "tools"))
    import build  # noqa: E402  (its generators; importing it changes nothing)
    gen.mkdir(parents=True, exist_ok=True)
    flags, env = configure.flags(cfg)
    configure.write_header(cfg, name, gen / "felucca_config.h")
    build.generate(gen, env)


def overlay(gen):
    """a clang VFS overlay: firmware/hal/<SIM_OVERLAY> read as firmware/sim/hal/<same>"""
    import json
    hal, sim = ROOT / "firmware" / "hal", ROOT / "firmware" / "sim" / "hal"
    o = {"version": 0, "case-sensitive": "false", "roots": [{"name": str(hal), "type": "directory", "contents": [
        {"name": h, "type": "file", "external-contents": str(sim / h)} for h in SIM_OVERLAY]}]}
    p = gen / "sim_overlay.yaml"
    p.write_text(json.dumps(o, indent=1))
    return p


def compile_sim(gen, out, wasm):
    src = ["-Xclang", "-ivfsoverlay", "-Xclang", str(overlay(gen)), "-I", str(ROOT / "firmware" / "sim" / "hal"),
           "-I", str(ROOT / "firmware" / "hal"),
           "-I", str(ROOT / "firmware" / "src"), "-I", str(gen), "-include", str(gen / "felucca_config.h")]
    main = str(ROOT / "firmware" / "sim" / "sim.c")
    if wasm:
        emcc = shutil.which("emcc")
        if not emcc:
            raise SystemExit("sim: --wasm needs emcc (Emscripten: brew install emscripten)")
        cmd = [emcc, *CFLAGS, *WASM_CFLAGS, *SIM_FLAGS, *src, main, *WASM_LINK, "-o", str(out)]
    else:
        cmd = [os.environ.get("CC", "cc"), *CFLAGS, *SIM_FLAGS, "-DSIM_HOST_MAIN=1", *src, main, "-lm", "-o", str(out)]
    r = subprocess.run(cmd)
    if r.returncode:
        raise SystemExit(f"sim: the compiler failed ({r.returncode})")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build", help="a configuration's simulator: a host executable, or --wasm for the browser")
    b.add_argument("--config", help="a .config (default: the defaults)")
    b.add_argument("--out", help="the output (default build/sim/sim or build/sim/sim.wasm)")
    b.add_argument("--wasm", action="store_true", help="WebAssembly with emcc (a standalone sim.wasm)")
    a = ap.parse_args(argv)
    cfg, name, forced = sim_config(a.config)
    work = ROOT / "build" / "sim"
    gen = work / "gen"
    if forced:
        print(f"sim: switched off for the simulator: {', '.join(forced)}")
    generate(cfg, name, gen)
    out = Path(a.out) if a.out else work / ("sim.wasm" if a.wasm else "sim")
    out.parent.mkdir(parents=True, exist_ok=True)
    compile_sim(gen, out, a.wasm)
    print(f"sim: {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
