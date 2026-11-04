#!/bin/sh
# The SD card test images (tests/sd_test.c): a FAT16 and a FAT32 card made by the system's own formatter, so the
# filesystem code is checked against a real mkfs, not against itself. macOS only (newfs_msdos + hdiutil); the .img.gz
# files are committed, so the tests need neither. Run from the repository root:
#   sh tests/fixtures/sd/make_images.sh
set -eu
HERE=tests/fixtures/sd
WORK=worktrees/sd-img
mkdir -p "$WORK"

make_card() {   # NAME SIZE_MB FAT_BITS
    img="$WORK/$1.img"
    rm -f "$img"
    dd if=/dev/zero of="$img" bs=1m count="$2" 2>/dev/null
    dev=$(hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount "$img" | awk 'NR==1{print $1}')
    newfs_msdos -F "$3" -c 1 -v FM1SD "$dev" >/dev/null
    hdiutil detach "$dev" >/dev/null
    # (attached again with the system's own mount: a plain mount_msdos needs the kext loaded, refused unprivileged)
    att=$(hdiutil attach -imagekey diskimage-class=CRawDiskImage "$img" | tail -1)
    dev=$(echo "$att" | awk '{print $1}')
    mnt=$(echo "$att" | sed 's/^[^\/]*\/dev\/[a-z0-9]*[[:space:]]*//; s/^[[:space:]]*//')
    printf 'Hello from the FM-1 SD card\n' > "$mnt/HELLO.TXT"
    dd if=/dev/zero of="$mnt/DATA.BIN" bs=4096 count=1 2>/dev/null           # preallocated: the firmware writes in it
    mkdir "$mnt/SAMPLES"
    python3 -c "import sys; sys.stdout.buffer.write(bytes((i * 7 + (i >> 8)) & 255 for i in range(3000)))" \
        > "$mnt/SAMPLES/KICK.RAW"                                               # 3000 B: six sectors, a partial last one
    printf 'long name\n' > "$mnt/Long File Name.txt"                           # (Petit FatFs sees its 8.3 alias)
    sync
    hdiutil detach "$dev" >/dev/null
    gzip -9 -n -c "$img" > "$HERE/$1.img.gz"
    echo "$HERE/$1.img.gz: $(wc -c < "$HERE/$1.img.gz") bytes"
}

make_card fat16 8 16
make_card fat32 40 32
