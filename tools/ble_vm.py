# SPDX-License-Identifier: GPL-3.0-only
"""The JieLi SDK "VM" key/value store as stock V15 keeps it in flash: a reader and, for tests and the RF capture
only, a writer. Written from docs/BLE-HW-FACTS.md §14 (branch feat/ble-facts, d907ce3), not from vendor code.

  area     the first of CANDIDATES that starts 55 AA AA 55 AND whose first record (length > 0, inside the area)
           passes its check is the live one, else no VM (the magic alone is not enough: Optimist keeps UP_FM6's
           voices at 0x093000 on an FM-1, which must never read as a VM):
             0x0E8000, 4 KiB  measured on an FM-1 (2026-10-08, console 'flr' after stock V15 booted, then Optimist
                              installed by stock's updater): the magic and records 106 107 187 108 113 109; the next
                              sector, 0x0E9000, is BTIF (record 102), so the area is at most 4 KiB
             0x0E7000, 4 KiB  NOT measured: only a candidate for the second area (FF on that unit)
             0x093000 / 0x095000, 8 KiB each: the emulator's layout (§14.1; FF on the FM-1), both marked: 0x093000
           where stock's second area is on hardware, and its size there, are not known
  record   4-byte header then the data, packed from area + 4 (§14.2):
             byte 0  the low byte of CRC-16/XMODEM over the data bytes only (the check)
             byte 1  id bits [7:0]
             byte 2  [3:0] id bits [11:8], [7:4] length bits [3:0]
             byte 3  length bits [11:4]
           the log ends at the first header whose check fails (an erased header always does) or at a record that
           would end past the area; for each id the last valid record wins (§14.3)
  187      64-byte payload + CRC-16/XMODEM of it, little-endian, + 00 00 (§14.5)

The firmware's reader is firmware/src/ble/ble_vm.c (the same rules and candidates, tested in tests/ble_vm_test.c). The
BLE code never writes the VM (but see docs/BLE-STACK.md §12: UP_FM6's store overlaps 0x0E8000); this writer exists to
build test images and the capture's perturbed second boot."""
import sys

HW_VM_BASE = 0x0E8000                   # stock V15's VM on an FM-1 (measured)
HW_AREA_SIZE = 0x1000                   # (0x0E9000 is BTIF)
VM_BASE = 0x093000                      # the emulator's area A (its runs and the capture lay their VM here)
AREA_SIZE = 0x2000                      # the emulator's 8 KiB areas (§14.1)
CANDIDATES = ((HW_VM_BASE, HW_AREA_SIZE), (0x0E7000, HW_AREA_SIZE),     # (offset, size), in the order tried
              (VM_BASE, AREA_SIZE), (VM_BASE + AREA_SIZE, AREA_SIZE))
AREAS = tuple(a for a, _ in CANDIDATES)
FLASH_SIZE = 0x100000
MAGIC = bytes((0x55, 0xAA, 0xAA, 0x55))
RF_IDS = (106, 107, 108, 187)           # the RF trims (§14.5)
RF_LEN = {106: 2, 107: 7, 108: 20, 187: 68}


def crc16_xmodem(data, crc=0):
    """CRC-16/XMODEM: polynomial 0x1021, initial value 0, no reflection, no final XOR"""
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else crc << 1
        crc &= 0xFFFF
    return crc


def header(rid, data):
    n = len(data)
    if not 0 <= rid < 0x1000 or not 0 < n < 0x1000:
        raise ValueError(f"VM record id {rid} / length {n} out of range")
    return bytes((crc16_xmodem(data) & 0xFF, rid & 0xFF, (rid >> 8) & 0x0F | (n & 0x0F) << 4, n >> 4))


def rec187(payload64):
    """record 187's data: the 64-byte payload, its CRC-16/XMODEM little-endian, 00 00"""
    if len(payload64) != 64:
        raise ValueError("record 187 has a 64-byte payload")
    c = crc16_xmodem(payload64)
    return bytes(payload64) + bytes((c & 0xFF, c >> 8, 0, 0))


