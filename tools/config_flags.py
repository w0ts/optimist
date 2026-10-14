#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A builder .config -> the -D flags a host test is compiled with (tests/run_tests.sh, the "mots-like" Optimist UI variant).

  python3 tools/config_flags.py tests/profiles/mots-like.config

The flags are the ones the firmware build gets (builder configure.flags(): every FELUCCA_* macro of the configuration),
less the target-only items (registry target_only: as felucca_config.h, the host keeps its defaults); the second core
(FELUCCA_DUAL, system/dual.c) is replaced by DUAL_LOAD=1 (its readout, system/dual_load.h) when the configuration builds it (DUAL 2). Prints one line.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "builder"))
import configure as C  # noqa: E402

def target_only():
    """the macros the generated felucca_config.h gives the target alone (#ifdef __PI32V2__; the host keeps its defaults)"""
    return {C.R.ITEMS[k].flag for k in C.R.ITEMS if C.R.ITEMS[k].target_only}


def host_flags(path):
    cfg, _ = C.load(path)
    macros, _env = C.flags(cfg)
    skip = target_only()
    out = [f"-D{k}={v}" for k, v in sorted(macros.items()) if k not in skip]
    if macros.get("FELUCCA_DUAL", 0) >= 2:
        out.append("-DDUAL_LOAD=1")
    return out


def main(argv):
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    print(" ".join(host_flags(argv[1])))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
