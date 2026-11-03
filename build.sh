#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Build Felucca (see BUILDING.md); a wrapper of tools/build.py, which finds the toolchain and the SDK files
# (tools/toolchain.py) and says what is missing. On any host: python tools/optimist.py build.
#   ./build.sh [--release X.Y] [--config FILE] [--measure]
#   PI32V2_LLVM_TOOLCHAIN  a pi32v2-llvm drop-in directory (default: found, tools/toolchain.py)
#   AC79_SDK         the AC79 SDK files (default: ./sdk, else the legacy ~/fw-AC79_AIoT_SDK)
set -e
cd "$(dirname "$0")"
exec "${PYTHON:-python3}" tools/build.py "$@"
