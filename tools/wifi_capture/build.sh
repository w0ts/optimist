#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The Wi-Fi start-up capture image (tools/wifi_capture/README.md), in the toolchain image (tools/toolchain.py).
#   tools/wifi_capture/build.sh <wl_rf_common.a> <wifi_conf.c> [out dir, default worktrees/wifi-capture] [CAP_RF_TRIM 0|1]
#       [wl_wifi_sta.a: also the Ralink STA driver, and the image runs the SDK's usb_register (CAP_STA=1)]
#   CAP_WLC_O=<cpu.a's wlc.c.o, extracted>: the real coexistence set-up (wlc_main) instead of the stand-in
# Inputs are the user's own AC79 SDK files (gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK, AC79NN_SDK_V1.2.1_2023-12-13);
# everything made from them stays in the out dir (git-ignored). Needs llvm-ar on the host (Homebrew llvm).
set -e
LIB=${1:?wl_rf_common.a}
CONF=${2:?wifi_conf.c}
OUT=${3:-worktrees/wifi-capture}
TRIM=${4:-0}
STA=${5:-}
IMG=${OPTIMIST_TOOLCHAIN_IMAGE:-optimist-toolchain:20250324.1}
LLVM_AR=${LLVM_AR:-$(command -v llvm-ar || echo /opt/homebrew/opt/llvm/bin/llvm-ar)}
mkdir -p "$OUT/rf"
(cd "$OUT/rf" && rm -f ./*.bc ./*.o && "$LLVM_AR" x "$(cd - >/dev/null && cd "$(dirname "$LIB")" && pwd)/$(basename "$LIB")")
for f in "$OUT"/rf/*.c.o; do mv "$f" "${f%.c.o}.bc"; done
rm -f "$OUT"/rf/version.z.o
WLC_DEF=-DCAP_WLC=0
if [ -n "$CAP_WLC_O" ]; then              # the SDK's cpu.a wlc.c.o (the coexistence set-up) instead of the stand-in
    cp "$CAP_WLC_O" "$OUT/rf/wlc.bc"
    WLC_DEF=-DCAP_WLC=1
fi
STA_OBJS= STA_DEF=-DCAP_STA=0 NAME=wifi_capture_t$TRIM
if [ -n "$STA" ]; then
    mkdir -p "$OUT/sta"
    (cd "$OUT/sta" && rm -f ./*.bc ./*.o && "$LLVM_AR" x "$(cd - >/dev/null && cd "$(dirname "$STA")" && pwd)/$(basename "$STA")")
    for f in "$OUT"/sta/*.c.o; do mv "$f" "${f%.c.o}.bc"; done
    rm -f "$OUT"/sta/version.z.o
    STA_OBJS="$OUT/sta/*.o" STA_DEF=-DCAP_STA=1 NAME=wifi_capture_sta_t$TRIM
fi
python3 tools/wifi_capture/gen_conf.py "$CONF" "$OUT/cap_conf.c"
ROOT=$(pwd)
docker run --rm --platform linux/amd64 -v "$ROOT:/work" -w /work "$IMG" sh -c "
set -e
CC=/opt/jieli/pi32v2/bin/clang
F='-target pi32v2 -Os -ffunction-sections -fno-builtin'
for b in $OUT/rf/*.bc ${STA:+$OUT/sta/*.bc}; do \$CC \$F -c \$b -o \${b%.bc}.o; done
\$CC \$F -c tools/wifi_capture/cap_crt.S -o $OUT/cap_crt.o
\$CC \$F -DCAP_RF_TRIM=$TRIM $STA_DEF $WLC_DEF -Wall -Wno-incompatible-library-redeclaration -c tools/wifi_capture/cap_main.c -o $OUT/cap_main.o
\$CC \$F -c $OUT/cap_conf.c -o $OUT/cap_conf.o
\$CC -target pi32v2 -nostdlib -Wl,-T,tools/wifi_capture/cap.ld -Wl,--gc-sections -Wl,-Map,$OUT/cap.map \
    -o $OUT/$NAME.elf $OUT/cap_crt.o $OUT/cap_main.o $OUT/cap_conf.o $OUT/rf/*.o $STA_OBJS \
    /opt/jieli/pi32v2/lib/r1/libcompiler-rt.a
/opt/jieli/pi32v2/bin/nm $OUT/$NAME.elf > $OUT/$NAME.syms
"
echo "built $OUT/$NAME.elf"
