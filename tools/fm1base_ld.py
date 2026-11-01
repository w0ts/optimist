#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Henri David
"""The linker of a measurement build of the release (tools/fm1base.py measure_toolchain): fm1-base's build has no
measurement mode (Optimist's own tools/build.py --measure links past the slot and the pool), so its toolchain's `ld` is
this wrapper, which gives the linker script room (the app slot, the SRAM, a NOINIT region beyond it, no check that
SRAM ends at the stacks) and runs the real linker (FM1B_REAL_LD). The link then succeeds for any configuration, fm1-base's
build writes its sizes.json (flash, ram, pool, ramtext) before it checks the slots, and the build itself still reports
what is over (rc 1, no package): a measurement, never a flashable image."""
import os
import re
import subprocess
import sys
from pathlib import Path

XIP_LENGTH = "0x800000"          # (the flash window: 8 MiB of address space)
SRAM_LENGTH = "0x180000"         # (as Optimist's own measurement link: tools/build.py MEASURE_SRAM)
NOINIT_ORIGIN = "0x01E00000"     # (past the enlarged SRAM: the real one sits at 0x01C7C000, inside it)


def measure_script(text):
    """-> the linker script with room for any build: None when it is not fm1-base's app.ld (the app's map)"""
    out, n = re.subn(r"(XIP\s*\(rx\)\s*:\s*ORIGIN = 0x[0-9A-Fa-f]+, LENGTH = )0x[0-9A-Fa-f]+", rf"\g<1>{XIP_LENGTH}", text)
    out, m = re.subn(r"(SRAM\s*\(rwx\)\s*:\s*ORIGIN = 0x[0-9A-Fa-f]+, LENGTH = )0x[0-9A-Fa-f]+", rf"\g<1>{SRAM_LENGTH}", out)
    out, k = re.subn(r"(NOINIT\s*\(rw\)\s*:\s*ORIGIN = )0x[0-9A-Fa-f]+", rf"\g<1>{NOINIT_ORIGIN}", out)
    out = re.sub(r"ASSERT\(_sram_end == _guard0[^\n]*\n", "", out)
    return out if n == m == k == 1 else None


def main(argv):
    real = os.environ.get("FM1B_REAL_LD")
    if not real:
        sys.exit("fm1base_ld: FM1B_REAL_LD (the toolchain's own ld) is not set")
    args = list(argv)
    if "-T" in args:
        i = args.index("-T") + 1
        script = Path(args[i])
        new = measure_script(script.read_text())
        if new is not None:                 # (the update loader's script is another one: linked as it is)
            elf = Path(args[args.index("-o") + 1]) if "-o" in args else script    # (beside the output: never in fm1-base's own device/)
            out = elf.with_name(script.stem + ".measure.ld")
            out.write_text(new)
            args[i] = str(out)
    return subprocess.call([real, *args])


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
