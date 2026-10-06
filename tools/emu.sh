#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Run a firmware in the FM-1 emulator; a wrapper of tools/emu.py (any host: python tools/optimist.py emu).
#   tools/emu.sh [FIRMWARE] [--cpu MHZ|own] [--bg] [--rebuild] [--list] [--update]     (--help: the details)
# IMAGES, EMU_REPO, EMU_BRANCH and EMU_DIR work as before.
exec "${PYTHON:-python3}" "$(dirname "$0")/optimist.py" emu "$@"