def rec187_ok(data):
    return len(data) == 68 and crc16_xmodem(data[:64]) == data[64] | data[65] << 8


def walk(area):
    """the records of one area (bytes from the area start) -> (records [(offset, id, data)], end offset); [] when the
    area has no magic"""
    if bytes(area[:4]) != MAGIC:
        return [], 0
    out, off = [], 4
    while off + 4 <= len(area):
        h = area[off:off + 4]
        rid = h[1] | (h[2] & 0x0F) << 8
        n = h[2] >> 4 | h[3] << 4
        if off + 4 + n > len(area):
            break
        data = bytes(area[off + 4:off + 4 + n])
        if crc16_xmodem(data) & 0xFF != h[0]:
            break
        out.append((off, rid, data))
        off += 4 + n
    return out, off


def area_size(area):
    """a candidate area's size (KeyError: not a candidate)"""
    return dict(CANDIDATES)[area]


def is_vm(area):
    """an area's bytes hold a VM: the magic, then a first record of length > 0 inside the area whose check holds"""
    if bytes(area[:4]) != MAGIC or len(area) < 8:
        return False
    h = area[4:8]
    n = h[2] >> 4 | h[3] << 4
    return 0 < n and 8 + n <= len(area) and crc16_xmodem(area[8:8 + n]) & 0xFF == h[0]


def read(image, base=0):
    """a raw flash image (image[0] = flash `base`: 0 for a whole-flash dump, else where a partial dump starts, such as
    0x0E6000 or VM_BASE) -> dict with 'area' (the live candidate's flash offset or None), 'size' (its size),
    'records' (its walk), 'end' (its log end, area-relative) and 'latest' {id: data} (last valid record per id). A
    candidate the image does not hold whole is skipped."""
    live = None
    for a, n in CANDIDATES:
        if base <= a and a + n <= base + len(image) and is_vm(image[a - base:a - base + n]):
            live = a
            break
    if live is None:
        return {"area": None, "size": 0, "records": [], "end": 0, "latest": {}}
    size = area_size(live)
    recs, end = walk(image[live - base:live - base + size])
    latest = {}
    for _, rid, data in recs:
        latest[rid] = data
    return {"area": live, "size": size, "records": recs, "end": end, "latest": latest}


def build_area(records, size=AREA_SIZE):
    """an area image (magic, the records in order, the rest erased) from [(id, data)]"""
    out = bytearray(MAGIC)
    for rid, data in records:
        out += header(rid, data) + bytes(data)
    if len(out) > size:
        raise ValueError("records do not fit the area")
    return bytes(out) + b"\xff" * (size - len(out))


def rewrite(image, area, changes):
    """a copy of a whole-flash image whose live VM records of the ids in changes {id: new data, same length} are
    replaced in place (the check bytes fixed): the capture's perturbed second boot"""
    out = bytearray(image)
    recs, _ = walk(image[area:area + area_size(area)])
    seen = set()
    for off, rid, data in recs:
        if rid in changes:
            new = bytes(changes[rid])
            if len(new) != len(data):
                raise ValueError(f"VM record {rid}: a change must keep the length")
            at = area + off
            out[at:at + 4] = header(rid, new)
            out[at + 4:at + 4 + len(new)] = new
            seen.add(rid)
    missing = set(changes) - seen
    if missing:
        raise ValueError(f"VM records {sorted(missing)} not in the log")
    return bytes(out)


# ---- decoding what the FM-1's console printed (BLE-STACK.md §12.7): 'blevmdump', or 'flr' reads, or a backup

def parse_dump(text, seen=None):
    """console lines "0E8000: 55 AA AA 55 ..." (flr / blevmdump: a 6-digit hex offset, a colon, hex bytes) -> a
    whole-flash image (base 0; FF where no line gave a byte) and how many bytes the lines gave; seen (a set), when
    given, gets every offset a line gave"""
    img = bytearray(b"\xff" * FLASH_SIZE)
    got = 0
    for line in text.splitlines():
        head, sep, rest = line.strip().partition(":")
        if not sep or len(head) != 6:
            continue
        try:
            off = int(head, 16)
            data = bytes(int(x, 16) for x in rest.split())
        except ValueError:
            continue
        for i, b in enumerate(data):
            a = off + i
            if 0 <= a < len(img):
                img[a] = b
                got += 1
                if seen is not None:
                    seen.add(a)
    return bytes(img), got


