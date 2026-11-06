#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Builds the FM-1 recovery dongle's firmware: FM-1-transporter (kurogedelic, MIT) at the commit pinned below,
# with tools/dongle/transporter-optimist.patch, for the Seeed XIAO RP2040 and the Raspberry Pi Pico.
#
#   tools/dongle/build.sh                 -> build/dongle/fm1-dongle-seeed_xiao_rp2040.uf2, fm1-dongle-pico.uf2
#   BOARDS=pico_w tools/dongle/build.sh   -> one other pico-sdk board (GP0 = D+, GP1 = D-, as on the Pico)
#
# Everything fetched lands in build/ (git-ignored): pico-sdk 2.2.0 with TinyUSB, and the transporter with its
# Pico-PIO-USB submodule. Needs cmake, ninja and arm-none-eabi-gcc; on macOS:
#   brew install cmake ninja && brew install --cask gcc-arm-embedded
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD="$ROOT/build/dongle"
SDK="$ROOT/build/pico-sdk"
SRC="$BUILD/FM-1-transporter"
PATCH="$ROOT/tools/dongle/transporter-optimist.patch"
TRANSPORTER_URL=https://github.com/kurogedelic/FM-1-transporter
TRANSPORTER_REV=14fd4dfe08b5afb403693142b07fcb21a95092ba
SDK_REV=2.2.0
BOARDS="${BOARDS:-seeed_xiao_rp2040 pico}"

for tool in git cmake ninja arm-none-eabi-gcc patch; do
    command -v "$tool" >/dev/null 2>&1 || { echo "build.sh: $tool is missing (see the header of this script)" >&2; exit 1; }
done
mkdir -p "$BUILD"

if [ ! -d "$SDK/.git" ]; then
    echo "fetching pico-sdk $SDK_REV ..."
    git clone -q --depth 1 --branch "$SDK_REV" https://github.com/raspberrypi/pico-sdk "$SDK"
fi
git -C "$SDK" submodule update -q --init --depth 1 lib/tinyusb

if [ ! -d "$SRC/.git" ]; then
    echo "fetching FM-1-transporter ..."
    git clone -q "$TRANSPORTER_URL" "$SRC"
fi
git -C "$SRC" cat-file -e "$TRANSPORTER_REV^{commit}" 2>/dev/null || git -C "$SRC" fetch -q origin
git -C "$SRC" checkout -q --detach "$TRANSPORTER_REV"
git -C "$SRC" checkout -q -- .
git -C "$SRC" submodule update -q --init
git -C "$SRC" apply "$PATCH"
echo "FM-1-transporter $(git -C "$SRC" rev-parse --short HEAD) + $(basename "$PATCH")"

for board in $BOARDS; do
    out="$BUILD/fm1-dongle-$board.uf2"
    cmake -S "$SRC" -B "$SRC/build-$board" -G Ninja -DPICO_SDK_PATH="$SDK" -DPICO_BOARD="$board" \
        >"$BUILD/cmake-$board.log" 2>&1 || { echo "build.sh: cmake failed for $board, see $BUILD/cmake-$board.log" >&2; exit 1; }
    ninja -C "$SRC/build-$board" fm1_transporter >"$BUILD/ninja-$board.log" 2>&1 \
        || { echo "build.sh: the build failed for $board, see $BUILD/ninja-$board.log" >&2; exit 1; }
    cp "$SRC/build-$board/fm1_transporter.uf2" "$out"
    echo "$out"
done
shasum -a 256 "$BUILD"/fm1-dongle-*.uf2
