# SPDX-License-Identifier: GPL-3.0-only
"""MENU.BIN from the flat binary: m-bootloader's stage 2 trailer (its docs/SPEC.md 11.5): "STG2", the CRC16 of everything
before the trailer, little endian, two zero bytes. usage: stage2_pack.py IN.bin OUT.bin"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

MIN_SIZE = 16
MAX_SIZE = 0x8000
MAGIC = b"STG2"


def crc16(data: bytes, crc: int = 0) -> int:
    """CRC-16/XMODEM, m-bootloader's core_crc16"""
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def pack(payload: bytes) -> bytes:
    image = payload + MAGIC + struct.pack("<H", crc16(payload)) + b"\0\0"
    if not MIN_SIZE <= len(image) <= MAX_SIZE:
        raise ValueError(f"MENU.BIN of {len(image)} B: it must be {MIN_SIZE}..{MAX_SIZE} B")
    return image


def main(argv: list) -> int:
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    try:
        image = pack(Path(argv[1]).read_bytes())
        Path(argv[2]).write_bytes(image)
    except (OSError, ValueError) as e:
        print(f"stage2_pack: {e}", file=sys.stderr)
        return 1
    print(f"{argv[2]}: {len(image)} B of {MAX_SIZE}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