def report(image, base, seen=None):
    """a decoded VM as text: every candidate's first word, the live area, every valid record, the RF set
    (BLE-HW-FACTS §18.1 step 4); seen: the offsets a console log gave (parse_dump), else the image is all read"""
    vm = read(image, base)
    out = []
    for a, n in CANDIDATES:
        held = base <= a and a + n <= base + len(image) and (seen is None or all(a + i in seen for i in range(4)))
        first = image[a - base:a - base + 4].hex(" ") if held else "(not read)"
        out.append(f"area {a:06x}: {first}")
    if vm["area"] is None:
        return "\n".join(out + ["no VM (no candidate area starts 55 aa aa 55 with a valid first record)"])
    out.append(f"live area {vm['area']:06x} ({vm['size'] // 1024} KiB), log end +{vm['end']:#x} "
               f"({100 * vm['end'] // vm['size']} % of the area)")
    for off, rid, data in vm["records"]:
        out.append(f"  @{vm['area'] + off:06x} id {rid:4d} len {len(data):3d}  {data.hex(' ')}")
    for rid in RF_IDS:
        d = vm["latest"].get(rid)
        state = "missing" if d is None else f"len {len(d)}" + ("" if len(d) == RF_LEN[rid] else f" (expected {RF_LEN[rid]})")
        out.append(f"RF {rid}: {state}" + (f"  {d.hex(' ')}" if d is not None else ""))
    d187 = vm["latest"].get(187)
    out.append(f"187 inner CRC: {'ok' if d187 is not None and rec187_ok(d187) else 'FAILS or missing'}")
    complete = all(vm["latest"].get(r) is not None and len(vm["latest"][r]) == RF_LEN[r] for r in RF_IDS) and \
        d187 is not None and rec187_ok(d187)
    out.append("the RF set is complete: a BLE build can start the radio from it" if complete else
               "the RF set is NOT complete: a BLE build keeps the radio off (NO RF CAL) unless its copy has one")
    return "\n".join(out)


def main(argv=None):
    import argparse
    ap = argparse.ArgumentParser(description="decode stock V15's VM (the first candidate area marked 55 aa aa 55: "
                                 + ", ".join(f"{a:#08x}" for a in AREAS) + ") from a console log of 'blevmdump' or "
                                 "'flr' reads, a whole-flash backup (fm1_rescue.py), or a raw partial dump (--base)")
    ap.add_argument("file", help="the console log (text), a 1 MiB flash backup (.bin) or, with --base, a raw dump")
    ap.add_argument("--base", type=lambda v: int(v, 0), default=None,
                    help="the file is raw flash bytes starting at this offset (e.g. 0xe6000)")
    a = ap.parse_args(argv)
    with open(a.file, "rb") as f:
        raw = f.read()
    if a.base is not None:
        if a.base < 0 or a.base + len(raw) > FLASH_SIZE:
            print(f"{a.file}: {len(raw)} bytes at {a.base:#x} run past the 1 MiB flash", file=sys.stderr)
            return 1
        print(f"{a.file}: {len(raw)} raw bytes from {a.base:#08x}")
        print(report(raw, a.base))
        return 0
    if len(raw) == FLASH_SIZE:
        print(f"{a.file}: a whole-flash image")
        print(report(raw, 0))
        return 0
    seen = set()
    image, got = parse_dump(raw.decode("utf-8", "replace"), seen)
    if not got:
        print(f"{a.file}: no dump lines (\"0E8000: 55 aa ...\") found", file=sys.stderr)
        return 1
    print(f"{a.file}: {got} bytes in the log (the rest read as FF)")
    print(report(image, 0, seen))
    return 0


if __name__ == "__main__":
    sys.exit(main())
